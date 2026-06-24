#include "ProtocolHandler.h"
#include "Logger.h"
#include "Board.h"
#include <ArduinoJson.h>
#include <Arduino.h>
#include "5_AppController/AppController.h"
#include "3_Storage/StorageManager.h"
#include "3_Storage/FlashDriver.h"
#include "4_Network/NetManager.h"
#include "1_Signal/SignalProcessor.h"
#include "0_Base/SystemStateMachine.h"

extern AppController gAppController;
extern StorageManager gStorage;
extern StateManager gState;
extern NetManager gNetManager;
extern SignalProcessor gSignal;

void ProtocolHandler::init() { _debugBufIdx = 0; LOG("[PROTO] Init (Serial & BLE bridge).\n"); }
void ProtocolHandler::sendStatus(const char* stateName) { LOG("[PROTO] Status: %s\n", stateName); }
void ProtocolHandler::sendStatus(const char* stateName, uint8_t progress) { LOG("[PROTO] Status: %s (%d%%)\n", stateName, progress); }
void ProtocolHandler::sendCalResult(bool success, float restRms, float refRms, const char* errMsg) { success ? LOG("[PROTO] Calib SUCCESS: rest=%.2f, ref=%.2f\n", restRms, refRms) : LOG("[PROTO] Calib FAILED: %s\n", errMsg ? errMsg : "unknown"); }
void ProtocolHandler::sendData(float rms, float fatigue) { /* 占位 */ }
void ProtocolHandler::sendError(const char* msg) { LOG("[PROTO] Error: %s\n", msg); }

AppCommand_t ProtocolHandler::tickLocalDebug() {
    if (!SERIAL_COMM.available()) return CMD_NONE;
    while (SERIAL_COMM.available() > 0) {
        char c = SERIAL_COMM.read();
        if (c == '\r' || c == '\n') {
            if (_debugBufIdx > 0) {
                _debugBuf[_debugBufIdx] = '\0'; _debugBufIdx = 0;
                char cmd = _debugBuf[0]; if (cmd >= 'A' && cmd <= 'Z') cmd += 32;
                switch (cmd) {
                    case 't': return CMD_SYNC_TIME; case 'c': return CMD_START_CALIB; case 's': return CMD_STOP;
                    case 'r': return CMD_RESET_CALIB; case '?': return CMD_GET_STATUS; case 'd': return CMD_START_RECORD;
                    case 'f': return CMD_FETCH_HISTORY; case 'v': return CMD_VERIFY_CALIB; case 'x': return CMD_DEBUG_SIGNAL;
                    case 'i': return CMD_INJECT_SIGNAL; case 'p': return CMD_SIGNAL_DIAGNOSE;
                    case 'w': {
                        char* p = _debugBuf + 1; while (*p == ' ') p++; char* ssidStart = p;
                        while (*p && *p != ' ') p++; char* ssidEnd = p; while (*p == ' ') p++; char* passStart = p; *ssidEnd = '\0';
                        WifiCredentials_t creds; memset(&creds, 0, sizeof(creds));
                        strncpy(creds.ssid, ssidStart, 31); creds.ssid[31] = '\0'; strncpy(creds.pass, passStart, 63); creds.pass[63] = '\0'; creds.isValid = true;
                        gStorage.SaveWifiCredentials(&creds); delay(1000); NVIC_SystemReset(); break;
                    }
                    default: LOG("[PROTO] Unknown cmd: [%c]\n", cmd); break;
                }
            }
            return CMD_NONE;
        }
        if (_debugBufIdx < sizeof(_debugBuf) - 1) _debugBuf[_debugBufIdx++] = c;
    }
    return CMD_NONE;
}

void ProtocolHandler::tick() { /* 占位 */ }

void ProtocolHandler::handleJsonCommand(uint8_t clientNum, const char* json) {
    LOG("[PROTO] RX JSON: %s\n", json);
    // static allocation on BSS to avoid stack overflow on RA4M1 (~2KB main stack)
    static StaticJsonDocument<512> doc;
    doc.clear();  // clear residual state from previous parse
    LOG("[PROTO] doc allocated (static BSS), attempting deserialize...\n");
    DeserializationError err = deserializeJson(doc, json);
    LOG("[PROTO] deserializeJson returned, err=%s\n", err ? err.c_str() : "OK");
    if (err) { LOG("[PROTO] JSON parse error: %s\n", err.c_str()); return; }

    const char* cmd = doc["cmd"];
    LOG("[PROTO] cmd field accessed\n");
    if (!cmd) { LOG("[PROTO] No cmd field\n"); return; }
    
    int seq = -1;
    if (doc["seq"].is<int>()) { seq = doc["seq"].as<int>(); LOG("[PROTO] seq is int: %d\n", seq); }
    else if (doc["seq"].is<const char*>()) { seq = atoi(doc["seq"].as<const char*>()); LOG("[PROTO] seq is string: %d\n", seq); }
    else { LOG("[PROTO] seq not found, default -1\n"); }
    LOG("[PROTO] cmd='%s' seq=%d\n", cmd, seq);

    if (strcmp(cmd, "start_stream") == 0) {
        // 方案2：先发ACK，再执行逻辑（避免小程序超时）
        if (seq >= 0) { char ack[128]; snprintf(ack, sizeof(ack), "{\"cmd\":\"start_stream\",\"status\":\"ok\",\"seq\":%d}", seq); gNetManager.sendJsonTo(clientNum, ack); }
        SystemState_t st = gState.getState();
        if (st == ST_IDLE || st == ST_CALIB_DONE) gAppController.onCommandReceived(CMD_START_STREAM, clientNum);  // [FIX] Pass clientNum
    } 
    else if (strcmp(cmd, "stop") == 0) {
        // 方案2：先发ACK，再执行逻辑（避免小程序超时）
        if (seq >= 0) { char ack[64]; snprintf(ack, sizeof(ack), "{\"cmd\":\"stop\",\"status\":\"ok\",\"seq\":%d}", seq); gNetManager.sendJsonTo(clientNum, ack); }
        gAppController.onCommandReceived(CMD_STOP, clientNum);  // [FIX] Pass clientNum
    } 
    else if (strcmp(cmd, "query_cz") == 0) {
        uint32_t startTs = doc["start_ts"] | 0; uint32_t endTs = doc["end_ts"] | 0xFFFFFFFF;
        // [FIX] Defer to tick() to avoid stack overflow in WebSocket callback
        gAppController.deferQueryCZ(clientNum, startTs, endTs);
    }
    else if (strcmp(cmd, "load_user") == 0) {
        uint8_t slot = doc["slot"] | 0; char userId[16] = {0}; char name[32] = {0}; uint8_t age = 0, gender = 0, handedness = 0;
        if (gStorage.LoadUserProfile(slot, userId, name, &age, &gender, &handedness)) {
            userId[sizeof(userId)-1] = '\0'; name[sizeof(name)-1] = '\0'; char buf[256];
            if (seq >= 0) snprintf(buf, sizeof(buf), "{\"cmd\":\"user_loaded\",\"ok\":true,\"slot\":%d,\"user_id\":\"%s\",\"name\":\"%s\",\"age\":%d,\"gender\":%d,\"handedness\":%d,\"seq\":%d}", slot, userId, name, age, gender, handedness, seq);
            else snprintf(buf, sizeof(buf), "{\"cmd\":\"user_loaded\",\"ok\":true,\"slot\":%d,\"user_id\":\"%s\",\"name\":\"%s\",\"age\":%d,\"gender\":%d,\"handedness\":%d}", slot, userId, name, age, gender, handedness);
            gNetManager.sendJsonTo(clientNum, buf);
        } else {
            char buf[128];
            if (seq >= 0) snprintf(buf, sizeof(buf), "{\"cmd\":\"user_loaded\",\"ok\":false,\"has_curve\":false,\"err\":\"slot_empty\",\"seq\":%d}", seq);
            else snprintf(buf, sizeof(buf), "{\"cmd\":\"user_loaded\",\"ok\":false,\"has_curve\":false,\"err\":\"slot_empty\"}");
            gNetManager.sendJsonTo(clientNum, buf);
        }
    } 
    else if (strcmp(cmd, "save_calib") == 0) {
        uint8_t slot = doc["slot"] | 0;
        gAppController.handleSaveCalib(clientNum, seq, slot);
    } 
    else if (strcmp(cmd, "get_calib_result") == 0) {
        uint8_t slot = doc["slot"] | 0;
        LOG("[PROTO] get_calib_result: slot=%d\n", slot);
        gAppController.handleGetCalibResult(clientNum, seq, slot);
    } 
    else if (strcmp(cmd, "start_calib") == 0) {
        LOG("[PROTO] start_calib branch reached\n");
        const char* phase = doc["phase"] | "REST";
        LOG("[PROTO] phase extracted: '%s'\n", phase);
        // 方案2：先发ACK，再执行逻辑（修复MAX阶段超时的核心）
        if (seq >= 0) { char ack[64]; snprintf(ack, sizeof(ack), "{\"cmd\":\"start_calib\",\"status\":\"ok\",\"seq\":%d,\"phase\":\"%s\"}", seq, phase); gNetManager.sendJsonTo(clientNum, ack); LOG("[PROTO] ACK sent\n"); }
        LOG("[PROTO] about to call onCommandReceived, phase=%s\n", phase);
        if (strcmp(phase, "MAX") == 0) { LOG("[PROTO] calling CMD_START_CALIB_MAX\n"); gAppController.onCommandReceived(CMD_START_CALIB_MAX); }
        else { LOG("[PROTO] calling CMD_START_CALIB\n"); gAppController.onCommandReceived(CMD_START_CALIB); }
        LOG("[PROTO] onCommandReturned\n");
    } 
    else if (strcmp(cmd, "start_db_feature") == 0) { gAppController.handleStartDbFeature(clientNum, doc.as<JsonObject>()); if (seq >= 0) { char ack[128]; snprintf(ack, sizeof(ack), "{\"cmd\":\"start_db_feature\",\"status\":\"ok\",\"seq\":%d}", seq); gNetManager.sendJsonTo(clientNum, ack); } } 
    else if (strcmp(cmd, "raw_phase_done") == 0) { gAppController.handleRawPhaseDone(clientNum); if (seq >= 0) { char ack[128]; snprintf(ack, sizeof(ack), "{\"cmd\":\"raw_phase_done\",\"status\":\"ok\",\"seq\":%d}", seq); gNetManager.sendJsonTo(clientNum, ack); } } 
    else if (strcmp(cmd, "db_mark") == 0) { gAppController.handleDbMark(clientNum, doc.as<JsonObject>()); if (seq >= 0) { char ack[128]; snprintf(ack, sizeof(ack), "{\"cmd\":\"db_mark\",\"status\":\"ok\",\"seq\":%d}", seq); gNetManager.sendJsonTo(clientNum, ack); } } 
    else if (strcmp(cmd, "flash_diagnose") == 0) { LOG("[PROTO] flash_diagnose command received\n"); FlashDriver::instance().diagnoseJedec(); if (seq >= 0) { char ack[64]; snprintf(ack, sizeof(ack), "{\"cmd\":\"flash_diagnose\",\"status\":\"ok\",\"seq\":%d}", seq); gNetManager.sendJsonTo(clientNum, ack); } }
    else if (strcmp(cmd, "capture_raw_phase") == 0) {
        uint8_t phase = doc["phase"] | 0;
        LOG("[PROTO] capture_raw_phase phase=%d\n", phase);
        gAppController.handleCaptureRawPhase(clientNum, doc.as<JsonObject>());
        if (seq >= 0) { char ack[128]; snprintf(ack, sizeof(ack), "{\"cmd\":\"capture_raw_phase\",\"status\":\"ok\",\"seq\":%d}", seq); gNetManager.sendJsonTo(clientNum, ack); }
    }
    else if (strcmp(cmd, "reset_calib") == 0) {
        uint8_t slot = doc["slot"] | 0;
        LOG("[PROTO] reset_calib slot=%d\n", slot);
        // 直接调用 handleResetCalib（携带 slot 参数），不走 onCommandReceived 枚举
        gAppController.handleResetCalib(slot);
        if (seq >= 0) {
            char ack[64];
            snprintf(ack, sizeof(ack), "{\"cmd\":\"reset_calib\",\"status\":\"ok\",\"seq\":%d}", seq);
            gNetManager.sendJsonTo(clientNum, ack);
        }
    }
    // 曲线拟合命令支持
    else if (strcmp(cmd, "save_user") == 0) {
        uint8_t slot = doc["slot"] | 0;
        const char* userId = doc["user_id"] | "user_0";
        const char* name = doc["name"] | "";
        uint8_t age = doc["age"] | 0;
        uint8_t gender = doc["gender"] | 0;
        uint8_t handedness = doc["handedness"] | 0;
        LOG("[PROTO] save_user slot=%d name=%s age=%d gender=%d handedness=%d\n", slot, name, age, gender, handedness);
        bool ok = gStorage.SaveUserProfile(slot, userId, name, age, gender, handedness);
        if (seq >= 0) {
            char ack[128];
            snprintf(ack, sizeof(ack), "{\"cmd\":\"save_user\",\"ok\":%s,\"slot\":%d,\"seq\":%d}", ok ? "true" : "false", slot, seq);
            gNetManager.sendJsonTo(clientNum, ack);
        }
    }
    else if (strcmp(cmd, "list_curves") == 0) {
        uint8_t gender = doc["gender"] | 0;
        uint8_t handedness = doc["handedness"] | 0;
        uint8_t age = doc["age"] | 0;
        LOG("[PROTO] list_curves gender=%d handedness=%d age=%d\n", gender, handedness, age);
        gAppController.handleListCurves(clientNum, gender, handedness, age, seq);
    }
    else if (strcmp(cmd, "gen_personal_curve") == 0) {
        uint8_t curveId = doc["curve_id"] | 0;
        float baselineRms = doc["baseline_rms"] | 0.0f;
        float baselineMdf = doc["baseline_mdf"] | 0.0f;
        LOG("[PROTO] gen_personal_curve curve_id=%d baseline_rms=%.2f baseline_mdf=%.1f\n", curveId, baselineRms, baselineMdf);
        gAppController.handleGenPersonalCurve(clientNum, curveId, baselineRms, baselineMdf, seq);
    }
    else { LOG("[PROTO] Unknown cmd: %s\n", cmd); if (seq >= 0) { char ack[64]; snprintf(ack, sizeof(ack), "{\"cmd\":\"error\",\"err\":\"unknown_cmd\",\"seq\":%d}", seq); gNetManager.sendJsonTo(clientNum, ack); } }
}


