#include "AppController.h"
#include "0_Base/Logger.h"
#include "0_Base/Board.h"
#include "3_Storage/StorageManager.h"
#include "4_Network/NetManager.h"
#include <ArduinoJson.h>

// 外部引用
extern NetManager gNetManager;

AppController::AppController(
    StateManager* stateMgr,
    CalibrationManager* calibMgr,
    SignalProcessor* signalProc,
    StorageManager* storageMgr,
    NetManager* netMgr,
    BleConfigServer* bleServer
) : _stateMgr(stateMgr),
    _calibMgr(calibMgr),
    _signalProc(signalProc),
    _storageMgr(storageMgr),
    _netMgr(netMgr),
    _bleServer(bleServer),
    _rawPhaseBuf(nullptr),
    _currentStage(0),
    _rawPhaseCount(0),
    _calibRestDbgCount(0),
    _calibMaxDbgCount(0),
    _monitoringClientNum(255)  // [FIX] Initialize monitoring client
{
    memset(_stageStarted, 0, sizeof(_stageStarted));
}

void AppController::init(void)
{
    PersonalCalibData_t calib = {0};
    if (_storageMgr->GetPersonalCalib(&calib) && calib.calib_timestamp > 0) {
        _signalProc->setCalibration(calib.rest_rms_mv, calib.max_rms_mv, calib.rest_mdf_hz, calib.max_mdf_hz, calib.peak_rms_mv, calib.has_curve, calib.curve_coef);
        LOG("[CTRL] Boot: loaded calib from A zone, has_curve=%d\n", calib.has_curve);
    } else {
        LOG("[CTRL] Boot: no calib in A zone\n");
    }

    _stateMgr->transitionTo(ST_IDLE);
    LOG("[CTRL] Boot: entering IDLE, background processing enabled\n");
    LOG("[CTRL] AppController initialized.\n");
}

void AppController::tick(void)
{
    // ===== Background: signal processing (all states) =====
    float rms = _signalProc->update();
    float mdf = _signalProc->getMDF();
    float fatigue = _signalProc->getFatigue();
    float activation = _signalProc->getActivation();
    uint8_t quality = (uint8_t)_signalProc->getSignalQuality();

    // ===== Background: C-zone storage (IDLE and MONITORING only) =====
    SystemState_t curState = _stateMgr->getState();
    if (rms > 0.0f && (curState == ST_IDLE || curState == ST_MONITORING)) {
        uint32_t ts = _netMgr->getCurrentTimestamp();
        if (!_netMgr->isNtpSynced()) return;  // [FIX] NTP not synced, skip C-zone write
        CZone_DataPoint_t pt;
        memset(&pt, 0, sizeof(pt));
        pt.timestamp_ms = ts;
        pt.rms_compressed = (uint16_t)(rms * 100.0f);
        pt.mdf_compressed = (uint16_t)(mdf * 10.0f);
        pt.fatigue_level = (uint8_t)(fatigue * 2.55f);
        pt.signal_quality = quality;
        bool czOk = _storageMgr->CZone_AppendDataPoint(&pt);
        static uint32_t czDbgCnt = 0;
        if (++czDbgCnt >= 100) {  // every ~10s at 10Hz
            czDbgCnt = 0;
            LOG("[CTRL] czAppend ok=%d rms=%.1f st=%d\n", czOk, rms, curState);
        }
    }

    // ===== State-specific logic =====
    switch (curState) {
        case ST_IDLE:
            // Nothing to do, processing and storage already done above
            break;

        case ST_MONITORING:
            _handleMonitoringState(rms, mdf, fatigue, quality, activation);
            break;

        case ST_CALIB_REST:
            _handleCalibRestState(rms, mdf);
            break;

        case ST_CALIB_WAIT:
            break;

        case ST_CALIB_MAX:
            _handleCalibMaxState(rms, mdf);
            break;

        case ST_CALIB_DONE:
            break;

        case ST_DB_FEATURE:
            _handleDbFeatureState(rms, mdf, fatigue, quality, activation);
            break;

        case ST_ERROR:
        default:
            _handleErrorState();
            break;
    }

    // [FIX] Deferred query_cz processing (avoid stack overflow in WS callback)
    if (_pendingQueryCZ) {
        _pendingQueryCZ = false;
        LOG("[CTRL] Processing deferred query_cz\n");
        handleQueryCZ(_pendingQueryCZClientNum, _pendingQueryStartTs, _pendingQueryEndTs);
    }
}

void AppController::onCommandReceived(AppCommand_t cmd, uint8_t clientNum)
{
    switch (cmd) {
        case CMD_START_CALIB: {
            SystemState_t st = _stateMgr->getState();
            LOG("[CTRL] CMD_START_CALIB: from state=%d\n", st);
            if (st != ST_IDLE) {
                _stateMgr->transitionTo(ST_IDLE);
            }
            if (_stateMgr->getState() != ST_IDLE) {
                LOG("[CTRL] Force reset from %s to IDLE\n", _stateMgr->getStateName());
                _stateMgr->forceReset();
            }
            _calibMgr->reset();
            _signalProc->clearCalibration();
            _calibRestDbgCount = 0;  // 重置调试计数�?
            _calibMaxDbgCount = 0;
            _calibMgr->beginPhase();
            _stateMgr->transitionTo(ST_CALIB_REST);
            _stateMgr->startCalibPhase(CALIB_REST_SEC);
            LOG("[CTRL] Calibration REST phase started %ds\n", CALIB_REST_SEC);
            break;
        }
        case CMD_START_CALIB_MAX: {
            LOG("[CALIB] CMD_START_CALIB_MAX reached\n");
            SystemState_t st = _stateMgr->getState();
            LOG("[CALIB] CMD_START_CALIB_MAX: state=%d (expected CALIB_WAIT=%d)\n", st, ST_CALIB_WAIT);
            if (st == ST_CALIB_WAIT) {
                LOG("[CALIB] about to resetEMA\n");
                _signalProc->resetEMA();
                LOG("[CALIB] about to beginPhase\n");
                _calibMgr->beginPhase();
                LOG("[CALIB] beginPhase done\n");
                _stateMgr->transitionTo(ST_CALIB_MAX);
                _stateMgr->startCalibPhase(CALIB_MAX_SEC);
                LOG("[CTRL] Calib MAX phase started (app-triggered) %ds\n", CALIB_MAX_SEC);
            } else {
                LOG("[CTRL] CMD_START_CALIB_MAX ignored, current state=%d (expected CALIB_WAIT)\n", st);
            }
            break;
        }
        case CMD_STOP: {
            SystemState_t st = _stateMgr->getState();
            if (st == ST_CALIB_REST || st == ST_CALIB_WAIT || st == ST_CALIB_MAX || st == ST_CALIB_DONE || st == ST_MONITORING || st == ST_ERROR) {
                _stateMgr->transitionTo(ST_IDLE);
                _monitoringClientNum = 255;  // [FIX] Clear monitoring client
                LOG("[CTRL] Stopped, back to IDLE\n");
            }
            break;
        }
        case CMD_RESET_CALIB: {
            // [FIX] 枚举路径已废弃，�?handleResetCalib(slot) 直接处理
            // 这里不再执行任何操作，保留枚举仅为兼�?tickLocalDebug 串口调试
            LOG("[CTRL] CMD_RESET_CALIB: use handleResetCalib(slot) instead\n");
            _monitoringClientNum = 255;  // [FIX] Clear monitoring client
            break;
        }
        case CMD_GET_CALIB_RESULT: {
            LOG("[CTRL] CMD_GET_CALIB_RESULT: use handleGetCalibResult() instead\n");
            break;
        }
        case CMD_SYNC_TIME: {
            _netMgr->disconnectWifi();
            _bleServer->startAdvertising();
            LOG("[CTRL] Entering BLE config mode\n");
            break;
        }
        case CMD_VERIFY_CALIB: {
            if (_calibMgr->validateResult()) {
                const CalibData_t& calib = _calibMgr->getData();
                LOG("[CTRL] Verify OK: RMS(%.2f/%.2f) MDF(%.1f/%.1f)\n", calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf);
            } else {
                LOG("[CTRL] Verify FAIL: invalid calibration data\n");
            }
            break;
        }
        case CMD_START_STREAM: {
            SystemState_t st = _stateMgr->getState();
            if (st == ST_MONITORING) {
                LOG("[CTRL] Already in monitoring\n");
                break;
            }
            _stateMgr->transitionTo(ST_MONITORING);
            _monitoringClientNum = clientNum;  // [FIX] Store client number
            LOG("[CTRL] Entering MONITORING (TCP stream enabled)\n");
            break;
        }
        default: {
            break;
        }
    }
}

// ===== State handlers (receive pre-computed values from tick()) =====

void AppController::_handleIdleState(void)
{
    // IDLE: background processing and C-zone storage already done in tick()
}

void AppController::_handleCalibRestState(float rms, float mdf)
{
    _calibMgr->addSample(rms, mdf);

    if (_calibRestDbgCount++ % 10 == 0) {
        LOG("[CTRL] CALIB_REST: rms=%.3f, mdf=%.1f\n", rms, mdf);
    }

    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, 0.0f, 0, 0.0f, true, "REST");
    }

    if (_stateMgr->isCalibPhaseComplete()) {
        _calibMgr->endPhase(true);
        LOG("[CTRL] Calib REST phase done. Waiting for app to trigger MAX...\n");
        _stateMgr->transitionTo(ST_CALIB_WAIT);
    }
}

void AppController::_handleCalibMaxState(float rms, float mdf)
{
    _calibMgr->addSample(rms, mdf);

    if (_calibMaxDbgCount++ % 10 == 0) {
        LOG("[CTRL] CALIB_MAX: rms=%.3f, mdf=%.1f, progress=%d%%\n", rms, mdf, _stateMgr->getCalibProgress());
    }

    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, 0.0f, 0, 0.0f, true, "MAX");
    }

    if (_stateMgr->isCalibPhaseComplete()) {
        _calibMgr->endPhase(false);
        _stateMgr->transitionTo(ST_CALIB_DONE);
        LOG("[CTRL] Calib MAX phase done. Waiting for app to get result & save...\n");
    }
}

void AppController::_handleMonitoringState(float rms, float mdf, float fatigue, uint8_t quality, float activation)
{
    // TCP upload only (signal processing and C-zone storage done in tick())
    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, fatigue, quality, activation);
    }
}

void AppController::_handleErrorState(void)
{
}

void AppController::_handleDbFeatureState(float rms, float mdf, float fatigue, uint8_t quality, float activation)
{
    if (_stageStarted[_currentStage] && _rawPhaseBuf && _rawPhaseCount < 3000) {
        uint16_t drained = _signalProc->drainNewSamples(_rawPhaseBuf + _rawPhaseCount, 3000 - _rawPhaseCount);
        _rawPhaseCount += drained;
        if (_rawPhaseCount >= 3000) {
            bool ok = _storageMgr->BZone_AppendRawPhase(_currentStage, _rawPhaseBuf, _rawPhaseCount);
            LOG("[CTRL] DB: Stage %d auto-saved %d samples, ok=%s\n", _currentStage, _rawPhaseCount, ok ? "YES" : "NO");
            _stageStarted[_currentStage] = false;
            _rawPhaseCount = 0;
            memset(_rawPhaseBuf, 0, 3000 * sizeof(int16_t));

            char resp[128];
            snprintf(resp, sizeof(resp), "{\"cmd\":\"raw_phase_auto\",\"stage\":%d,\"ok\":%s}", _currentStage, ok ? "true" : "false");
            _netMgr->sendJsonTo(_lastDbClientNum, resp);
        }
    }

    if (rms > 0.0f) {
        uint16_t rmsCompressed = (uint16_t)(rms * 100.0f);
        uint16_t mdfCompressed = (uint16_t)(mdf * 10.0f);
        _storageMgr->BZone_AppendFeaturePoint(rmsCompressed, mdfCompressed);
    }

    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, fatigue, quality, activation);
    }
}

// ==================== JSON command handlers ====================

void AppController::handleQueryCZ(uint8_t clientNum, uint32_t startTs, uint32_t endTs)
{
    LOG("[CTRL] handleQueryCZ: startTs=%lu, endTs=%lu\n", startTs, endTs);
    // 小程序未传时间参数时，默认查询最�?小时数据（使用NTP时间戳）
    if (startTs == 0) {
        uint32_t nowSec = _netMgr->getCurrentTimestamp();
        startTs = nowSec - 3600UL;  // 1小时前（Unix秒）
        LOG("[CTRL] handleQueryCZ: startTs adjusted to last 1h = %lu\n", startTs);
    }
    if (endTs == 0 || endTs == 0xFFFFFFFF) {
        endTs = _netMgr->getCurrentTimestamp();
    }

    // [NOTE] Skip CZone_FlushCache before query to avoid heap corruption risk
    // The latest few seconds of data may not be flushed to Flash yet

    CZone_DataPoint_t points[10];  // [FIX] Small stack array, query in batches of 10
    LOG("[CTRL] queryCZ: points on stack\n");
    if (false) {  // [FIX] static array, no alloc failure possible
        char buf[] = "{\"cmd\":\"cz_data\",\"points\":[],\"has_curve\":0,\"error\":\"nomem\"}";
        gNetManager.sendJsonTo(clientNum, buf);
        return;
    }

    uint16_t count = 0;
    uint32_t nextTs = 0;
    bool ok = _storageMgr->CZone_QueryByTimeRange(startTs, endTs, points, 10, &count, &nextTs);
    LOG("[CTRL] queryCZ: query done ok=%d count=%d\n", ok, count);

    if (!ok || count == 0) {
        PersonalCalibData_t calib = {0};
        bool hc = _storageMgr->GetPersonalCalib(&calib) && calib.has_curve;
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"cmd\":\"cz_data\",\"points\":[],\"has_curve\":%d}", hc ? 1 : 0);
        gNetManager.sendJsonTo(clientNum, buf);
        LOG("[CTRL] queryCZ: empty result sent\n");
    }

    // 先缓�?has_curve，避免每 batch 重复�?Flash
    PersonalCalibData_t calibInfo = {0};
    bool hasCurve = _storageMgr->GetPersonalCalib(&calibInfo) && calibInfo.has_curve;

    for (uint16_t batch = 0; batch < count; batch += 10) {
        static StaticJsonDocument<512> doc;  // [FIX] BSS段避免栈溢出
        doc.clear();
        doc["cmd"] = "cz_data";
        doc["has_curve"] = hasCurve ? 1 : 0;
        JsonArray pts = doc.createNestedArray("points");
        uint16_t batchEnd = (batch + 10 < count) ? batch + 10 : count;
        for (uint16_t i = batch; i < batchEnd; i++) {
            JsonObject pt = pts.createNestedObject();
            pt["ts"] = points[i].timestamp_ms;
            pt["rms"] = points[i].rms_compressed / 100.0f;
            pt["mdf"] = points[i].mdf_compressed / 10.0f;
            pt["f"] = points[i].fatigue_level / 255.0f;
            pt["q"] = points[i].signal_quality;
        }
        if (batch + 10 < count) {
            doc["more"] = true;
            doc["next_ts"] = nextTs;
        }
        static char buf[512];  // [FIX] BSS段避免栈溢出
        serializeJson(doc, buf, sizeof(buf));
        gNetManager.sendJsonTo(clientNum, buf);
    }
    LOG("[CTRL] queryCZ: complete\n");
}
void AppController::deferQueryCZ(uint8_t clientNum, uint32_t startTs, uint32_t endTs)
{
    _pendingQueryCZ = true;
    _pendingQueryCZClientNum = clientNum;
    _pendingQueryStartTs = startTs;
    _pendingQueryEndTs = endTs;
    LOG("[CTRL] query_cz deferred to tick\n");
}


void AppController::handleSaveRecord(uint8_t clientNum, JsonObject doc)
{
    const char* name = doc["name"] | "unnamed";
    uint32_t subjectId = doc["subject_id"] | (millis() / 1000);
    uint8_t age = doc["age"] | 25;
    uint8_t gender = doc["gender"] | 1;
    uint8_t handedness = doc["handedness"] | 2;
    uint32_t testTs = doc["test_timestamp"] | (millis() / 1000);
    uint16_t durationSec = doc["test_duration_sec"] | 60;
    float restRms = doc["rest_rms_mv"] | 0.0f;
    float maxRms = doc["max_rms_mv"] | 0.0f;
    float restMdf = doc["rest_mdf_hz"] | 0.0f;
    float maxMdf = doc["max_mdf_hz"] | 0.0f;

    JsonArray rmsArr = doc["rms_sequence"];
    JsonArray mdfArr = doc["mdf_sequence"];
    uint16_t seqLen = rmsArr.size();
    if (seqLen == 0 || seqLen != mdfArr.size()) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"save_result\",\"ok\":false,\"err\":\"invalid_sequence\"}");
        return;
    }

    uint32_t slotAddr;
    if (!_storageMgr->BZone_GetNextAvailableSlot(&slotAddr)) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"save_result\",\"ok\":false,\"err\":\"b_zone_full\"}");
        return;
    }

    SubjectBasicInfo_t subject = { subjectId, age, gender, handedness };
    if (!_storageMgr->BZone_BeginRecord(&subject, slotAddr)) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"save_result\",\"ok\":false,\"err\":\"begin_failed\"}");
        return;
    }

    const uint16_t BATCH_SIZE = 256;
    uint16_t rmsBuf[BATCH_SIZE];
    uint16_t mdfBuf[BATCH_SIZE];
    bool writeOk = true;
    for (uint16_t batch = 0; batch < seqLen && writeOk; batch += BATCH_SIZE) {
        uint16_t batchLen = (seqLen - batch > BATCH_SIZE) ? BATCH_SIZE : (seqLen - batch);
        for (uint16_t i = 0; i < batchLen; i++) {
            rmsBuf[i] = (uint16_t)(rmsArr[batch + i].as<float>() * 100);
            mdfBuf[i] = (uint16_t)(mdfArr[batch + i].as<float>() * 10);
        }
        if (!_storageMgr->BZone_AppendFeatureSequence(rmsBuf, mdfBuf, batchLen)) {
            writeOk = false;
        }
    }

    if (!writeOk) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"save_result\",\"ok\":false,\"err\":\"write_failed\"}");
        return;
    }

    _storageMgr->BZone_EndRecord();
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"cmd\":\"save_result\",\"ok\":true,\"slot\":%lu}", (unsigned long)slotAddr);
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleListRecords(uint8_t clientNum)
{
    BZone_ListEntry_t entries[20];
    uint8_t count = 0;
    _storageMgr->BZone_ListRecords(entries, 20, &count);

    StaticJsonDocument<2048> doc;
    doc["cmd"] = "record_list";
    JsonArray recs = doc.createNestedArray("records");
    for (uint8_t i = 0; i < count; i++) {
        JsonObject rec = recs.createNestedObject();
        rec["slot"] = entries[i].slot_addr;
        rec["name"] = entries[i].name;
        rec["subject_id"] = entries[i].subject_id;
        rec["timestamp"] = entries[i].test_timestamp;
        rec["duration_sec"] = entries[i].test_duration_sec;
        rec["age"] = entries[i].age;
        rec["gender"] = entries[i].gender;
        rec["quality"] = entries[i].snap_calib_quality;
    }
    char buf[2048];
    serializeJson(doc, buf, sizeof(buf));
    gNetManager.sendJsonTo(clientNum, buf);
}

void AppController::handleDeleteRecord(uint8_t clientNum, uint32_t slotAddr)
{
    bool ok = _storageMgr->BZone_DeleteSlot(slotAddr);
    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"delete_result\",\"ok\":true,\"slot\":%lu}", (unsigned long)slotAddr);
    } else {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"delete_result\",\"ok\":false,\"err\":\"invalid_slot\",\"slot\":%lu}", (unsigned long)slotAddr);
    }
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleListCurves(uint8_t clientNum, uint8_t gender, uint8_t handedness, uint8_t age, int seq)
{
    BZone_CurveEntry_t entries[20];
    uint8_t count = 0;
    bool ok = _storageMgr->BZone_ListCurves(gender, handedness, age, entries, 20, &count);

    StaticJsonDocument<4096> doc;
    doc["cmd"] = "curve_list";
    if (seq >= 0) doc["seq"] = seq;
    doc["ok"] = ok;
    doc["count"] = count;
    JsonArray curves = doc.createNestedArray("curves");
    for (uint8_t i = 0; i < count; i++) {
        JsonObject c = curves.createNestedObject();
        c["curve_id"] = entries[i].curve_id;
        c["curve_type"] = entries[i].curve_type;
        JsonArray coef = c.createNestedArray("coef");
        for (uint8_t j = 0; j < 5; j++) coef.add(entries[i].coefficients[j]);
        c["baseline_rms"] = entries[i].group_baseline_rms;
        c["baseline_mdf"] = entries[i].group_baseline_mdf;
        c["gender"] = entries[i].gender_hint;
        c["age_min"] = entries[i].age_min;
        c["age_max"] = entries[i].age_max;
        c["handedness"] = entries[i].handedness_hint;
        c["slot_count"] = entries[i].active_slot_count;
        c["status"] = entries[i].curve_status;
        c["quality"] = entries[i].fit_quality;
        c["desc"] = entries[i].description;
    }
    char buf[4096];
    serializeJson(doc, buf, sizeof(buf));
    gNetManager.sendJsonTo(clientNum, buf);
}

void AppController::handleGenPersonalCurve(uint8_t clientNum, uint8_t bcurveId, float baselineRms, float baselineMdf, int seq)
{
    bool ok = _storageMgr->GeneratePersonalCurve(bcurveId, baselineRms, baselineMdf);
    char resp[256];
    if (ok) {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"curve_gen_result\",\"ok\":true,\"curve_id\":%d,\"baseline_rms\":%.2f,\"baseline_mdf\":%.2f,\"seq\":%d}", bcurveId, baselineRms, baselineMdf, seq);
    } else {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"curve_gen_result\",\"ok\":false,\"err\":\"generation_failed\",\"seq\":%d}", seq);
    }
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleGetCurve(uint8_t clientNum, uint8_t curveId)
{
    BZone_CurveEntry_t curve;
    bool ok = _storageMgr->BZone_GetCurve(curveId, &curve);

    StaticJsonDocument<1024> doc;
    doc["cmd"] = "curve_detail";
    doc["ok"] = ok;
    if (ok) {
        doc["curve_id"] = curve.curve_id;
        doc["curve_type"] = curve.curve_type;
        JsonArray coef = doc.createNestedArray("coef");
        for (uint8_t j = 0; j < 5; j++) coef.add(curve.coefficients[j]);
        doc["baseline_rms"] = curve.group_baseline_rms;
        doc["baseline_mdf"] = curve.group_baseline_mdf;
        doc["gender"] = curve.gender_hint;
        doc["age_min"] = curve.age_min;
        doc["age_max"] = curve.age_max;
        doc["handedness"] = curve.handedness_hint;
        doc["status"] = curve.curve_status;
        doc["quality"] = curve.fit_quality;
    } else {
        doc["err"] = "curve_not_found";
    }
    char buf[1024];
    serializeJson(doc, buf, sizeof(buf));
    gNetManager.sendJsonTo(clientNum, buf);
}

void AppController::handleStartDbFeature(uint8_t clientNum, JsonObject doc)
{
    _lastDbClientNum = clientNum;

    uint32_t subjectId = doc["subject_id"] | (millis() / 1000);
    uint8_t age = doc["age"] | 25;
    uint8_t gender = doc["gender"] | 1;
    uint8_t handedness = doc["handedness"] | 2;

    uint32_t slotAddr;
    if (!_storageMgr->BZone_GetNextAvailableSlot(&slotAddr)) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_feature_started\",\"ok\":false,\"err\":\"b_zone_full\"}");
        return;
    }

    SubjectBasicInfo_t subject = { subjectId, age, gender, handedness };
    if (!_storageMgr->BZone_BeginRecord(&subject, slotAddr)) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_feature_started\",\"ok\":false,\"err\":\"begin_failed\"}");
        return;
    }

    _stateMgr->transitionTo(ST_DB_FEATURE);
    _rawPhaseBuf = (int16_t*)malloc(3000 * sizeof(int16_t));
    if (!_rawPhaseBuf) {
        _stateMgr->transitionTo(ST_IDLE);
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_feature_started\",\"ok\":false,\"err\":\"malloc_failed\"}");
        return;
    }

    LOG("[CTRL] DB Feature started at slot 0x%06X, subject=%lu, age=%d, gender=%d, handedness=%d\n", slotAddr, (unsigned long)subjectId, age, gender, handedness);
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"cmd\":\"db_feature_started\",\"ok\":true,\"slot\":%lu}", (unsigned long)slotAddr);
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleCaptureRawPhase(uint8_t clientNum, JsonObject doc)
{
    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"raw_phase_done\",\"ok\":false,\"err\":\"not_in_db_feature\"}");
        return;
    }

    uint8_t phaseIndex = doc["phase"] | 1;
    uint16_t samplesPerPhase = doc["samples"] | 3000;
    JsonArray rawArr = doc["raw"].as<JsonArray>();

    if (rawArr.isNull() || rawArr.size() == 0) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"raw_phase_done\",\"ok\":false,\"err\":\"empty_raw\"}");
        return;
    }

    if (!_rawPhaseBuf) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"raw_phase_done\",\"ok\":false,\"err\":\"buf_not_init\"}");
        return;
    }

    uint16_t count = rawArr.size();
    if (count > 3000) count = 3000;

    for (uint16_t i = 0; i < count; i++) {
        _rawPhaseBuf[i] = (int16_t)rawArr[i].as<int>();
    }

    bool ok = _storageMgr->BZone_AppendRawPhase(phaseIndex, _rawPhaseBuf, count);
    LOG("[CTRL] DB: capture_raw_phase phase=%d, samples=%d, ok=%s\n", phaseIndex, count, ok ? "YES" : "NO");

    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"raw_phase_done\",\"ok\":true,\"phase\":%d,\"samples\":%d}", phaseIndex, count);
    } else {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"raw_phase_done\",\"ok\":false,\"err\":\"write_failed\",\"phase\":%d}", phaseIndex);
    }
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleRawPhaseDone(uint8_t clientNum)
{
    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_record_saved\",\"ok\":false,\"err\":\"not_in_db_feature\"}");
        return;
    }

    for (uint8_t i = 0; i < 4; i++) {
        if (_stageStarted[i]) {
            LOG("[CTRL] DB: stage %d raw data confirmed saved\n", i);
        } else {
            LOG("[CTRL] DB: WARNING - stage %d raw data NOT saved!\n", i);
        }
    }

    bool ok = _storageMgr->BZone_EndRecord();
    _stateMgr->transitionTo(ST_IDLE);

    free(_rawPhaseBuf);
    _rawPhaseBuf = nullptr;

    _currentStage = 0;
    memset(_stageStarted, 0, sizeof(_stageStarted));
    _rawPhaseCount = 0;

    LOG("[CTRL] DB: record saved, ok=%s\n", ok ? "YES" : "NO");
    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"db_record_saved\",\"ok\":true}");
    } else {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"db_record_saved\",\"ok\":false,\"err\":\"commit_failed\"}");
    }
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleGetCalibResult(uint8_t clientNum, int seq, uint8_t slot)
{
    LOG("[CTRL] handleGetCalibResult: client=%d seq=%d slot=%d\n", clientNum, seq, slot);

    SystemState_t st = _stateMgr->getState();

    // 校准刚完成（ST_CALIB_DONE）：�?_calibMgr RAM 读取实时校准结果
    if (st == ST_CALIB_DONE) {
        const CalibData_t& calib = _calibMgr->getData();
        bool valid = _calibMgr->validateResult();
        LOG("[CTRL] CALIB_DONE: rest_rms=%.2f ref_rms=%.2f rest_mdf=%.1f ref_mdf=%.1f valid=%d\n",
            calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf, valid);

        char buf[512];
        snprintf(buf, sizeof(buf),
            "{\"cmd\":\"calib_result\",\"ok\":true,\"seq\":%d,"
            "\"rest_rms\":%.2f,\"max_rms\":%.2f,\"rest_mdf\":%.1f,\"max_mdf\":%.1f,"
            "\"peak_rms\":%.2f,\"validateResult\":%d}",
            seq, calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf,
            calib.peak_rms, valid ? 1 : 0);

        _netMgr->sendJsonTo(clientNum, buf);
        LOG("[CTRL] sendJsonTo done\n");
        return;
    }

    // 非校准状态：�?Flash 读取指定 slot 的已保存校准数据
    PersonalCalibData_t pcData;
    bool hasData = _storageMgr->GetPersonalCalibBySlot(slot, &pcData);

    if (!hasData || pcData.calib_timestamp == 0) {
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"cmd\":\"calib_result\",\"ok\":false,\"seq\":%d,\"err\":\"no_calib_data\"}", seq);
        _netMgr->sendJsonTo(clientNum, buf);
        LOG("[CTRL] slot=%d has no calib data\n", slot);
        return;
    }

    LOG("[CTRL] slot=%d calib: rest_rms=%.2f max_rms=%.2f rest_mdf=%.1f max_mdf=%.1f\n",
        slot, pcData.rest_rms_mv, pcData.max_rms_mv, pcData.rest_mdf_hz, pcData.max_mdf_hz);

    char buf[512];
    // Flash路径：内联验证（RMS倍数+最小�?MDF范围+样本数）
    // Flash中无样本数信息，但从Flash读取说明已保存成功（当时validateResult已通过�?
    // 因此Flash路径直接标记为valid=1
    bool fwValid = (pcData.max_rms_mv > pcData.rest_rms_mv * 2.0f && pcData.max_rms_mv >= 0.5f
                    && pcData.rest_mdf_hz >= 10.0f && pcData.rest_mdf_hz <= 250.0f
                    && pcData.max_mdf_hz >= 10.0f && pcData.max_mdf_hz <= 250.0f
                    && pcData.calib_quality > 0);  // calib_quality>0 证实曾经通过验证
    snprintf(buf, sizeof(buf),
        "{\"cmd\":\"calib_result\",\"ok\":true,\"seq\":%d,"
        "\"rest_rms\":%.2f,\"max_rms\":%.2f,\"rest_mdf\":%.1f,\"max_mdf\":%.1f,"
        "\"peak_rms\":%.2f,\"has_curve\":%d,\"validateResult\":%d}",
        seq, pcData.rest_rms_mv, pcData.max_rms_mv, pcData.rest_mdf_hz, pcData.max_mdf_hz,
        pcData.peak_rms_mv, pcData.has_curve, fwValid ? 1 : 0);

    _netMgr->sendJsonTo(clientNum, buf);
    LOG("[CTRL] sendJsonTo done\n");
}

void AppController::handleSaveCalib(uint8_t clientNum, int seq, uint8_t slot)
{
    SystemState_t st = _stateMgr->getState();
    if (st != ST_CALIB_DONE) {
        char err[80];
        snprintf(err, sizeof(err), "{\"cmd\":\"calib_saved\",\"ok\":false,\"seq\":%d,\"err\":\"not_in_calib_done\"}", seq);
        _netMgr->sendJsonTo(clientNum, err);
        return;
    }

    if (!_calibMgr->validateResult()) {
        char err[80];
        snprintf(err, sizeof(err), "{\"cmd\":\"calib_saved\",\"ok\":false,\"seq\":%d,\"err\":\"validation_failed\"}", seq);
        _netMgr->sendJsonTo(clientNum, err);
        // 留在 CALIB_DONE 状态，让小程序端决定后续操�?
        return;
    }

    const CalibData_t& calib = _calibMgr->getData();
    PersonalCalibData_t pcData = {0};
    pcData.rest_rms_mv = calib.rest_rms;
    pcData.max_rms_mv = calib.ref_rms;
    pcData.rest_mdf_hz = calib.rest_mdf;
    pcData.max_mdf_hz = calib.ref_mdf;
    pcData.peak_rms_mv = calib.peak_rms;
    pcData.calib_timestamp = _netMgr->getCurrentTimestamp();
    pcData.calib_quality = 100;  // validateResult 已通过，直�?100
    _storageMgr->UpdatePersonalCalib(&pcData, slot);

    PersonalCalibData_t latest;
    _storageMgr->GetPersonalCalib(&latest);
    _signalProc->setCalibration(calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf, calib.peak_rms, latest.has_curve, latest.curve_coef);

    // [FIX] 先发响应再转IDLE，确保小程序后续get_calib_result能从RAM读到数据
    char buf[80];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"calib_saved\",\"ok\":true,\"seq\":%d,\"has_curve\":%d}", seq, latest.has_curve);
    _netMgr->sendJsonTo(clientNum, buf);
    LOG("[CTRL] Calib saved: RMS(%.2f/%.2f/peak=%.2f) MDF(%.1f/%.1f) has_curve=%d\n", calib.rest_rms, calib.ref_rms, calib.peak_rms, calib.rest_mdf, calib.ref_mdf, latest.has_curve);

    _stateMgr->transitionTo(ST_IDLE);
    LOG("[CTRL] Calib saved, back to IDLE (wait for start_stream to enter MONITORING)\n");
}

void AppController::handleResetCalib(uint8_t slot)
{
    _calibMgr->reset();
    _signalProc->clearCalibration();
    PersonalCalibData_t emptyData = {0};
    _storageMgr->UpdatePersonalCalib(&emptyData, slot);
    _stateMgr->transitionTo(ST_IDLE);
    LOG("[CTRL] Calibration reset, slot=%d\n", slot);
}

void AppController::handleDbMark(uint8_t clientNum, JsonObject doc)
{
    _lastDbClientNum = clientNum;

    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_marked\",\"ok\":false,\"err\":\"not_in_db_feature\"}");
        return;
    }

    uint8_t markIndex = doc["stage"] | 0;
    if (markIndex >= 4) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_marked\",\"ok\":false,\"err\":\"invalid_stage\"}");
        return;
    }

    if (markIndex > 0 && _rawPhaseBuf && _rawPhaseCount > 0) {
        uint8_t prevStage = markIndex - 1;
        LOG("[CTRL] DB: Saving raw phase %d (%d samples)\n", prevStage, _rawPhaseCount);
        bool saveOk = _storageMgr->BZone_AppendRawPhase(prevStage, _rawPhaseBuf, _rawPhaseCount);
        if (saveOk) {
            LOG("[CTRL] DB: Raw phase %d saved OK\n", prevStage);
        } else {
            LOG("[CTRL] DB: ERROR saving raw phase %d\n", prevStage);
        }
        _rawPhaseCount = 0;
        memset(_rawPhaseBuf, 0, 3000 * sizeof(int16_t));
    }

    _currentStage = markIndex;
    _stageStarted[markIndex] = true;
    LOG("[CTRL] DB: Stage %d started collecting\n", markIndex);

    uint16_t featureIdx = _storageMgr->BZone_GetFeatureCount();
    bool ok = _storageMgr->BZone_MarkFeaturePoint(markIndex);
    LOG("[CTRL] DB: db_mark stage=%d, featureIdx=%d, ok=%s\n", markIndex, featureIdx, ok ? "YES" : "NO");

    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"db_marked\",\"ok\":true,\"stage\":%d,\"feature_idx\":%d}", markIndex, featureIdx);
    } else {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"db_marked\",\"ok\":false,\"err\":\"mark_failed\",\"stage\":%d}", markIndex);
    }
    gNetManager.sendJsonTo(clientNum, resp);

    if (markIndex >= 3) {
        LOG("[CTRL] DB: all 4 stages marked, ready to save\n");
    }
}

