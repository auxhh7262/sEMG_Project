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
    _rawPhaseCount(0)
{
    memset(_stageStarted, 0, sizeof(_stageStarted));
}

void AppController::init(void)
{
    PersonalCalibData_t calib = {0};
    if (_storageMgr->GetPersonalCalib(&calib) && calib.calib_timestamp > 0) {
        _signalProc->setCalibration(calib.rest_rms_mv, calib.max_rms_mv, calib.rest_mdf_hz, calib.max_mdf_hz, calib.peak_rms_mv, calib.has_curve, calib.curve_coef);
        LOG("[CTRL] Boot: loaded calib from A区 has_curve=%d\n", calib.has_curve);
    } else {
        LOG("[CTRL] Boot: no calib in A区\n");
    }

    _stateMgr->transitionTo(ST_IDLE);
    _netMgr->stopStreaming(); 
    LOG("[CTRL] Boot: entering IDLE, waiting for start_stream command\n");
    LOG("[CTRL] AppController initialized.\n");
}

void AppController::tick(void)
{
    switch (_stateMgr->getState()) {
        case ST_IDLE:        _handleIdleState(); break;
        case ST_CALIB_REST:  _handleCalibRestState(); break;
        case ST_CALIB_WAIT:  break;
        case ST_CALIB_MAX:   _handleCalibMaxState(); break;
        case ST_CALIB_DONE:  break;
        case ST_MONITORING:  _handleMonitoringState(); break;
        case ST_DB_FEATURE:  _handleDbFeatureState(); break;
        case ST_ERROR:
        default:             _handleErrorState(); break;
    }
}

void AppController::onCommandReceived(AppCommand_t cmd)
{
    switch (cmd) {
        case CMD_START_CALIB: {
            SystemState_t st = _stateMgr->getState();
            if (st == ST_MONITORING) {
                LOG("[CTRL] Already in monitoring, ignore CMD_START_CALIB\n");
                break;
            }
            if (st == ST_ERROR) {
                LOG("[CTRL] Error state, must STOP first before new calibration\n");
                break;
            }
            if (st == ST_IDLE) {
                _calibMgr->reset();
                _calibMgr->beginPhase();
                _netMgr->stopStreaming(); // 进入校准前确保关闭实时数据流
                _stateMgr->transitionTo(ST_CALIB_REST);
                _stateMgr->startCalibPhase(CALIB_REST_SEC);
                LOG("[CTRL] Calibration started: REST phase %ds\n", CALIB_REST_SEC);
            }
            break;
        }
        case CMD_START_CALIB_MAX: {
            SystemState_t st = _stateMgr->getState();
            if (st == ST_CALIB_WAIT) {
                _signalProc->resetEMA();
                _calibMgr->beginPhase();
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
                _netMgr->stopStreaming(); // 停止数据流
                _stateMgr->transitionTo(ST_IDLE);
                LOG("[CTRL] Stopped, back to IDLE\n");
            }
            break;
        }
        case CMD_RESET_CALIB: {
            _calibMgr->reset();
            _signalProc->clearCalibration();
            PersonalCalibData_t emptyData = {0};
            _storageMgr->UpdatePersonalCalib(&emptyData);
            _netMgr->stopStreaming(); // 停止数据流
            _stateMgr->transitionTo(ST_IDLE);
            LOG("[CTRL] Calibration reset\n");
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
                LOG("[CTRL] Already in monitoring (streaming)\n");
                _netMgr->startStreaming(); // 确保开启
                break;
            }
            if (st == ST_IDLE) {
                LOG("[CTRL] Starting streaming (no calibration)\n");
                // [关键修复] 先开启流，再切换状态，防止被 IDLE 轮询误关
                _netMgr->startStreaming(); 
                _stateMgr->transitionTo(ST_MONITORING);
                break;
            }
            // 其他状态：先停止当前操作，再进入MONITORING
            LOG("[CTRL] Stopping current operation, entering monitoring\n");
            _netMgr->stopStreaming();
            _stateMgr->transitionTo(ST_IDLE);
            _netMgr->startStreaming();
            _stateMgr->transitionTo(ST_MONITORING);
            break;
        }
        default: {
            break;
        }
    }
}

void AppController::_handleIdleState(void)
{
    // [关键修复] 彻底移除此处的 _netMgr->stopStreaming();
    // 流的开启和关闭必须只由 onCommandReceived (状态切换动作) 显式触发！
    // 在 tick() 的高频循环里调用 stopStreaming 会导致与 startStreaming 的微秒级时序竞争，把刚开启的流误关！
}

void AppController::_handleCalibRestState(void)
{
    float rms = _signalProc->update();
    float mdf = _signalProc->getMDF();
    _calibMgr->addSample(rms, mdf);

    static uint32_t debug_counter = 0;
    if (debug_counter++ % 10 == 0) {
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

void AppController::_handleCalibMaxState(void)
{
    float rms = _signalProc->update();
    float mdf = _signalProc->getMDF();
    _calibMgr->addSample(rms, mdf);

    static uint32_t max_debug_counter = 0;
    if (max_debug_counter++ % 10 == 0) {
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

void AppController::_handleMonitoringState(void)
{
    float rms = _signalProc->update();
    float mdf = _signalProc->getMDF();
    float fatigue = _signalProc->getFatigue();
    float activation = _signalProc->getActivation();
    uint8_t quality = (uint8_t)_signalProc->getSignalQuality();

    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, fatigue, quality, activation);

        CZone_DataPoint_t pt;
        memset(&pt, 0, sizeof(pt));
        pt.timestamp_ms = millis();
        pt.rms_compressed = (uint16_t)(rms * 100.0f);
        pt.mdf_compressed = (uint16_t)(mdf * 10.0f);
        pt.fatigue_level = (uint8_t)(fatigue * 2.55f);
        pt.signal_quality = quality;
        _storageMgr->CZone_AppendDataPoint(&pt);
    }
}

void AppController::_handleErrorState(void)
{
}

void AppController::_handleDbFeatureState(void)
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

    float rms = _signalProc->update();
    float mdf = _signalProc->getMDF();
    float fatigue = _signalProc->getFatigue();
    float activation = _signalProc->getActivation();
    uint8_t quality = (uint8_t)_signalProc->getSignalQuality();

    if (rms > 0.0f) {
        uint16_t rmsCompressed = (uint16_t)(rms * 100.0f);
        uint16_t mdfCompressed = (uint16_t)(mdf * 10.0f);
        _storageMgr->BZone_AppendFeaturePoint(rmsCompressed, mdfCompressed);
    }

    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, fatigue, quality, activation);
    }
}

// ==================== JSON 命令处理实现 ====================

void AppController::handleQueryCZ(uint8_t clientNum, uint32_t startTs, uint32_t endTs)
{
    CZone_DataPoint_t* points = new CZone_DataPoint_t[100];
    if (!points) {
        char buf[] = "{\"cmd\":\"cz_data\",\"points\":[],\"has_curve\":0,\"error\":\"nomem\"}";
        gNetManager.sendJsonTo(clientNum, buf);
        return;
    }

    uint16_t count = 0;
    uint32_t nextTs = 0;
    bool ok = _storageMgr->CZone_QueryByTimeRange(startTs, endTs, points, 100, &count, &nextTs);

    if (!ok || count == 0) {
        PersonalCalibData_t calib = {0};
        bool hc = _storageMgr->GetPersonalCalib(&calib) && calib.has_curve;
        char buf[128];
        snprintf(buf, sizeof(buf), "{\"cmd\":\"cz_data\",\"points\":[],\"has_curve\":%d}", hc ? 1 : 0);
        gNetManager.sendJsonTo(clientNum, buf);
        delete[] points;
        return;
    }

    for (uint16_t batch = 0; batch < count; batch += 20) {
        StaticJsonDocument<1024> doc;
        doc["cmd"] = "cz_data";
        PersonalCalibData_t calibInfo = {0};
        bool hasC = _storageMgr->GetPersonalCalib(&calibInfo) && calibInfo.has_curve;
        doc["has_curve"] = hasC ? 1 : 0;
        JsonArray pts = doc.createNestedArray("points");
        uint16_t batchEnd = (batch + 20 < count) ? batch + 20 : count;
        for (uint16_t i = batch; i < batchEnd; i++) {
            JsonObject pt = pts.createNestedObject();
            pt["ts"] = points[i].timestamp_ms;
            pt["rms"] = points[i].rms_compressed / 100.0f;
            pt["mdf"] = points[i].mdf_compressed / 10.0f;
            pt["f"] = points[i].fatigue_level / 255.0f;
            pt["q"] = points[i].signal_quality;
        }
        if (batch + 20 < count) {
            doc["more"] = true;
            doc["next_ts"] = nextTs;
        }
        char buf[1024];
        serializeJson(doc, buf, sizeof(buf));
        gNetManager.sendJsonTo(clientNum, buf);
    }
    delete[] points;
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

void AppController::handleListCurves(uint8_t clientNum, uint8_t gender, uint8_t handedness, uint8_t age)
{
    BZone_CurveEntry_t entries[20];
    uint8_t count = 0;
    bool ok = _storageMgr->BZone_ListCurves(gender, handedness, age, entries, 20, &count);

    StaticJsonDocument<4096> doc;
    doc["cmd"] = "curve_list";
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

void AppController::handleGenPersonalCurve(uint8_t clientNum, uint8_t bcurveId, float baselineRms, float baselineMdf)
{
    bool ok = _storageMgr->GeneratePersonalCurve(bcurveId, baselineRms, baselineMdf);
    char resp[256];
    if (ok) {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"curve_gen_result\",\"ok\":true,\"curve_id\":%d,\"baseline_rms\":%.2f,\"baseline_mdf\":%.2f}", bcurveId, baselineRms, baselineMdf);
    } else {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"curve_gen_result\",\"ok\":false,\"err\":\"generation_failed\"}");
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
    _netMgr->stopStreaming(); 
    _stateMgr->transitionTo(ST_IDLE);

    free(_rawPhaseBuf);
    _rawPhaseBuf = nullptr;

    _currentStage = 0;
    memset(_stageStarted, 0, sizeof(_stageStarted));
    _currentStage = 0;;
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

void AppController::handleGetCalibResult(uint8_t clientNum, int seq)
{
    Serial.println("GCR_ENTER");
    Serial.println(clientNum);
    Serial.println(seq);
    LOG("[CTRL] handleGetCalibResult: client=%d seq=%d\n", clientNum, seq);

    const CalibData_t& calib = _calibMgr->getData();
    LOG("[CTRL] calib data: rest_rms=%.2f ref_rms=%.2f rest_mdf=%.1f ref_mdf=%.1f peak_rms=%.2f\n", calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf, calib.peak_rms);
    bool valid = _calibMgr->validateResult();
    LOG("[CTRL] validateResult=%d\n", valid);

    char buf[512];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"calib_result\",\"ok\":true,\"seq\":%d,\"rest_rms\":%.2f,\"max_rms\":%.2f,\"rest_mdf\":%.1f,\"max_mdf\":%.1f,\"peak_rms\":%.2f,\"valid\":%d}", seq, calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf, calib.peak_rms, valid ? 1 : 0);

    LOG("[CTRL] sendJsonTo start, buflen=%d\n", (int)strlen(buf));
    _netMgr->sendJsonTo(clientNum, buf);
    LOG("[CTRL] sendJsonTo done\n");
}

void AppController::handleSaveCalib(uint8_t clientNum, int seq)
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
        _netMgr->stopStreaming(); 
        _stateMgr->transitionTo(ST_IDLE);
        return;
    }

    const CalibData_t& calib = _calibMgr->getData();
    PersonalCalibData_t pcData = {0};
    pcData.rest_rms_mv = calib.rest_rms;
    pcData.max_rms_mv = calib.ref_rms;
    pcData.rest_mdf_hz = calib.rest_mdf;
    pcData.max_mdf_hz = calib.ref_mdf;
    pcData.peak_rms_mv = calib.peak_rms;
    pcData.calib_timestamp = millis() / 1000;
    pcData.calib_quality = 85;
    _storageMgr->UpdatePersonalCalib(&pcData);

    PersonalCalibData_t latest;
    _storageMgr->GetPersonalCalib(&latest);
    _signalProc->setCalibration(calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf, calib.peak_rms, latest.has_curve, latest.curve_coef);

    // 进入监控模式，显式开启流
    _netMgr->startStreaming(); 
    _stateMgr->transitionTo(ST_MONITORING);

    char buf[80];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"calib_saved\",\"ok\":true,\"seq\":%d,\"has_curve\":%d}", seq, latest.has_curve);
    _netMgr->sendJsonTo(clientNum, buf);
    LOG("[CTRL] Calib saved and entered MONITORING: RMS(%.2f/%.2f/peak=%.2f) MDF(%.1f/%.1f) has_curve=%d\n", calib.rest_rms, calib.ref_rms, calib.peak_rms, calib.rest_mdf, calib.ref_mdf, latest.has_curve);
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
