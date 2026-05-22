// 文件: main.cpp
// 描述: sEMG 肌电疲劳监测设备主程序 V1.0
// 原则: main 只做三件事 —— 喂采样、心跳、调用调度器
// ============================================================

#include <Arduino.h>
#include <FspTimer.h>

// 基础驱动
#include "0_Base/Board.h"
#include "0_Base/Logger.h"
#include "0_Base/Globals.h"

// 业务与网络模块
#include "0_Base/SystemStateMachine.h"
#include "2_Calibration/CalibrationManager.h"
#include "1_Signal/SignalProcessor.h"
#include "3_Storage/StorageManager.h"
#include "4_Network/BleConfigServer.h"
#include "4_Network/NetManager.h"

// 调度层
#include "5_AppController/AppController.h"
#include "0_Base/ProtocolHandler.h"

// ============================================================
// 全局实例（仅做“接线”，不做逻辑）
// ============================================================
SignalProcessor gSignal;
StateManager gState;
CalibrationManager gCalib;
StorageManager gStorage;
BleConfigServer gBleConfig;
NetManager gNetManager;
ProtocolHandler gProtocol;
AppController gAppController(
    &gState,
    &gCalib,
    &gSignal,
    &gStorage,
    &gNetManager,
    &gBleConfig
);

// 硬件定时器（原 Board.cpp 已删除，直接在此定义）
FspTimer adc_timer;
volatile bool g_adcTimerFlag = false;
volatile uint32_t g_adcCallbackCount = 0;  // [DEBUG] ISR计数器

// ============================================================
// 定时器中断
// ============================================================
void timer_callback(timer_callback_args_t __attribute((unused)) *args) {
    g_adcTimerFlag = true;
    g_adcCallbackCount++;  // [DEBUG] ISR计数
}

// 溢出中断ISR（无参数版本，用于setup_overflow_irq）
void timer_overflow_isr() {
    g_adcTimerFlag = true;
    g_adcCallbackCount++;
}

// ============================================================
// setup()
// ============================================================
void setup() {
    delay(3000);  // 等待串口监控连接，确保开机日志不丢失
    SERIAL_COMM.begin(115200);
    while (!SERIAL_COMM && millis() < 3000);

    // 开机分隔线（区分本次启动与上次启动的串口残留）
    LOG("\n\n========== sEMG V1.0 BOOT ==========\n");

    pinMode(PIN_LED_BUILTIN, OUTPUT);
    digitalWrite(PIN_LED_BUILTIN, LOW);

    analogReadResolution(14);
    gSignal.init();

    // 1kHz ADC 定时器
    uint8_t timer_type = 0;
    int8_t timer_channel = FspTimer::get_available_timer(timer_type);
    if (timer_channel < 0) {
        LOG("[MAIN] ERROR: No available timer, trying force...\n");
        timer_channel = FspTimer::get_available_timer(timer_type, true);
        if (timer_channel < 0) {
            LOG("[MAIN] ERROR: No timer available even with force!\n");
        }
    }
    LOG("[MAIN] Timer type=%d, channel=%d\n", timer_type, timer_channel);
    
    bool begin_ok = adc_timer.begin(
        TIMER_MODE_PERIODIC,
        timer_type,
        (uint8_t)timer_channel,
        1000.0f,
        0.0f,
        timer_callback
    );
    if (!begin_ok) {
        LOG("[MAIN] ERROR: Timer begin failed!\n");
    } else {
        LOG("[MAIN] Timer begin OK\n");
        
        // [P0-fix-v2] 使用FSP标准回调机制
        // 传入nullptr让IRQManager使用默认的gpt_counter_overflow_isr
        // 该ISR会自动调用begin()设置的p_callback（即timer_callback）
        bool irq_ok = adc_timer.setup_overflow_irq(12, nullptr);
        if (!irq_ok) {
            LOG("[MAIN] ERROR: Timer overflow IRQ setup failed!\n");
        } else {
            LOG("[MAIN] Timer overflow IRQ setup OK (using FSP default ISR)\n");
            bool open_ok = adc_timer.open();
            if (!open_ok) {
                LOG("[MAIN] ERROR: Timer open failed!\n");
            } else {
                LOG("[MAIN] Timer opened (R_GPT_Enable called)\n");
                bool start_ok = adc_timer.start();
                if (!start_ok) {
                    LOG("[MAIN] ERROR: Timer start failed!\n");
                } else {
                    LOG("[MAIN] Timer start OK - 1kHz sampling via FSP callback\n");
                }
            }
        }
    }

    // 模块初始化
    gState.init();
    gCalib.init();
    gStorage.Init(); // 【修正】调用正确的对象
    gNetManager.init(&gBleConfig);
    gBleConfig.init();
    gAppController.init();

    // 注册 JSON 命令回调
    gNetManager.setCommandCallback([](uint8_t clientNum, const char* json) {
        gProtocol.handleJsonCommand(clientNum, json);
    });

    // 初始化完成，进入 IDLE 状态
    // [FIX] Removed: gState.transitionTo(ST_IDLE);

    LOG("[MAIN] V1.0 系统初始化完成\n");
}

// ============================================================
// loop()
// ============================================================
void loop() {
    // 1. 高频采样（1kHz）
    if (g_adcTimerFlag) {
        g_adcTimerFlag = false;
        int raw = FAST_ADC_READ(PIN_EMG_ADC);
        gSignal.isrPushSample(raw);
        gSignal.updateSampleRateStats();  // [P0-fix] 采样率统计从ISR移到loop
    }

    // 2. 10Hz 主调度节拍
    static uint32_t lastTick = 0;
    if (millis() - lastTick < LOOP_INTERVAL_MS) {
        return;
    }
    lastTick = millis();

    // 3. 存储管理
    gStorage.tick();  // [B2-2-fix] 异步擦除轮询

    // [DEBUG] 每2秒打印ADC采样状态
    static uint32_t debug_tick = 0;
    if (++debug_tick >= 20) {  // 10Hz * 2s = 20 ticks
        debug_tick = 0;
        LOG("[MAIN] ADC callbacks=%lu, samples=%u\n", 
            (unsigned long)g_adcCallbackCount, gSignal.getBufferAvailable());
    }

    // 4. 网络心跳
    gNetManager.tick();
    gBleConfig.tick();

    // 5. 串口指令解析
    AppCommand_t cmd = gProtocol.tickLocalDebug();
    if (cmd != CMD_NONE) {
        gAppController.onCommandReceived(cmd);
    }

    // 5. 业务调度（全部交给 AppController）
    gAppController.tick();
}