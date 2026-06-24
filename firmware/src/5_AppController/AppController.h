#ifndef APP_CONTROLLER_H
#define APP_CONTROLLER_H

#include <stdint.h>
#include <ArduinoJson.h>
#include "0_Base/Globals.h"

// 【关键修正】直接包含 C++ 头文件，而不是使用前向声明
#include "0_Base/SystemStateMachine.h"
#include "2_Calibration/CalibrationManager.h"
#include "1_Signal/SignalProcessor.h"
#include "3_Storage/StorageManager.h" // 这是一个 C 模块，保持指针即可
#include "4_Network/NetManager.h"
#include "4_Network/BleConfigServer.h"

class AppController {
public:
    AppController(
        StateManager* stateMgr,
        CalibrationManager* calibMgr,
        SignalProcessor* signalProc,
        StorageManager* storageMgr,
        NetManager* netMgr,
        BleConfigServer* bleServer
    );

    void init(void);
    void tick(void);
    void onCommandReceived(AppCommand_t cmd, uint8_t clientNum = 255);

    // ---- JSON command handlers ----
    void handleQueryCZ(uint8_t clientNum, uint32_t startTs, uint32_t endTs);
    void deferQueryCZ(uint8_t clientNum, uint32_t startTs, uint32_t endTs);  // [FIX] Defer to tick()
    void handleSaveRecord(uint8_t clientNum, JsonObject doc);
    void handleListRecords(uint8_t clientNum);
    void handleDeleteRecord(uint8_t clientNum, uint32_t slotAddr);
    void handleListCurves(uint8_t clientNum, uint8_t gender, uint8_t handedness, uint8_t age, int seq = -1);
    void handleGenPersonalCurve(uint8_t clientNum, uint8_t bcurveId, float baselineRms, float baselineMdf, int seq = -1);
    void handleGetCurve(uint8_t clientNum, uint8_t curveId);

    // ---- Calibration command handlers ----
    void handleGetCalibResult(uint8_t clientNum, int seq, uint8_t slot = 0);
    void handleSaveCalib(uint8_t clientNum, int seq, uint8_t slot = 0);
    void handleResetCalib(uint8_t slot);

    // ---- Database command handlers ----
    void handleStartDbFeature(uint8_t clientNum, JsonObject doc);
    void handleCaptureRawPhase(uint8_t clientNum, JsonObject doc);
    void handleRawPhaseDone(uint8_t clientNum);
    void handleDbMark(uint8_t clientNum, JsonObject doc);

private:
    uint8_t _monitoringClientNum;  // [FIX] 记录当前监测模式的客户端编号
    // [FIX] Deferred query_cz (avoid stack overflow in WS callback)
    volatile bool _pendingQueryCZ = false;
    uint8_t _pendingQueryCZClientNum = 255;
    uint32_t _pendingQueryStartTs = 0;
    uint32_t _pendingQueryEndTs = 0;
    void _handleIdleState(void);
    void _handleCalibRestState(float rms, float mdf);
    void _handleCalibMaxState(float rms, float mdf);
    void _handleMonitoringState(float rms, float mdf, float fatigue, uint8_t quality, float activation);
    void _handleErrorState(void);
    void _handleDbFeatureState(float rms, float mdf, float fatigue, uint8_t quality, float activation);

    // Stage management (single buffer)
    uint8_t _currentStage;
    bool _stageStarted[4];
    uint16_t _rawPhaseCount;
    uint8_t _lastDbClientNum;

    // Calib debug counters (member vars to reset per-calib)
    uint32_t _calibRestDbgCount;
    uint32_t _calibMaxDbgCount;

    // Dependencies
    StateManager* _stateMgr;
    CalibrationManager* _calibMgr;
    SignalProcessor* _signalProc;
    StorageManager* _storageMgr;
    NetManager* _netMgr;
    BleConfigServer* _bleServer;

    // Raw phase buffer (only used during ST_DB_FEATURE)
    int16_t* _rawPhaseBuf;
};

#endif // APP_CONTROLLER_H
