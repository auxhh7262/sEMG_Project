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
    void onCommandReceived(AppCommand_t cmd);

    // ---- JSON 命令处理接口 ----
    void handleQueryCZ(uint8_t clientNum, uint32_t startTs, uint32_t endTs);
    void handleSaveRecord(uint8_t clientNum, JsonObject doc);
    void handleListRecords(uint8_t clientNum);
    void handleDeleteRecord(uint8_t clientNum, uint32_t slotAddr);
    void handleListCurves(uint8_t clientNum, uint8_t gender, uint8_t handedness, uint8_t age);
    void handleGenPersonalCurve(uint8_t clientNum, uint8_t bcurveId, float baselineRms, float baselineMdf);
    void handleGetCurve(uint8_t clientNum, uint8_t curveId);

    // ---- 校准命令处理接口 ----
    void handleGetCalibResult(uint8_t clientNum, int seq);  // [v3.9.14] 获取校准结果
    void handleSaveCalib(uint8_t clientNum, int seq);       // [v3.9.14] 从RAM保存校准结果

    // ---- 建库命令处理接口 ----
    void handleStartDbFeature(uint8_t clientNum, JsonObject doc);
    void handleCaptureRawPhase(uint8_t clientNum, JsonObject doc);
    void handleRawPhaseDone(uint8_t clientNum);
    void handleDbMark(uint8_t clientNum, JsonObject doc);

private:
    void _handleIdleState(void);
    void _handleCalibRestState(void);
    void _handleCalibMaxState(void);
    void _handleMonitoringState(void);
    void _handleErrorState(void);
    void _handleDbFeatureState(void);  // 【新增】建库特征采集状态处理

    // 【修复】阶段管理（使用单缓冲区方案）
    uint8_t _currentStage;          // 当前采集阶段 0~3
    bool _stageStarted[4];          // 各阶段是否已开始采集
    uint16_t _rawPhaseCount;         // 当前阶段已采集样本数（单缓冲区）
    uint8_t _lastDbClientNum;        // 建库连接的clientNum

    // 依赖注入
    StateManager* _stateMgr;
    CalibrationManager* _calibMgr;
    SignalProcessor* _signalProc;
    StorageManager* _storageMgr;
    NetManager* _netMgr;
    BleConfigServer* _bleServer;

    // 建库阶段波形缓冲区（仅在 ST_DB_FEATURE 期间使用，采集完即释放）
    int16_t* _rawPhaseBuf;
};

#endif // APP_CONTROLLER_H