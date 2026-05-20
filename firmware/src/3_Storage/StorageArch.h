// File: StorageArch.h
// Description: sEMG设备存储架构V1.0
// Version: 1.0
// Note: 使用兼容的断言宏
#ifndef STORAGE_ARCH_H
#define STORAGE_ARCH_H
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

// ==================== 编译时断言宏 (兼容性处理) ====================
#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 6))
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#elif defined(__clang__)
#define STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#elif defined(_MSC_VER) && (_MSC_VER >= 1800)
#define STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#else
#define STATIC_ASSERT(cond, msg) typedef char static_assert_##__LINE__[(cond) ? 1 : -1] __attribute__((unused))
#endif

// ==================== 通用常量与宏 ====================
#define FLASH_TOTAL_SIZE (0x1000000UL) // 16 MB
#define FLASH_END_ADDR (0x0FFFFFFFUL)
#define SECTOR_SIZE (4096UL)
#define BLOCK_SIZE_64K (65536UL)

// 版本打包/解包宏
#define PACK_VERSION(major, minor) ((((uint16_t)(major) & 0xFF) << 8) | ((uint16_t)(minor) & 0xFF))
#define GET_MAJOR_VERSION(ver) (((ver) >> 8) & 0xFF)
#define GET_MINOR_VERSION(ver) ((ver) & 0xFF)

// 全局量化系数 (强制统一)
#define RMS_SCALE_FACTOR 0.01f // 存储值 * 0.01 = 实际电压 (mV)
#define MDF_SCALE_FACTOR 0.1f  // 存储值 * 0.1 = 实际频率

// CRC16 多项式 (强制统一为 Modbus)
#define CRC16_POLYNOMIAL 0xA001
#define CRC16_INIT 0xFFFF

// ==================== A区定义 (个人档案与模型区) ====================
#define ZONE_A_NUM_SECTORS 2
#define ZONE_A_TOTAL_SIZE (ZONE_A_NUM_SECTORS * SECTOR_SIZE) // 0x2000
#define ZONE_A_BASE_ADDR (0x0000000UL)
#define ZONE_A_END_ADDR (ZONE_A_BASE_ADDR + ZONE_A_TOTAL_SIZE - 1) // 0x0001FFF

/* A区扇区数据结构 (AZone_Sector_t) - 严格4096字节 */
#pragma pack(push, 1)
typedef struct {
    /* --- 扇区头 (8字节) --- */
    uint32_t magic;         // 0xA55AA55A
    uint16_t struct_version;// PACK_VERSION(1, 2) [v3.9.12] 新增has_curve
    uint16_t header_crc16;  // CRC16(从`user_id`到`sys_reserved`，【修复】不含reserved空闲区

    /* --- 有效数据载荷 (111字节) --- */
    // 1. 个人身份信息 (54字节)
    char user_id[16];
    char name[32];
    uint8_t age;
    uint8_t gender;         // 1:男, 2:女
    uint8_t handedness;     // 1:左手, 2:右手
    uint8_t info_reserved[3];

    // 2. 个人校准参数 (28字节) [v3.9.11] 新增peak_rms_mv
    float rest_rms_mv;
    float max_rms_mv;
    float rest_mdf_hz;
    float max_mdf_hz;
    uint32_t calib_timestamp;
    uint8_t calib_quality;
    float peak_rms_mv;      // [v3.9.11] MAX阶段峰值RMS，用于激活度计算

    // 3. 个性化拟合曲线 (36字节)
    float curve_coef[5];         // 个人曲线5系数 (方案A平移用)
    float curve_baseline_rms;   // 个人曲线RMS基准锚点
    float curve_baseline_mdf;    // 个人曲线MDF基准锚点
    uint8_t matched_bcurve_id;   // 匹配的B区曲线ID (0~63)，0xFF=未选择
    float fit_confidence;        // 拟合置信度 (0.0~1.0)
    uint8_t curve_version;       // 曲线版本号
    uint8_t has_curve;        // [v3.9.12] 曲线是否已拟合: 0=无, 1=有
    uint8_t curve_status;      // 0=未生成, 1=有效, 2=需刷新
    uint8_t curve_reserved[1];

    // 4. C区状态锚点 (8字节)
    uint32_t cz_active_block_addr;
    uint16_t cz_write_offset;
    uint8_t cz_wrap_around;
    uint8_t sys_reserved;

    // --- 预留空间 (3964字节) [v3.9.11] 新增peak_rms_mv占用4字节 ---
    uint8_t reserved[3964];
} AZone_Sector_t;
#pragma pack(pop)
STATIC_ASSERT(sizeof(AZone_Sector_t) == 4096, "AZone_Sector_t size must be 4096 bytes");

// ==================== B区定义 (肌电特征数据库) ====================
#define B_SLOT_SECTORS 10
#define B_SLOT_SIZE_BYTES (B_SLOT_SECTORS * SECTOR_SIZE) // 0xA000 (40KB)
#define B_MAX_SLOTS 49 // 【物理强制】从52降至49

// B区起始地址
#define B_ZONE_BASE_ADDR (ZONE_A_END_ADDR + 1) // 0x0002000

// 曲线库预留空间
#define B_CURVE_LIB_SECTORS 8
#define B_CURVE_LIB_SIZE (B_CURVE_LIB_SECTORS * SECTOR_SIZE) // 0x8000
#define B_CURVE_LIB_ADDR (B_ZONE_BASE_ADDR + (B_MAX_SLOTS * B_SLOT_SIZE_BYTES))
#define B_ZONE_RAW_DATA_END_ADDR (B_CURVE_LIB_ADDR - 1)
#define B_ZONE_END_ADDR (B_CURVE_LIB_ADDR + B_CURVE_LIB_SIZE - 1)

/* B区槽位状态 */
typedef enum {
    B_SLOT_EMPTY = 0x00,
    B_SLOT_WRITING = 0x01,
    B_SLOT_VALID = 0x02,
    B_SLOT_DELETED = 0x03
} BSlotStatus_t;

/* 【新增修复】B区轻量级头部结构（仅用于RAM中暂存，约100字节） */
#pragma pack(push, 1)
typedef struct {
    uint8_t magic[4]; // {'S','E','M','G'}
    uint16_t struct_version;
    uint8_t record_status; // BSlotStatus_t
    uint8_t reserved_head[57];
    uint32_t subject_id;
    uint8_t age;
    uint8_t gender;
    uint8_t handedness;
    uint8_t data_quality;
    uint32_t test_timestamp;
    uint16_t test_duration_sec;
    float snap_rest_rms_mv;
    float snap_max_rms_mv;
    float snap_rest_mdf_hz;
    float snap_max_mdf_hz;
    uint8_t snap_calib_quality;
    // 疲劳标记：记录标记时刻的特征点序号（由db_mark命令写入）
    uint16_t mark_feature_idx[4]; // 4段标记对应的特征点索引
    uint8_t  mark_count;          // 已标记数量(0~4)
    uint8_t  reserved_meta[2];    // 保留对齐
} BZone_Header_Payload_t;
#pragma pack(pop)

/* B区单条原始记录结构 - 仅作为物理映射模板，严禁在RAM中声明全局变量 */
#pragma pack(push, 1)
typedef struct {
    // 记录头
    uint8_t magic[4];
    uint16_t struct_version;
    uint8_t record_status;
    uint8_t reserved_head[57];
    uint32_t subject_id;
    uint8_t age;
    uint8_t gender;
    uint8_t handedness;
    uint8_t data_quality;
    uint32_t test_timestamp;
    uint16_t test_duration_sec;
    float snap_rest_rms_mv;
    float snap_max_rms_mv;
    float snap_rest_mdf_hz;
    float snap_max_mdf_hz;
    uint8_t snap_calib_quality;
    uint16_t mark_feature_idx[4]; // 疲劳标记特征点索引
    uint8_t  mark_count;          // 已标记数量(0~4)
    uint8_t  reserved_meta[2];    // 保留对齐
    // 特征序列 (12,000 字节)
    uint16_t rms_sequence[3000];
    uint16_t mdf_sequence[3000];
    // 原始波形 (24,000 字节)
    int16_t raw_phase_1[3000];
    int16_t raw_phase_2[3000];
    int16_t raw_phase_3[3000];
    int16_t raw_phase_4[3000];
    // 记录尾
    uint16_t data_crc16;
    uint32_t commit_marker; // 0xAA55AA55
    uint8_t tail_reserved[6];
} BZone_Record_t;
#pragma pack(pop)

/* B区特征曲线库结构 */
#pragma pack(push, 1)
typedef struct {
    uint16_t library_version;
    uint16_t max_supported_curves;
    uint16_t curve_count;
    uint32_t gen_timestamp;
    struct {
        uint8_t  curve_id;
        uint8_t  curve_type;
        float    coefficients[5];
        uint8_t  gender_hint;    // 1:男, 2:女
        uint8_t  age_min;
        uint8_t  age_max;
        uint8_t  handedness_hint;// 1:左手, 2:右手
        uint8_t  fit_quality;
        uint8_t  contributing_slots[8]; // 贡献槽位列表 (0xFF=无效槽)
        uint8_t  active_slot_count;     // 当前有效槽位数 (0=需刷新)
        uint8_t  curve_status;  // 0x00=空, 0x01=有效, 0x02=需刷新
        uint8_t  reserved_curve[1];
    } curves[63];
    uint16_t library_crc16;
} BZone_CurveLibrary_t;
#pragma pack(pop)
// 曲线数从64降至63以确保 fit 32KB (8扇区): 63×40=2520+header(12)+CRC(2)=2534 < 32768
STATIC_ASSERT(sizeof(BZone_CurveLibrary_t) <= B_CURVE_LIB_SIZE,
    "BZone_CurveLibrary_t exceeds 32KB reserved space");

// ==================== C区定义 (个人实时监控区) ====================
#define C_ZONE_BASE_ADDR (B_ZONE_END_ADDR + 1) // 0x0200000
#define C_ZONE_END_ADDR FLASH_END_ADDR // 0x0FFFFFFF
#define C_ZONE_TOTAL_SIZE (C_ZONE_END_ADDR - C_ZONE_BASE_ADDR + 1)
#define C_BLOCK_SIZE BLOCK_SIZE_64K

/* C区数据点结构 */
#pragma pack(push, 1)
typedef struct {
    uint32_t timestamp_ms;
    uint16_t rms_compressed;
    uint16_t mdf_compressed;
    uint8_t fatigue_level;
    uint8_t signal_quality;
    uint8_t motion_state;
    uint8_t reserved;
} CZone_DataPoint_t; // 12 字节
#pragma pack(pop)

/* C区64KB块头结构 */
#pragma pack(push, 1)
typedef struct {
    uint32_t magic;           // 0x435A424C ("CZBL")
    uint32_t start_timestamp;
    uint16_t num_points;
    uint8_t block_status;
    uint8_t reserved[25];
    uint16_t block_crc16;
} CZone_BlockHeader_t; // 32 字节
#pragma pack(pop)

/* B 区记录列表条目（用于返回给小程序） */
#pragma pack(push, 1)
typedef struct {
    uint32_t slot_addr;
    uint8_t  record_status;
    uint32_t subject_id;
    uint32_t test_timestamp;
    uint16_t test_duration_sec;
    uint8_t  age;
    uint8_t  gender;
    uint8_t  handedness;
    float    snap_rest_rms_mv;
    float    snap_max_rms_mv;
    float    snap_rest_mdf_hz;
    float    snap_max_mdf_hz;
    uint8_t  snap_calib_quality;
    char     name[24];
    uint8_t  reserved[8];
} BZone_ListEntry_t;
#pragma pack(pop)

#endif // STORAGE_ARCH_V8_H
