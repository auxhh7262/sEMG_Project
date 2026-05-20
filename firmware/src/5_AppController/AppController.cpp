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
    _rawPhaseCount(0) {
    memset(_stageStarted, 0, sizeof(_stageStarted));
}

void AppController::init(void) {
    // [v3.9.12] 开机时从 A区 加载校准数据到 SignalProcessor（支持个性化曲线）
    PersonalCalibData_t calib = {0};
    if (_storageMgr->GetPersonalCalib(&calib) && calib.calib_timestamp > 0) {
        _signalProc->setCalibration(calib.rest_rms_mv, calib.max_rms_mv,
                                     calib.rest_mdf_hz, calib.max_mdf_hz,
                                     calib.peak_rms_mv,
                                     calib.has_curve, calib.curve_coef);
        LOG("[CTRL] Boot: loaded calib from A区 has_curve=%d\n", calib.has_curve);
    } else {
        LOG("[CTRL] Boot: no calib in A区\n");
    }
    LOG("[CTRL] AppController initialized.\n");
}

void AppController::tick(void) {
    switch (_stateMgr->getState()) {
        case ST_IDLE:
            _handleIdleState();
            break;
        case ST_CALIB_REST:
            _handleCalibRestState();
            break;
        case ST_CALIB_WAIT:
            // [v3.9.14] REST完成等待小程序触发MAX，空闲等待
            break;
        case ST_CALIB_MAX:
            _handleCalibMaxState();
            break;
        case ST_CALIB_DONE:
            // [v3.9.14] MAX完成等待小程序get_calib_result/save_calib，空闲等待
            break;
        case ST_MONITORING:
            _handleMonitoringState();
            break;
        case ST_DB_FEATURE:
            _handleDbFeatureState();
            break;
        case ST_ERROR:
        default:
            _handleErrorState();
            break;
    }
}

void AppController::onCommandReceived(AppCommand_t cmd) {
    switch (cmd) {
        case CMD_START_CALIB: {
            // [修复] ST_MONITORING 状态下忽略 CMD_START_CALIB，避免 realtime 页面重新触发校准
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
                // [FIX] 必须先 transitionTo 再 startCalibPhase！
                // transitionTo() 会重置 _phaseActive=false，
                // 如果先调 startCalibPhase 再调 transitionTo，_phaseActive 会被清零，
                // 导致 isCalibPhaseComplete() 永远返回 false，校准永远卡在 REST。
                _stateMgr->transitionTo(ST_CALIB_REST);
                _stateMgr->startCalibPhase(CALIB_REST_SEC);
                LOG("[CTRL] Calibration started: REST phase %ds\n", CALIB_REST_SEC);
            }
            break;
        }
        case CMD_START_CALIB_MAX: {
            // [v3.9.14] 小程序显式触发MAX阶段
            SystemState_t st = _stateMgr->getState();
            if (st == ST_CALIB_WAIT) {
                // [FIX-v3.9.6] 切换到 MAX 阶段时重置 EMA
                // REST→MAX 频谱形态巨变，EMA 残值导致 MDF 锁死
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
            // [A0-3-fix] STOP可以终止校准或监控，回到IDLE
            SystemState_t st = _stateMgr->getState();
            if (st == ST_CALIB_REST || st == ST_CALIB_WAIT || st == ST_CALIB_MAX || st == ST_CALIB_DONE || st == ST_MONITORING || st == ST_ERROR) {
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
            _stateMgr->transitionTo(ST_IDLE);
            LOG("[CTRL] Calibration reset\n");
            break;
        }
        case CMD_GET_CALIB_RESULT: {
            // [v3.9.14] 通过 handleGetCalibResult 发送校准数据
            // clientNum 需要从 ProtocolHandler 传入，但 onCommandReceived 不接受 clientNum
            // 因此改由 ProtocolHandler 直接调用 handleGetCalibResult
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
            // [A0-3-fix] 验证当前校准数据
            if (_calibMgr->validateResult()) {
                const CalibData_t& calib = _calibMgr->getData();
                LOG("[CTRL] Verify OK: RMS(%.2f/%.2f) MDF(%.1f/%.1f)\n",
                     calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf);
            } else {
                LOG("[CTRL] Verify FAIL: invalid calibration data\n");
            }
            break;
        }
        default: {
            break;
        }
    }
}

void AppController::_handleIdleState(void) {
    // 空闲状态：等待指令
}

void AppController::_handleCalibRestState(void) {
    // [A0-1-fix] 校准静息阶段：每个10Hz tick采集一组RMS+MDF样本
    float rms = _signalProc->update();
    // [REVERT] 校准用平滑 MDF（更稳定，避免 raw 值噪声导致平均值偏低）
    float mdf = _signalProc->getMDF();
    _calibMgr->addSample(rms, mdf);

    // [DEBUG] 打印 RMS 和 MDF 值
    static uint32_t debug_counter = 0;
    if (debug_counter++ % 10 == 0) {
        LOG("[CTRL] CALIB_REST: rms=%.3f, mdf=%.1f\n", rms, mdf);
    }

    // 推送实时数据到小程序（让用户看到校准进度）
    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, 0.0f, 0, 0.0f, true);  // 校准模式：无f/q/a
    }

    // 检查静息阶段是否结束
    if (_stateMgr->isCalibPhaseComplete()) {
        _calibMgr->endPhase(true);  // isRestPhase = true
        LOG("[CTRL] Calib REST phase done. Waiting for app to trigger MAX...\n");
        // [v3.9.14] 不再自动进入MAX，切换到CALIB_WAIT等待小程序触发
        _stateMgr->transitionTo(ST_CALIB_WAIT);
    }
}

void AppController::_handleCalibMaxState(void) {
    // [A0-1-fix] 校准最大收缩阶段：每个10Hz tick采集一组RMS+MDF样本
    float rms = _signalProc->update();
    // [REVERT] 校准用平滑 MDF（更稳定，避免 raw 值噪声导致平均值偏低）
    float mdf = _signalProc->getMDF();
    _calibMgr->addSample(rms, mdf);

    // [DEBUG] 每10个tick打印一次MAX阶段状态
    static uint32_t max_debug_counter = 0;
    if (max_debug_counter++ % 10 == 0) {
        LOG("[CTRL] CALIB_MAX: rms=%.3f, mdf=%.1f, progress=%d%%\n",
            rms, mdf, _stateMgr->getCalibProgress());
    }

    // 推送实时数据到小程序（让用户看到校准进度）
    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, 0.0f, 0, 0.0f, true);  // 校准模式：无f/q/a
    }

    // 检查最大收缩阶段是否结束
    if (_stateMgr->isCalibPhaseComplete()) {
        _calibMgr->endPhase(false);  // isRestPhase = false

        // [v3.9.14] 不再自动validate+进入MONITORING，切换到CALIB_DONE等待小程序操作
        // 小程序会发 get_calib_result 获取结果，再发 save_calib 保存
        _stateMgr->transitionTo(ST_CALIB_DONE);
        LOG("[CTRL] Calib MAX phase done. Waiting for app to get result & save...\n");
    }
}

void AppController::_handleMonitoringState(void) {
    // [A0-2-fix] Monitoring: 10Hz tick调用update()，但FFT需要256样本(256ms)
    // SignalProcessor::update() 内部已有 availableSamples < fftWindowSize 的保护，
    // 数据不足时返回 0.0f，不会重复计算。自然约每3个tick完成一次有效FFT。
    float rms = _signalProc->update();
    float mdf = _signalProc->getMDF();
    float fatigue = _signalProc->getFatigue();
    float activation = _signalProc->getActivation();
    uint8_t quality = (uint8_t)_signalProc->getSignalQuality();

    // 仅在有有效数据时处理（RMS=0表示数据不足，跳过）
    if (rms > 0.0f) {
        // 发送实时数据到小程序
        _netMgr->sendData(rms, mdf, fatigue, quality, activation);
        
        // [修复] 写入 C 区存储（历史数据分析用）
        CZone_DataPoint_t pt;
        memset(&pt, 0, sizeof(pt));  // 清零所有字段
        pt.timestamp_ms = millis();  // 毫秒时间戳
        pt.rms_compressed = (uint16_t)(rms * 100.0f);   // mV -> 0.01mV
        pt.mdf_compressed = (uint16_t)(mdf * 10.0f);    // Hz -> 0.1Hz
        pt.fatigue_level = (uint8_t)(fatigue * 2.55f);  // 0-100 -> 0-255
        pt.signal_quality = quality;
        _storageMgr->CZone_AppendDataPoint(&pt);
    }
}

void AppController::_handleErrorState(void) {
    // [A0-3-fix] 错误状态下处理 STOP 指令回到 IDLE
    // （onCommandReceived 已处理 CMD_STOP → transitionTo ST_IDLE）
}

void AppController::_handleDbFeatureState(void) {
    // 【修复】仅在当前阶段已启动时，才采集原始sEMG数据
    static uint32_t lastSampleTime = 0;
    if (_stageStarted[_currentStage] && millis() - lastSampleTime >= 10 && _rawPhaseBuf && _rawPhaseCount < 3000) {
        int16_t rawSample = analogRead(A0);  // sEMG传感器接A0
        _rawPhaseBuf[_rawPhaseCount++] = rawSample;
        lastSampleTime = millis();
    }

    // 建库特征采集状态：10Hz向B区写入特征序列，同时发送实时数据到小程序
    float rms = _signalProc->update();
    float mdf = _signalProc->getMDF();
    float fatigue = _signalProc->getFatigue();
    float activation = _signalProc->getActivation();
    uint8_t quality = (uint8_t)_signalProc->getSignalQuality();

    // 写特征点到B区
    if (rms > 0.0f) {
        uint16_t rmsCompressed = (uint16_t)(rms * 100.0f);
        uint16_t mdfCompressed = (uint16_t)(mdf * 10.0f);
        _storageMgr->BZone_AppendFeaturePoint(rmsCompressed, mdfCompressed);
    }

    // 同时发送实时数据到小程序（保持连接活跃）
    if (rms > 0.0f) {
        _netMgr->sendData(rms, mdf, fatigue, quality, activation);
    }
}

// ==================== JSON 命令处理实现 ====================

void AppController::handleQueryCZ(uint8_t clientNum, uint32_t startTs, uint32_t endTs) {
    CZone_DataPoint_t points[100];
    uint16_t count = 0;
    uint32_t nextTs = 0;
    
    bool ok = _storageMgr->CZone_QueryByTimeRange(startTs, endTs, points, 100, &count, &nextTs);
    
    if (!ok || count == 0) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"cz_data\",\"points\":[]}");
        return;
    }
    
    // 分批发送，每批最多 20 个点（避免 JSON 过长）
    for (uint16_t batch = 0; batch < count; batch += 20) {
        StaticJsonDocument<1024> doc;
        doc["cmd"] = "cz_data";
        JsonArray pts = doc.createNestedArray("points");
        
        uint16_t batchEnd = (batch + 20 < count) ? batch + 20 : count;
        for (uint16_t i = batch; i < batchEnd; i++) {
            JsonObject pt = pts.createNestedObject();
            pt["ts"] = points[i].timestamp_ms;
            pt["rms"] = points[i].rms_compressed / 100.0f;  // 解压缩
            pt["mdf"] = points[i].mdf_compressed / 10.0f;
            pt["f"] = points[i].fatigue_level / 255.0f;
            pt["q"] = points[i].signal_quality;
        }
        
        if (batch + 20 < count) {
            doc["more"] = true;
            doc["next_ts"] = nextTs;  // [P1-fix] 翻页游标
        }
        
        char buf[1024];
        serializeJson(doc, buf, sizeof(buf));
        gNetManager.sendJsonTo(clientNum, buf);
    }
}

void AppController::handleSaveRecord(uint8_t clientNum, JsonObject doc) {
    // 解析 JSON 字段
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
    
    // 获取特征序列
    JsonArray rmsArr = doc["rms_sequence"];
    JsonArray mdfArr = doc["mdf_sequence"];
    uint16_t seqLen = rmsArr.size();
    
    if (seqLen == 0 || seqLen != mdfArr.size()) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"save_result\",\"ok\":false,\"err\":\"invalid_sequence\"}");
        return;
    }
    
    // 查找可用槽位
    uint32_t slotAddr;
    if (!_storageMgr->BZone_GetNextAvailableSlot(&slotAddr)) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"save_result\",\"ok\":false,\"err\":\"b_zone_full\"}");
        return;
    }
    
    // 开始写入
    SubjectBasicInfo_t subject = { subjectId, age, gender, handedness };
    if (!_storageMgr->BZone_BeginRecord(&subject, slotAddr)) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"save_result\",\"ok\":false,\"err\":\"begin_failed\"}");
        return;
    }
    
    // 压缩写入特征序列（每值 uint16_t）
    // [FIX] 使用栈上小批量缓冲分批写入，避免堆分配碎片化
    // 每批256点 = 1KB（uint16_t×2），3批处理3000点
    const uint16_t BATCH_SIZE = 256;
    uint16_t rmsBuf[BATCH_SIZE];
    uint16_t mdfBuf[BATCH_SIZE];
    
    bool writeOk = true;
    for (uint16_t batch = 0; batch < seqLen && writeOk; batch += BATCH_SIZE) {
        uint16_t batchLen = (seqLen - batch > BATCH_SIZE) ? BATCH_SIZE : (seqLen - batch);
        for (uint16_t i = 0; i < batchLen; i++) {
            rmsBuf[i] = (uint16_t)(rmsArr[batch + i].as<float>() * 100);   // mV -> 0.01mV
            mdfBuf[i] = (uint16_t)(mdfArr[batch + i].as<float>() * 10);   // Hz -> 0.1Hz
        }
        if (!_storageMgr->BZone_AppendFeatureSequence(rmsBuf, mdfBuf, batchLen)) {
            writeOk = false;
        }
    }
    
    if (!writeOk) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"save_result\",\"ok\":false,\"err\":\"write_failed\"}");
        return;
    }
    
    // 结束写入
    _storageMgr->BZone_EndRecord();
    
    // 响应成功
    char resp[128];
    snprintf(resp, sizeof(resp), "{\"cmd\":\"save_result\",\"ok\":true,\"slot\":%lu}", (unsigned long)slotAddr);
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleListRecords(uint8_t clientNum) {
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

void AppController::handleDeleteRecord(uint8_t clientNum, uint32_t slotAddr) {
    bool ok = _storageMgr->BZone_DeleteSlot(slotAddr);
    
    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"delete_result\",\"ok\":true,\"slot\":%lu}", (unsigned long)slotAddr);
    } else {
        snprintf(resp, sizeof(resp), "{\"cmd\":\"delete_result\",\"ok\":false,\"err\":\"invalid_slot\",\"slot\":%lu}", (unsigned long)slotAddr);
    }
    gNetManager.sendJsonTo(clientNum, resp);
}

// ==================== B区曲线库命令处理 ====================

void AppController::handleListCurves(uint8_t clientNum,
                                     uint8_t gender, uint8_t handedness, uint8_t age) {
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

void AppController::handleGenPersonalCurve(uint8_t clientNum, uint8_t bcurveId,
                                          float baselineRms, float baselineMdf) {
    bool ok = _storageMgr->GeneratePersonalCurve(bcurveId, baselineRms, baselineMdf);

    char resp[256];
    if (ok) {
        snprintf(resp, sizeof(resp),
            "{\"cmd\":\"curve_gen_result\",\"ok\":true,\"curve_id\":%d,\"baseline_rms\":%.2f,\"baseline_mdf\":%.2f}",
            bcurveId, baselineRms, baselineMdf);
    } else {
        snprintf(resp, sizeof(resp),
            "{\"cmd\":\"curve_gen_result\",\"ok\":false,\"err\":\"generation_failed\"}");
    }
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleGetCurve(uint8_t clientNum, uint8_t curveId) {
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

// ==================== 建库命令处理实现 ====================

void AppController::handleStartDbFeature(uint8_t clientNum, JsonObject doc) {
    // 解析被试信息
    uint32_t subjectId = doc["subject_id"] | (millis() / 1000);
    uint8_t age = doc["age"] | 25;
    uint8_t gender = doc["gender"] | 1;
    uint8_t handedness = doc["handedness"] | 2;

    // 查找可用槽位
    uint32_t slotAddr;
    if (!_storageMgr->BZone_GetNextAvailableSlot(&slotAddr)) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_feature_started\",\"ok\":false,\"err\":\"b_zone_full\"}");
        return;
    }

    // 写入B区header（SubjectBasicInfo + 校准快照）
    SubjectBasicInfo_t subject = { subjectId, age, gender, handedness };
    if (!_storageMgr->BZone_BeginRecord(&subject, slotAddr)) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_feature_started\",\"ok\":false,\"err\":\"begin_failed\"}");
        return;
    }

    // 切换到建库采集状态（独立于常规监控）
    _stateMgr->transitionTo(ST_DB_FEATURE);

    // 【P0-Fix】动态分配波形缓冲区（仅在采集期间占用6KB，完成后立即释放）
    _rawPhaseBuf = (int16_t*)malloc(3000 * sizeof(int16_t));
    if (!_rawPhaseBuf) {
        _stateMgr->transitionTo(ST_IDLE);
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_feature_started\",\"ok\":false,\"err\":\"malloc_failed\"}");
        return;
    }

    LOG("[CTRL] DB Feature started at slot 0x%06X, subject=%lu, age=%d, gender=%d, handedness=%d\n",
        slotAddr, (unsigned long)subjectId, age, gender, handedness);

    char resp[128];
    snprintf(resp, sizeof(resp),
        "{\"cmd\":\"db_feature_started\",\"ok\":true,\"slot\":%lu}",
        (unsigned long)slotAddr);
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleCaptureRawPhase(uint8_t clientNum, JsonObject doc) {
    // 仅在 ST_DB_FEATURE 状态下允许
    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"raw_phase_done\",\"ok\":false,\"err\":\"not_in_db_feature\"}");
        return;
    }

    uint8_t phaseIndex = doc["phase"] | 1;  // 1~4
    uint16_t samplesPerPhase = doc["samples"] | 3000;
    JsonArray rawArr = doc["raw"].as<JsonArray>();

    if (rawArr.isNull() || rawArr.size() == 0) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"raw_phase_done\",\"ok\":false,\"err\":\"empty_raw\"}");
        return;
    }

    // 【P0-Fix】使用动态分配的 _rawPhaseBuf（handleStartDbFeature 中分配，handleRawPhaseDone 中释放）
    if (!_rawPhaseBuf) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"raw_phase_done\",\"ok\":false,\"err\":\"buf_not_init\"}");
        return;
    }
    uint16_t count = rawArr.size();
    if (count > 3000) count = 3000;

    // 将 JSON 数组复制到 _rawPhaseBuf
    for (uint16_t i = 0; i < count; i++) {
        _rawPhaseBuf[i] = (int16_t)rawArr[i].as<int>();
    }

    // 写入对应phase数据到B区
    bool ok = _storageMgr->BZone_AppendRawPhase(phaseIndex, _rawPhaseBuf, count);

    LOG("[CTRL] DB: capture_raw_phase phase=%d, samples=%d, ok=%s\n",
        phaseIndex, count, ok ? "YES" : "NO");

    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp),
            "{\"cmd\":\"raw_phase_done\",\"ok\":true,\"phase\":%d,\"samples\":%d}",
            phaseIndex, count);
    } else {
        snprintf(resp, sizeof(resp),
            "{\"cmd\":\"raw_phase_done\",\"ok\":false,\"err\":\"write_failed\",\"phase\":%d}",
            phaseIndex);
    }
    gNetManager.sendJsonTo(clientNum, resp);
}

void AppController::handleRawPhaseDone(uint8_t clientNum) {
    // 仅在 ST_DB_FEATURE 状态下允许
    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_record_saved\",\"ok\":false,\"err\":\"not_in_db_feature\"}");
        return;
    }

    // 【修复】确认各阶段原始数据是否已保存
    for (uint8_t i = 0; i < 4; i++) {
        if (_stageDataSaved[i]) {
            LOG("[CTRL] DB: stage %d raw data confirmed saved\n", i);
        } else {
            LOG("[CTRL] DB: WARNING - stage %d raw data NOT saved!\n", i);
        }
    }

    // 结束B区记录写入
    bool ok = _storageMgr->BZone_EndRecord();

    // 恢复到IDLE状态
    _stateMgr->transitionTo(ST_IDLE);

    // 【P0-Fix】释放波形缓冲区（malloc(nullptr) 是安全的）
    free(_rawPhaseBuf);
    _rawPhaseBuf = nullptr;

    // 【修复】重置阶段管理变量
    _currentStage = 0;
    memset(_stageDataSaved, 0, sizeof(_stageDataSaved));
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

// ==================== [v3.9.14] 校准结果命令 ====================

void AppController::handleGetCalibResult(uint8_t clientNum, int seq) {
    const CalibData_t& calib = _calibMgr->getData();
    bool valid = _calibMgr->validateResult();

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"cmd\":\"calib_result\",\"ok\":true,\"seq\":%d,\"rest_rms\":%.2f,\"max_rms\":%.2f,\"rest_mdf\":%.1f,\"max_mdf\":%.1f,\"peak_rms\":%.2f,\"valid\":%d}",
        seq, calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf, calib.peak_rms, valid ? 1 : 0);
    _netMgr->sendJsonTo(clientNum, buf);
    LOG("[CTRL] Sent calib_result: RMS(%.2f/%.2f) MDF(%.1f/%.1f) valid=%d\n",
        calib.rest_rms, calib.ref_rms, calib.rest_mdf, calib.ref_mdf, valid);
}

void AppController::handleSaveCalib(uint8_t clientNum, int seq) {
    // 仅在 ST_CALIB_DONE 状态下允许保存
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
        _stateMgr->transitionTo(ST_IDLE);
        return;
    }

    const CalibData_t& calib = _calibMgr->getData();

    // 保存到A区
    PersonalCalibData_t pcData = {0};
    pcData.rest_rms_mv = calib.rest_rms;
    pcData.max_rms_mv = calib.ref_rms;
    pcData.rest_mdf_hz = calib.rest_mdf;
    pcData.max_mdf_hz = calib.ref_mdf;
    pcData.peak_rms_mv = calib.peak_rms;
    pcData.calib_timestamp = millis() / 1000;
    pcData.calib_quality = 85;
    _storageMgr->UpdatePersonalCalib(&pcData);

    // 同步到SignalProcessor（含 has_curve 和曲线系数）
    PersonalCalibData_t latest;
    _storageMgr->GetPersonalCalib(&latest);
    _signalProc->setCalibration(calib.rest_rms, calib.ref_rms,
                                 calib.rest_mdf, calib.ref_mdf,
                                 calib.peak_rms,
                                 latest.has_curve, latest.curve_coef);

    // 进入MONITORING
    _stateMgr->transitionTo(ST_MONITORING);

    char buf[80];
    snprintf(buf, sizeof(buf), "{\"cmd\":\"calib_saved\",\"ok\":true,\"seq\":%d,\"has_curve\":%d}", seq, latest.has_curve);
    _netMgr->sendJsonTo(clientNum, buf);
    LOG("[CTRL] Calib saved and entered MONITORING: RMS(%.2f/%.2f/peak=%.2f) MDF(%.1f/%.1f) has_curve=%d\n",
         calib.rest_rms, calib.ref_rms, calib.peak_rms, calib.rest_mdf, calib.ref_mdf, latest.has_curve);
}

void AppController::handleDbMark(uint8_t clientNum, JsonObject doc) {
    // 仅在 ST_DB_FEATURE 状态下允许
    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_marked\",\"ok\":false,\"err\":\"not_in_db_feature\"}");
        return;
    }

    uint8_t markIndex = doc["stage"] | 0;  // 0~3
    if (markIndex >= 4) {
        gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"db_marked\",\"ok\":false,\"err\":\"invalid_stage\"}");
        return;
    }

    // 【修复】标记前，先保存当前阶段剩余的原始数据到Flash
    if (_rawPhaseBuf && _rawPhaseCount > 0) {
        if (_storageMgr->BZone_AppendRawPhase(_currentStage + 1, _rawPhaseBuf, _rawPhaseCount)) {
            _stageDataSaved[_currentStage] = true;
            LOG("[CTRL] DB: stage %d raw data flushed (%d samples)\n", _currentStage, _rawPhaseCount);
        }
        _rawPhaseCount = 0;  // 重置计数器，准备下一阶段
        memset(_rawPhaseBuf, 0, 3000 * sizeof(int16_t));  // 清空缓冲区
    }

    uint16_t featureIdx = _storageMgr->BZone_GetFeatureCount();
    bool ok = _storageMgr->BZone_MarkFeaturePoint(markIndex);

    LOG("[CTRL] DB: db_mark stage=%d, featureIdx=%d, ok=%s\n",
        markIndex, featureIdx, ok ? "YES" : "NO");

    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp),
            "{\"cmd\":\"db_marked\",\"ok\":true,\"stage\":%d,\"feature_idx\":%d}",
            markIndex, featureIdx);
    } else {
        snprintf(resp, sizeof(resp),
            "{\"cmd\":\"db_marked\",\"ok\":false,\"err\":\"mark_failed\",\"stage\":%d}",
            markIndex);
    }
    gNetManager.sendJsonTo(clientNum, resp);

    // 【修复】切换到下一阶段
    _currentStage = markIndex + 1;
    if (_currentStage >= 4) {
        LOG("[CTRL] DB: all 4 stages marked, ready to save\n");
    }
}