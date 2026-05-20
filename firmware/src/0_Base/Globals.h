// 文件: 0_Base/Globals.h
#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include "Config.h"

// 1. 错误码枚举 (映射协议字典 err_calib_failed 的 msg)
typedef enum {
    ERROR_NONE = 0,
    ERROR_CALIB_INVALID,  // 力度不足
    ERROR_SIGNAL_NOISE,   // 信号干扰
    ERROR_ARM_TIMEOUT,    // 等待超时
    ERROR_BODY_MOVE,      // 肢体乱动
    ERROR_CALIB_DRIFT,    // 佩戴松动
    ERROR_FLASH_FULL,     // 存储已满
    ERROR_FLASH_IO        // 读写故障
} SystemError_t;

// 2. 系统状态枚举 (M3 阶段简化版状态机，为后续完整 SOP 预留扩展)
typedef enum {
    ST_BOOT = 0,
    ST_IDLE,
    ST_CALIB_REST,
    ST_CALIB_WAIT,    // REST完成，等待小程序触发MAX
    ST_CALIB_MAX,
    ST_CALIB_DONE,    // MAX完成，等待小程序保存校准结果
    ST_MONITORING,
    ST_DB_FEATURE,   // 【新增】建库特征采集状态（10Hz特征流写入B区）
    ST_ERROR
} SystemState_t;

// 3. 核心校准数据结构体 (V1.0)
typedef struct {
    float bias_voltage;  // 极化零点电压 (当前暂填 0.0f)
    float rest_rms;      // 底噪 RMS (mV)
    float ref_rms;       // 标尺 RMS (mV)
    float rest_mdf;      // 底噪 MDF (Hz) - 【新增】
    float ref_mdf;       // 标尺 MDF (Hz) - 【新增】
    float peak_rms;      // MAX阶段峰值RMS (mV) - [v3.9.11] 用于激活度计算
    bool valid;
} CalibData_t;

// ==========================================
// 【V1.0】网络与云端扩展定义
// ==========================================

// 4. 网络工作模式枚举
typedef enum {
    NET_MODE_IDLE = 0,
    NET_MODE_BLE_CONFIG,    // 蓝牙等待配网中
    NET_MODE_WIFI_CONNECTING,// 正在连接路由器
    NET_MODE_WIFI_ONLINE    // WiFi已连接，业务在线
} NetMode_t;

// 5. 小程序/云端下行 统一指令枚举
typedef enum {
    CMD_NONE = 0,
    CMD_START_CALIB,        // 开始校准(REST阶段)
    CMD_START_CALIB_MAX,    // 开始校准MAX阶段(小程序显式触发)
    CMD_STOP,               // 停止当前流程
    CMD_GET_STATUS,         // 查询状态
    CMD_RESET_CALIB,        // 重置校准
    CMD_START_RECORD,       // 开始建库采集
    CMD_FETCH_HISTORY,      // 拉取Flash历史建库数据
    CMD_SYNC_TIME,          // 强制NTP时间同步 (借用为：触发BLE配网)
    CMD_VERIFY_CALIB,       // 新增：验证校准数据命令
    CMD_ENTER_OTA,          // 进入固件升级模式
    CMD_DEBUG_SIGNAL,       // 调试信号命令
    CMD_INJECT_SIGNAL,      // 注入测试信号命令
    CMD_SIGNAL_DIAGNOSE,    // 信号诊断命令
    
    // --- C区查询 + B区列表 + 建库保存 ---
    CMD_QUERY_CZ,           // 查询 C 区历史数据
    CMD_SAVE_RECORD,        // 保存建库数据到 B 区
    CMD_LIST_RECORDS,       // 列出 B 区已有记录
    CMD_DELETE_RECORD,       // 删除 B 区指定槽位记录
    // --- B区曲线库 ---
    CMD_LIST_CURVES,         // 查询匹配条件的群体曲线列表
    CMD_GEN_PERSONAL_CURVE, // 生成个人曲线（A区）
    CMD_GET_CURVE,           // 获取单条曲线详情
    CMD_GET_CALIB_RESULT    // 获取当前校准结果
} AppCommand_t;

// 6. WiFi 凭证结构体
typedef struct {
    char ssid[32];
    char pass[64];
    bool isValid;
} WifiCredentials_t;

// ==========================================
// 【原有】B区：群体建库 Header 结构体
// ==========================================
struct __attribute__((packed)) BZone_Header_t {
    uint16_t struct_version; // 0x0101
    uint32_t sample_rate;    // 1000
    uint32_t data_length;    // 28000
    char username[16];
    uint8_t is_valid;        // 0x01
    // --- 数据自包含字段（防标尺漂移） ---
    float bias_voltage;
    float rest_rms;
    float ref_rms;
    // --- 真实时间戳溯源 ---
    uint32_t timestamp_s;
    uint16_t timestamp_ms;
    uint8_t reserved[627];   // 精确填充至 672 字节
};

// 7. 特征值长度宏 (给 BLE 用)
#define BLE_WIFI_CHAR_MAX_LEN 128

// 8. 【关键防御】全局致命错误状态变量
extern volatile SystemError_t g_systemFatalError;
#endif // GLOBALS_H