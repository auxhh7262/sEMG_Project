#ifndef STORAGE_MANAGER_H
#define STORAGE_MANAGER_H

#include "StorageArch.h"
#include <stdint.h>
#include "0_Base/Globals.h"

// ==================== 公共类型定义 ====================

typedef struct {
    float rest_rms_mv;
    float max_rms_mv;
    float rest_mdf_hz;
    float max_mdf_hz;
    uint32_t calib_timestamp;
    uint8_t calib_quality;
    float peak_rms_mv;  // [v3.9.11] MAX阶段峰值RMS，用于激活度计算
    float curve_coef[5];
    float curve_baseline_rms;
    float curve_baseline_mdf;
    uint8_t matched_bcurve_id;
    float fit_confidence;
    uint8_t curve_version;
    uint8_t curve_status;
    uint8_t has_curve;    // [v3.9.12] 曲线是否已拟合
} PersonalCalibData_t;

typedef struct {
    uint32_t subject_id;
    uint8_t age;
    uint8_t gender;
    uint8_t handedness;
} SubjectBasicInfo_t;

// B区曲线库条目（小程序通信用，从 Flash 结构映射）
typedef struct {
    uint8_t  curve_id;
    uint8_t  curve_type;
    float    coefficients[5];
    float    group_baseline_rms;
    float    group_baseline_mdf;
    uint8_t  gender_hint;
    uint8_t  age_min;
    uint8_t  age_max;
    uint8_t  handedness_hint;
    uint8_t  active_slot_count;
    uint8_t  curve_status;
    uint8_t  fit_quality;
    char     description[32];
} BZone_CurveEntry_t;

typedef enum {
    B_STATE_IDLE = 0,
    B_STATE_ERASING,          // [C0-4-fix] 异步擦除槽位中
    B_STATE_WRITING_HEADER,
    B_STATE_WRITING_FEATURES,
    B_STATE_WRITING_RAW_WAVE,
    B_STATE_COMMITTING
} BZoneWriteState_t;

typedef struct {
    BZoneWriteState_t state;
    uint32_t slot_addr;
    uint32_t write_offset;
    uint16_t features_written;
    uint16_t samples_written;
    uint8_t current_phase;
    uint8_t erase_progress;    // [C0-4-fix] 已擦除扇区数 (0~B_SLOT_SECTORS-1)
} BZoneWriteContext_t;

// ==================== C++ StorageManager 类 ====================
class StorageManager {
public:
    int Init();
    bool GetPersonalCalib(PersonalCalibData_t* data);
    bool UpdatePersonalCalib(const PersonalCalibData_t* data);

    // 建库接口
    bool BZone_AppendFeaturePoint(uint16_t rmsCompressed, uint16_t mdfCompressed);  // 单点追加
    bool BZone_AppendRawPhase(uint8_t phaseIndex, const int16_t* samples, uint16_t count);
    bool BZone_MarkFeaturePoint(uint8_t markIndex);  // 记录当前特征点索引到header的mark_feature_idx[]
    uint16_t BZone_GetFeatureCount();  // 获取当前已写入特征点数

    // B区接口
    bool BZone_GetNextAvailableSlot(uint32_t* slotAddr);
    bool BZone_BeginRecord(const SubjectBasicInfo_t* subjectInfo, uint32_t slotAddr);
    bool BZone_AppendFeatureSequence(const uint16_t* rmsArray, const uint16_t* mdfArray, uint16_t count);
    bool BZone_AppendRawWaveform(const int16_t* phase1, const int16_t* phase2, const int16_t* phase3, const int16_t* phase4, uint16_t samplesPerPhase);
    bool BZone_EndRecord();

    // C区接口
    bool CZone_AppendDataPoint(const CZone_DataPoint_t* dataPoint);

    // ---- C 区查询接口 ----
    // 按时间范围查询 C 区数据点，返回最多 maxPoints 个
    // outNextTs 用于分页：下次查询的起始时间戳（0 表示无更多数据）
    bool CZone_QueryByTimeRange(uint32_t startTs, uint32_t endTs,
                                CZone_DataPoint_t* outBuf, uint16_t maxPoints,
                                uint16_t* outCount, uint32_t* outNextTs);

    // ---- B 区记录列表 ----
    bool BZone_ListRecords(BZone_ListEntry_t* outEntries, uint8_t maxEntries, uint8_t* outCount);

    // ---- B 区删除槽位 ----
    // slotAddr: B区槽位起始地址（来自 record_list 响应的 slot 字段）
    // 返回: true 删除成功，false 槽位无效或已空
    bool BZone_DeleteSlot(uint32_t slotAddr);

    // [B2-2-fix] 主循环轮询接口，用于异步擦除完成检测等
    void tick();

    // ---- B 区曲线库（B区后半段 32KB）----
    // 查询所有有效曲线，支持按条件筛选
    // gender: 1=男, 2=女, 0=不限
    // handedness: 1=左手, 2=右手, 0=不限
    // age: 被测者年龄，用于范围匹配（age_min <= age <= age_max）
    // 返回最多 maxEntries 条，outCount 填实际数量
    bool BZone_ListCurves(uint8_t gender, uint8_t handedness, uint8_t age,
                         BZone_CurveEntry_t* outEntries, uint8_t maxEntries, uint8_t* outCount);

    // 读取单条曲线详情（用于匹配后加载到 RAM）
    bool BZone_GetCurve(uint8_t curveId, BZone_CurveEntry_t* outCurve);

    // 将群体曲线+个人校准基准 → 生成个人曲线 → 写入 A 区
    // 公式: y_personal = y_group × (personal_baseline / group_baseline)
    // 调用前需确保已执行过个人校准（g_isCalibDataValid=true）
    bool GeneratePersonalCurve(uint8_t bcurve_id, const float personal_baseline_rms,
                               const float personal_baseline_mdf);

    // WiFi 接口
    bool LoadWifiCredentials(WifiCredentials_t* outCreds);
    bool SaveWifiCredentials(const WifiCredentials_t* creds);

    // [v3.9.13] 用户档案接口
    bool SaveUserProfile(uint8_t slot, const char* userId, const char* name,
                        uint8_t age, uint8_t gender, uint8_t handedness);
    bool LoadUserProfile(uint8_t slot, char* outUserId, char* outName,
                         uint8_t* outAge, uint8_t* outGender, uint8_t* outHandedness);
};

#endif // STORAGE_MANAGER_H
