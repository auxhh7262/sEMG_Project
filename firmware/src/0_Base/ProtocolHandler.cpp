#include "ProtocolHandler.h"
#include "Logger.h"
#include "Board.h"
#include <ArduinoJson.h>
#include <Arduino.h>  // for NVIC_SystemReset

// 引入完整类定义（而非前向声明）
#include "5_AppController/AppController.h"
#include "3_Storage/StorageManager.h"
#include "4_Network/NetManager.h"
#include "1_Signal/SignalProcessor.h"
#include "0_Base/SystemStateMachine.h"

// 外部引用
extern AppController gAppController;
extern StorageManager gStorage;
extern StateManager gState;
extern NetManager gNetManager;
extern SignalProcessor gSignal;

void ProtocolHandler::init() {
    _debugBufIdx = 0;
    LOG("[PROTO] Init (Serial & BLE bridge).\n");
}

void ProtocolHandler::sendStatus(const char* stateName) {
    LOG("[PROTO] Status: %s\n", stateName);
}

void ProtocolHandler::sendStatus(const char* stateName, uint8_t progress) {
    LOG("[PROTO] Status: %s (%d%%)\n", stateName, progress);
}

void ProtocolHandler::sendCalResult(bool success, float restRms, float refRms, const char* errMsg) {
    if (success) {
        LOG("[PROTO] Calib SUCCESS: rest=%.2f, ref=%.2f\n", restRms, refRms);
    } else {
        LOG("[PROTO] Calib FAILED: %s\n", errMsg ? errMsg : "unknown");
    }
}

void ProtocolHandler::sendData(float rms, float fatigue) {
    /* 占位 */
}

void ProtocolHandler::sendError(const char* msg) {
    LOG("[PROTO] Error: %s\n", msg);
}

// [B1-3-fix] 去除 String 堆分配，直接用字符比较
// 单字符指令无需 String 构造和 trim()，直接比较首字符即可
AppCommand_t ProtocolHandler::tickLocalDebug() {
    if (!SERIAL_COMM.available()) return CMD_NONE;

    while (SERIAL_COMM.available() > 0) {
        char c = SERIAL_COMM.read();
        if (c == '\r' || c == '\n') {
            if (_debugBufIdx > 0) {
                _debugBuf[_debugBufIdx] = '\0';
                _debugBufIdx = 0;

                // 取首字符（忽略大小写），直接映射指令
                char cmd = _debugBuf[0];
                // 兼容大写
                if (cmd >= 'A' && cmd <= 'Z') cmd += 32;  // tolower

                switch (cmd) {
                    case 't': return CMD_SYNC_TIME;
                    case 'c': return CMD_START_CALIB;
                    case 's': return CMD_STOP;
                    case 'r': return CMD_RESET_CALIB;
                    case '?': return CMD_GET_STATUS;
                    case 'd': return CMD_START_RECORD;
                    case 'f': return CMD_FETCH_HISTORY;
                    case 'v': return CMD_VERIFY_CALIB;
                    case 'x': return CMD_DEBUG_SIGNAL;
                    case 'i': return CMD_INJECT_SIGNAL;
                    case 'p': return CMD_SIGNAL_DIAGNOSE;
                    case 'w': {
                        // --- WiFi 配置命令: w SSID password ---
                        // _debugBuf 内容: "w SSID password"
                        char* p = _debugBuf + 1;  // skip 'w'
                        while (*p == ' ') p++;   // skip spaces
                        char* ssidStart = p;
                        // find end of SSID (next space or end)
                        while (*p && *p != ' ') p++;
                        char* ssidEnd = p;
                        while (*p == ' ') p++;   // skip spaces before password
                        char* passStart = p;
                        // null-terminate SSID
                        *ssidEnd = '\0';
                        // build WifiCredentials_t
                        WifiCredentials_t creds;
                        memset(&creds, 0, sizeof(creds));
                        strncpy(creds.ssid, ssidStart, 31);
                        creds.ssid[31] = '\0';
                        strncpy(creds.pass, passStart, 63);
                        creds.pass[63] = '\0';
                        creds.isValid = true;
                        LOG("[PROTO] WiFi: SSID='%s', passlen=%d\n", creds.ssid, (int)strlen(creds.pass));
                        gStorage.SaveWifiCredentials(&creds);
                        LOG("[PROTO] WiFi creds saved! Rebooting in 1s...\n");
                        delay(1000);
                        NVIC_SystemReset();
                        break;
                    }
                    default:
                        LOG("[PROTO] Unknown cmd: [%c]\n", cmd);
                        break;
                }
            }
            return CMD_NONE;
        }

        if (_debugBufIdx < sizeof(_debugBuf) - 1) {
            _debugBuf[_debugBufIdx++] = c;
        }
    }
    return CMD_NONE;
}

void ProtocolHandler::tick() {
    /* 占位 */
}

// ==================== JSON 命令解析 ====================

void ProtocolHandler::handleJsonCommand(uint8_t clientNum, const char* json) {
    LOG("[PROTO] RX JSON: %s\n", json);
    
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, json);
    
    if (err) {
        LOG("[PROTO] JSON parse error: %s\n", err.c_str());
        return;
    }
    
    const char* cmd = doc["cmd"];
    if (!cmd) {
        LOG("[PROTO] No cmd field\n");
        return;
    }
    
    // [v3.9.14] 提取seq用于命令-响应匹配（兼容int和string类型）
    int seq = -1;
    if (doc["seq"].is<int>()) {
        seq = doc["seq"].as<int>();
    } else if (doc["seq"].is<const char*>()) {
        seq = atoi(doc["seq"].as<const char*>());
    }
    
    LOG("[PROTO] cmd='%s' seq=%d\n", cmd, seq);
    gNetManager.setAutoSeq(seq);
    
    // 查询 C 区
    if (strcmp(cmd, "query_cz") == 0) {
        uint32_t startTs = doc["start_ts"] | 0;
        uint32_t endTs = doc["end_ts"] | 0xFFFFFFFF;
        
        gAppController.handleQueryCZ(clientNum, startTs, endTs);
    }
    // 保存建库记录
    else if (strcmp(cmd, "save_record") == 0) {
        gAppController.handleSaveRecord(clientNum, doc.as<JsonObject>());
    }
    // 列出 B 区记录
    else if (strcmp(cmd, "list_records") == 0) {
        gAppController.handleListRecords(clientNum);
    }
    // 删除 B 区指定槽位
    else if (strcmp(cmd, "delete_record") == 0) {
        uint32_t slotAddr = doc["slot"] | 0;
        gAppController.handleDeleteRecord(clientNum, slotAddr);
    }
    // 查询匹配条件的群体曲线列表
    else if (strcmp(cmd, "list_curves") == 0) {
        uint8_t gender = doc["gender"] | 0;
        uint8_t handedness = doc["handedness"] | 0;
        uint8_t age = doc["age"] | 0;
        gAppController.handleListCurves(clientNum, gender, handedness, age);
    }
    // 保存个人曲线（小程序曲线拟合流程：群体曲线 + 当前校准基准 → A 区）
    else if (strcmp(cmd, "save_curve") == 0) {
        uint8_t curve_id = doc["curve_id"] | 0;
        // 从RAM缓存读取当前校准数据作为基准
        PersonalCalibData_t calib = {0};
        if (!gStorage.GetPersonalCalib(&calib)) {
            gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"curve_save_result\",\"ok\":false,\"err\":\"no_calib_data\"}");
        } else {
            float baseline_rms = calib.rest_rms_mv;
            float baseline_mdf = calib.rest_mdf_hz;
            bool ok = gStorage.GeneratePersonalCurve(curve_id, baseline_rms, baseline_mdf);
            if (ok) {
                // 设置 has_curve 标志并回写 Flash
                PersonalCalibData_t updated = {0};
                gStorage.GetPersonalCalib(&updated);   // 读取最新的（含曲线系数）
                updated.has_curve = 1;                  // [v3.9.12] 标记曲线已拟合
                gStorage.UpdatePersonalCalib(&updated);
                // [v3.9.12] 同步 SignalProcessor（激活个性化曲线算法）
                gSignal.setCalibration(updated.rest_rms_mv, updated.max_rms_mv,
                    updated.rest_mdf_hz, updated.max_mdf_hz, updated.peak_rms_mv,
                    updated.has_curve, updated.curve_coef);
                gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"curve_save_result\",\"ok\":true,\"curve_id\":1,\"has_curve\":1}");
            } else {
                gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"curve_save_result\",\"ok\":false,\"err\":\"gen_failed\"}");
            }
        }
    }
    // 生成个人曲线（群体曲线 + 个人校准基准 → A 区）
    else if (strcmp(cmd, "gen_personal_curve") == 0) {
        uint8_t bcurve_id = doc["bcurve_id"] | 0;
        float baseline_rms = doc["baseline_rms"] | 0.0f;
        float baseline_mdf = doc["baseline_mdf"] | 0.0f;
        gAppController.handleGenPersonalCurve(clientNum, bcurve_id, baseline_rms, baseline_mdf);
    }
    // 获取单条曲线详情
    else if (strcmp(cmd, "get_curve") == 0) {
        uint8_t curveId = doc["curve_id"] | 0;
        gAppController.handleGetCurve(clientNum, curveId);
    }
    // 【新增】建库特征采集开始
    else if (strcmp(cmd, "start_db_feature") == 0) {
        gAppController.handleStartDbFeature(clientNum, doc.as<JsonObject>());
    }
    // 【新增】原始波形阶段采集
    else if (strcmp(cmd, "capture_raw_phase") == 0) {
        gAppController.handleCaptureRawPhase(clientNum, doc.as<JsonObject>());
    }
    // 【新增】建库记录保存完成
    else if (strcmp(cmd, "raw_phase_done") == 0) {
        gAppController.handleRawPhaseDone(clientNum);
    }
    // 【新增】疲劳阶段标记（采集中即时标记）
    else if (strcmp(cmd, "db_mark") == 0) {
        gAppController.handleDbMark(clientNum, doc.as<JsonObject>());
    }
    // 保存校准（[v3.9.14] 改为从RAM读取，不再需要小程序传参）
    else if (strcmp(cmd, "save_calib") == 0) {
        gAppController.handleSaveCalib(clientNum, seq);
    }
    // [v3.9.14] 获取校准结果
    else if (strcmp(cmd, "get_calib_result") == 0) {
        LOG("[PROTO] about to call handleGetCalibResult client=%d seq=%d\n", clientNum, seq);
        Serial.println("PROTO_BEFORE_GCR");
        gAppController.handleGetCalibResult(clientNum, seq);
        Serial.println("PROTO_AFTER_GCR"); LOG("[PROTO] handleGetCalibResult returned\n");
    }
    // 启动校准（[v3.9.14] 支持 phase 参数：REST 或 MAX，并回传 seq）
    else if (strcmp(cmd, "start_calib") == 0) {
        const char* phase = doc["phase"] | "REST";
        if (strcmp(phase, "MAX") == 0) {
            gAppController.onCommandReceived(CMD_START_CALIB_MAX);
        } else {
            gAppController.onCommandReceived(CMD_START_CALIB);
        }
        // [v3.9.14] 回传 ack 响应含 seq
        if (seq >= 0) {
            char ack[64];
            snprintf(ack, sizeof(ack), "{\"cmd\":\"start_calib\",\"status\":\"ok\",\"seq\":%d,\"phase\":\"%s\"}", seq, phase);
            gNetManager.sendJsonTo(clientNum, ack);
        }
    }
    // 启动/恢复数据流（兼容旧命令：IDLE时启动校准，MONITORING时恢复流）
    else if (strcmp(cmd, "start") == 0) {
        SystemState_t st = gState.getState();
        if (st == ST_MONITORING) {
            // 已在监控中，发送确认（小程序onShow时重连后发start）
            gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"status\",\"state\":\"monitoring\"}");
        } else if (st == ST_IDLE) {
            // IDLE状态：启动校准流程（校准完成后自动进入MONITORING）
            gAppController.onCommandReceived(CMD_START_CALIB);
        } else if (st == ST_CALIB_DONE) {
            // [修复] CALIB_DONE：校准刚完成，直接保存并进入MONITORING
            gAppController.handleSaveCalib(clientNum, seq);
        } else {
            // 其他状态（CALIB_REST/MAX/DB_FEATURE等）：忽略，回复当前状态
            char resp[64];
            snprintf(resp, sizeof(resp), "{\"cmd\":\"status\",\"state\":\"%d\"}", (int)st);
            gNetManager.sendJsonTo(clientNum, resp);
        }
    }
    else if (strcmp(cmd, "stop") == 0) {
        gAppController.onCommandReceived(CMD_STOP);
    }
    // [v3.9.13] 保存用户信息到A区
    else if (strcmp(cmd, "save_user") == 0) {
        const char* userId = doc["user_id"] | "";
        const char* name = doc["name"] | "";
        uint8_t age = doc["age"] | 0;
        uint8_t gender = doc["gender"] | 0;        // 1=男, 2=女
        uint8_t handedness = doc["handedness"] | 0; // 1=左手, 2=右手
        uint8_t slot = doc["slot"] | 0;             // 槽位0或1

        if (gStorage.SaveUserProfile(slot, userId, name, age, gender, handedness)) {
            char buf[128];
            snprintf(buf, sizeof(buf), "{\"cmd\":\"user_saved\",\"ok\":true,\"slot\":%d}", slot);
            gNetManager.sendJsonTo(clientNum, buf);
        } else {
            gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"user_saved\",\"ok\":false,\"err\":\"save_failed\"}");
        }
    }
    // [v3.9.13] 加载用户信息
    else if (strcmp(cmd, "load_user") == 0) {
        uint8_t slot = doc["slot"] | 0;
        char userId[16], name[32];
        uint8_t age, gender, handedness;

        if (gStorage.LoadUserProfile(slot, userId, name, &age, &gender, &handedness)) {
            char buf[256];
            snprintf(buf, sizeof(buf),
                "{\"cmd\":\"user_loaded\",\"ok\":true,\"slot\":%d,\"user_id\":\"%s\",\"name\":\"%s\",\"age\":%d,\"gender\":%d,\"handedness\":%d}",
                slot, userId, name, age, gender, handedness);
            gNetManager.sendJsonTo(clientNum, buf);
        } else {
            gNetManager.sendJsonTo(clientNum, "{\"cmd\":\"user_loaded\",\"ok\":false,\"err\":\"not_found\"}");
        }
    }
    // [v3.9.14] 清除自动seq
    gNetManager.setAutoSeq(-1);
}
