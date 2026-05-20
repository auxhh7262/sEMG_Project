#include "StorageManager.h"
#include "FlashDriver.h"
#include "0_Base/Logger.h"
#include <EEPROM.h>
#include <string.h>

// Flash 驱动单例快捷引用
static FlashDriver& flash = FlashDriver::instance();

// WiFi凭证存RA4M1板载Data Flash（通过EEPROM库访问，真实Flash非模拟）
// RA4M1 Data Flash 8KB，WiFi凭证仅用97字节，与SPI Flash外存完全隔离
#define EEPROM_WIFI_SSID_ADDR  0x00   // 32 bytes
#define EEPROM_WIFI_PASS_ADDR  0x20   // 64 bytes (offset 32)
#define EEPROM_WIFI_VALID_ADDR 0x60   // 1 byte  (offset 96)
#define EEPROM_WIFI_MAGIC      0xA5   // 有效标记

// ==================== 私有全局变量 ====================
static AZone_Sector_t g_workBuf;            // [C0-6-fix] 4KB工作缓冲：兼作A区镜像、CRC计算、A区异步写暂存（互斥）
// 注意：g_aZoneTmpBuf 已移除，所有操作复用 g_workBuf，避免RAM溢出
// 注意：与 g_asyncEraseState 状态机配合使用
static PersonalCalibData_t g_currentCalibData;
static bool g_isCalibDataValid = false;

// CZone 相关
static uint8_t g_czone_ram_cache[256];          // 单页写缓冲
static uint16_t g_czone_cache_write_pos = 0;     // cache内写入偏移
static uint32_t g_czone_current_block_addr = 0;  // 当前C区64KB块起始地址
static uint16_t g_czone_block_write_offset = 0;  // 当前块内偏移(含header)
static bool g_czone_initialized = false;

// [C0-4/C0-5-fix] 异步擦除状态机
typedef enum {
    ASYNC_ERASE_IDLE = 0,
    ASYNC_ERASE_AZONE_SECTOR,   // A区扇区擦除（UpdatePersonalCalib）
    ASYNC_ERASE_BZONE_SLOT,     // B区槽位逐扇区擦除（BZone_BeginRecord）
    ASYNC_ERASE_CZONE_BLOCK     // C区64KB块擦除（_czone_initFromScratch / _allocateNewBlock）
} AsyncEraseState_t;

static volatile AsyncEraseState_t g_asyncEraseState = ASYNC_ERASE_IDLE;
static uint8_t  g_asyncEraseProgress = 0;          // B区已擦除扇区计数
static uint32_t g_asyncEraseAddr = 0;              // 当前擦除地址
static uint32_t g_asyncEraseEndAddr = 0;           // B区擦除终止地址
// A区异步擦除完成后的待写数据（与g_workBuf互斥，复用同一缓冲区）
static bool     g_asyncAZonePending = false;       // 有待写入的A区扇区
static uint8_t  g_asyncAZoneTargetSector = 0;      // 目标扇区号(0 or 1)

// BZone 相关
static BZoneWriteContext_t g_bzone_ctx = { B_STATE_IDLE };
static BZone_Header_Payload_t g_bzone_record_header;

// ==================== 私有辅助函数声明 ====================
static uint16_t _calcCRC16(const uint8_t* data, uint32_t length, uint16_t init);
static bool _validateAZoneSector(const AZone_Sector_t* sector);
static uint32_t _getAZoneSectorAddr(uint8_t index);
static bool _writeMultiPage(uint32_t addr, const void* data, uint32_t len);
static bool _czone_initFromScratch(void);
static bool _czone_loadStateFromAZone(void);
static bool _czone_flushCache(void);
static bool _czone_allocateNewBlock(void);
static void _czone_finalizeCurrentBlock(void);
static bool _bzone_stream_write(const void* data, uint32_t len);
static bool _bzone_write_footer_and_commit(void);
static void _bzone_writeHeaderToSlot(uint32_t slotAddr, const BZone_Header_Payload_t* hdr);

// ==================== StorageManager 类方法实现 ====================

int StorageManager::Init() {
    if (!flash.Init())
        return -1;

    // [S0-1-fix] 逐扇区读取验证，复用 g_workBuf 作为工作缓冲区
    bool sectorValid[ZONE_A_NUM_SECTORS] = {false};

    // 扇区0：读入g_workBuf验证
    flash.ReadBytes(_getAZoneSectorAddr(0), &g_workBuf, sizeof(AZone_Sector_t));
    sectorValid[0] = _validateAZoneSector(&g_workBuf);

    // 扇区1：复用g_workBuf验证（先保存扇区0内容到栈上一个小标记）
    // [栈溢出修复] 不在栈上分配AZone_Sector_t(4096字节)，而是直接读入g_workBuf
    // 如果扇区1有效则保留在g_workBuf中，否则恢复扇区0
    bool sector1Valid = false;
    // 临时保存扇区0的有效性标记（g_workBuf内容会被扇区1覆盖）
    bool sector0Valid = sectorValid[0];
    // 保存扇区0的关键数据以便恢复（只需要校准字段）
    PersonalCalibData_t sector0Calib = {0};
    if (sector0Valid) {
        memcpy(&sector0Calib, &g_currentCalibData, sizeof(PersonalCalibData_t));
        sector0Calib.rest_rms_mv = g_workBuf.rest_rms_mv;
        sector0Calib.max_rms_mv = g_workBuf.max_rms_mv;
        sector0Calib.rest_mdf_hz = g_workBuf.rest_mdf_hz;
        sector0Calib.max_mdf_hz = g_workBuf.max_mdf_hz;
    }
    flash.ReadBytes(_getAZoneSectorAddr(1), &g_workBuf, sizeof(AZone_Sector_t));
    sector1Valid = _validateAZoneSector(&g_workBuf);

    uint8_t activeIndex;
    if (sector1Valid) {
        activeIndex = 1;
        // g_workBuf现在就是扇区1的内容，已经是正确的
    } else if (sector0Valid) {
        activeIndex = 0;
        // 恢复扇区0内容到g_workBuf
        flash.ReadBytes(_getAZoneSectorAddr(0), &g_workBuf, sizeof(AZone_Sector_t));
    } else {
        activeIndex = 0;
        memset(&g_workBuf, 0xFF, sizeof(AZone_Sector_t));
        flash.EraseSector(_getAZoneSectorAddr(0));
    }

    // 从A区镜像加载校准数据到RAM
    g_currentCalibData.rest_rms_mv = g_workBuf.rest_rms_mv;
    g_currentCalibData.max_rms_mv = g_workBuf.max_rms_mv;
    g_currentCalibData.rest_mdf_hz = g_workBuf.rest_mdf_hz;
    g_currentCalibData.max_mdf_hz = g_workBuf.max_mdf_hz;
    g_currentCalibData.calib_timestamp = g_workBuf.calib_timestamp;
    g_currentCalibData.calib_quality = g_workBuf.calib_quality;
    g_currentCalibData.peak_rms_mv = g_workBuf.peak_rms_mv;  // [v3.9.11]
    memcpy(g_currentCalibData.curve_coef, g_workBuf.curve_coef, sizeof(float) * 5);
    g_currentCalibData.curve_baseline_rms = g_workBuf.curve_baseline_rms;
    g_currentCalibData.curve_baseline_mdf = g_workBuf.curve_baseline_mdf;
    g_currentCalibData.matched_bcurve_id = g_workBuf.matched_bcurve_id;
    g_currentCalibData.fit_confidence = g_workBuf.fit_confidence;
    g_currentCalibData.curve_version = g_workBuf.curve_version;
    g_currentCalibData.curve_status = g_workBuf.curve_status;
    g_currentCalibData.has_curve = g_workBuf.has_curve;  // [v3.9.12]
    g_isCalibDataValid = (g_workBuf.magic == 0xA55AA55A &&
                          g_currentCalibData.calib_quality > 0);

    // 加载C区状态
    if (!_czone_loadStateFromAZone()) {
        _czone_initFromScratch();
    }

    LOG("[STORAGE] Manager Initialized. Active sector: %d, Calib valid: %s\n",
        activeIndex, g_isCalibDataValid ? "YES" : "NO");
    return 0;
}

bool StorageManager::GetPersonalCalib(PersonalCalibData_t* data) {
    if (!data || !g_isCalibDataValid)
        return false;
    *data = g_currentCalibData;
    return true;
}

// [S0-2-fix] UpdatePersonalCalib: 完整A区双缓冲写入 + CRC16校验
bool StorageManager::UpdatePersonalCalib(const PersonalCalibData_t* data) {
    if (!data) return false;

    // [栈溢出修复] 确定目标扇区，不分配任何AZone_Sector_t栈变量
    // g_workBuf 已是当前活跃扇区镜像，只需判断对面扇区状态
    // 策略：总是交替写（如果g_workBuf来自扇区0，就写扇区1，反之亦然）
    // 通过检查g_workBuf的CRC来判断它来自哪个扇区
    // 简化：Init时已记录activeIndex，这里用g_asyncAZoneTargetSector的对立面
    // 更简单：读取对面扇区的magic+header_crc16判断有效性（只需前几个字节）
    // 
    // 实际上最安全的做法：直接用g_workBuf做双缓冲判断
    // 因为g_workBuf就是当前活跃扇区的完整内容
    // 读对面扇区时临时覆盖g_workBuf来判断，然后恢复
    uint8_t targetSector;
    uint8_t activeSector = 0;  // 默认扇区0活跃
    
    if (g_workBuf.magic == 0xA55AA55A) {
        // g_workBuf当前内容是有效的，保存CRC以备恢复
        uint16_t savedCrc = g_workBuf.header_crc16;
        
        // 临时读扇区1到g_workBuf判断是否有效
        flash.ReadBytes(_getAZoneSectorAddr(1), &g_workBuf, sizeof(AZone_Sector_t));
        bool sector1Valid = _validateAZoneSector(&g_workBuf);
        
        if (sector1Valid) {
            // 扇区1有效 → 它是活跃的，恢复g_workBuf为扇区1内容（当前值）
            activeSector = 1;
        } else {
            // 扇区1无效 → 扇区0是活跃的，恢复g_workBuf
            flash.ReadBytes(_getAZoneSectorAddr(0), &g_workBuf, sizeof(AZone_Sector_t));
            activeSector = 0;
        }
    }
    targetSector = (activeSector == 0) ? 1 : 0;

    // [栈溢出修复] 直接在g_workBuf上修改，不需要栈上newSector(4096字节)
    g_workBuf.magic = 0xA55AA55A;
    g_workBuf.struct_version = PACK_VERSION(1, 1);  // [v3.9.11]
    g_workBuf.rest_rms_mv = data->rest_rms_mv;
    g_workBuf.max_rms_mv = data->max_rms_mv;
    g_workBuf.rest_mdf_hz = data->rest_mdf_hz;
    g_workBuf.max_mdf_hz = data->max_mdf_hz;
    g_workBuf.calib_timestamp = data->calib_timestamp;
    g_workBuf.calib_quality = data->calib_quality;
    g_workBuf.peak_rms_mv = data->peak_rms_mv;  // [v3.9.11]
    memcpy(g_workBuf.curve_coef, data->curve_coef, sizeof(float) * 5);
    g_workBuf.curve_baseline_rms = data->curve_baseline_rms;
    g_workBuf.curve_baseline_mdf = data->curve_baseline_mdf;
    g_workBuf.matched_bcurve_id = data->matched_bcurve_id;
    g_workBuf.fit_confidence = data->fit_confidence;
    g_workBuf.curve_version = data->curve_version;
    g_workBuf.curve_status = data->curve_status;

    // 保留C区锚点（已在g_workBuf中，无需额外操作）

    // [S0-2-fix] 计算CRC16：覆盖从 user_id 到 sys_reserved 的区域
    uint32_t payload_offset = offsetof(AZone_Sector_t, user_id);
    uint32_t payload_len = offsetof(AZone_Sector_t, reserved) - payload_offset;
    g_workBuf.header_crc16 = _calcCRC16((const uint8_t*)&g_workBuf + payload_offset,
                                         payload_len, CRC16_INIT);

    // [C0-5-fix] 异步擦除A区扇区，避免阻塞信号管线
    g_asyncAZoneTargetSector = targetSector;
    g_asyncAZonePending = true;

    g_asyncEraseState = ASYNC_ERASE_AZONE_SECTOR;
    g_asyncEraseAddr = _getAZoneSectorAddr(targetSector);
    flash.EraseSectorAsync(g_asyncEraseAddr);

    // 立即更新RAM校准缓存（乐观更新，Flash写入在tick完成）
    memcpy(&g_currentCalibData, data, sizeof(PersonalCalibData_t));
    g_isCalibDataValid = true;

    LOG("[STORAGE] Calib data: async erase sector %d started (CRC=0x%04X)\n",
        targetSector, g_workBuf.header_crc16);
    return true;
}

// ==================== B区操作实现 ====================

bool StorageManager::BZone_GetNextAvailableSlot(uint32_t* slotAddr) {
    if (!slotAddr) return false;

    // 扫描所有B区槽位，查找第一个EMPTY的
    // 每个槽位起始处有magic[4]和record_status字段
    for (uint8_t i = 0; i < B_MAX_SLOTS; i++) {
        uint32_t addr = B_ZONE_BASE_ADDR + (uint32_t)i * B_SLOT_SIZE_BYTES;
        uint8_t status = 0;
        // 读取record_status字段（偏移6字节：magic[4]+version[2]之后）
        flash.ReadBytes(addr + 6, &status, 1);
        if (status == B_SLOT_EMPTY) {
            *slotAddr = addr;
            LOG("[STORAGE] BZone: found empty slot %d at 0x%06X\n", i, addr);
            return true;
        }
    }
    LOG("[STORAGE] BZone: all %d slots occupied!\n", B_MAX_SLOTS);
    return false;
}

bool StorageManager::BZone_BeginRecord(const SubjectBasicInfo_t* subjectInfo, uint32_t slotAddr) {
    if (!subjectInfo || slotAddr < B_ZONE_BASE_ADDR) return false;
    if (g_bzone_ctx.state != B_STATE_IDLE) {
        LOG("[STORAGE] BZone: already in write state %d\n", g_bzone_ctx.state);
        return false;
    }

    // 初始化写上下文
    g_bzone_ctx.state = B_STATE_WRITING_HEADER;
    g_bzone_ctx.slot_addr = slotAddr;
    g_bzone_ctx.write_offset = 0;
    g_bzone_ctx.features_written = 0;
    g_bzone_ctx.samples_written = 0;
    g_bzone_ctx.current_phase = 0;

    // 构造记录头
    memset(&g_bzone_record_header, 0, sizeof(g_bzone_record_header));
    g_bzone_record_header.magic[0] = 'S';
    g_bzone_record_header.magic[1] = 'E';
    g_bzone_record_header.magic[2] = 'M';
    g_bzone_record_header.magic[3] = 'G';
    g_bzone_record_header.struct_version = PACK_VERSION(1, 0);
    g_bzone_record_header.record_status = B_SLOT_WRITING;
    g_bzone_record_header.subject_id = subjectInfo->subject_id;
    g_bzone_record_header.age = subjectInfo->age;
    g_bzone_record_header.gender = subjectInfo->gender;
    g_bzone_record_header.handedness = subjectInfo->handedness;
    g_bzone_record_header.test_timestamp = millis() / 1000;

    // 快照当前校准数据到header
    if (g_isCalibDataValid) {
        g_bzone_record_header.snap_rest_rms_mv = g_currentCalibData.rest_rms_mv;
        g_bzone_record_header.snap_max_rms_mv = g_currentCalibData.max_rms_mv;
        g_bzone_record_header.snap_rest_mdf_hz = g_currentCalibData.rest_mdf_hz;
        g_bzone_record_header.snap_max_mdf_hz = g_currentCalibData.max_mdf_hz;
        g_bzone_record_header.snap_calib_quality = g_currentCalibData.calib_quality;
    }

    // [C0-4-fix] 异步擦除槽位，避免阻塞信号管线
    // 先发起第一个扇区擦除，后续在tick()中逐扇区推进
    g_asyncEraseState = ASYNC_ERASE_BZONE_SLOT;
    g_asyncEraseProgress = 0;
    g_asyncEraseAddr = slotAddr;
    g_asyncEraseEndAddr = slotAddr + (uint32_t)B_SLOT_SECTORS * SECTOR_SIZE;
    flash.EraseSectorAsync(slotAddr);  // 发起第一个扇区擦除

    g_bzone_ctx.state = B_STATE_ERASING;
    g_bzone_ctx.erase_progress = 1;  // 已发起第1个扇区

    LOG("[STORAGE] BZone: async erase started at 0x%06X (%d sectors)\n",
        slotAddr, B_SLOT_SECTORS);
    return true;
}

bool StorageManager::BZone_AppendFeatureSequence(const uint16_t* rmsArray, const uint16_t* mdfArray, uint16_t count) {
    if (!rmsArray || !mdfArray || count == 0) return false;
    if (g_bzone_ctx.state != B_STATE_WRITING_FEATURES) {
        LOG("[STORAGE] BZone: not in feature-write state\n");
        return false;
    }

    // RMS序列（每个值2字节）
    if (!_bzone_stream_write(rmsArray, (uint32_t)count * sizeof(uint16_t))) {
        LOG("[STORAGE] BZone: RMS write failed\n");
        return false;
    }
    // MDF序列（每个值2字节）
    if (!_bzone_stream_write(mdfArray, (uint32_t)count * sizeof(uint16_t))) {
        LOG("[STORAGE] BZone: MDF write failed\n");
        return false;
    }

    g_bzone_ctx.features_written += count;
    return true;
}

bool StorageManager::BZone_AppendFeaturePoint(uint16_t rmsCompressed, uint16_t mdfCompressed) {
    if (g_bzone_ctx.state != B_STATE_WRITING_FEATURES &&
        g_bzone_ctx.state != B_STATE_WRITING_RAW_WAVE) {
        LOG("[STORAGE] BZone_AppendFeaturePoint: not in feature-write state (state=%d)\n",
            g_bzone_ctx.state);
        return false;
    }

    g_bzone_ctx.state = B_STATE_WRITING_FEATURES;

    // RMS 和 MDF 各2字节，直接写入
    static uint16_t s_singleRms[1];
    static uint16_t s_singleMdf[1];
    s_singleRms[0] = rmsCompressed;
    s_singleMdf[0] = mdfCompressed;

    if (!_bzone_stream_write(s_singleRms, sizeof(uint16_t))) return false;
    if (!_bzone_stream_write(s_singleMdf, sizeof(uint16_t))) return false;

    g_bzone_ctx.features_written += 1;
    return true;
}

uint16_t StorageManager::BZone_GetFeatureCount() {
    return g_bzone_ctx.features_written;
}

bool StorageManager::BZone_MarkFeaturePoint(uint8_t markIndex) {
    if (markIndex >= 4) return false;
    if (g_bzone_ctx.state != B_STATE_WRITING_FEATURES &&
        g_bzone_ctx.state != B_STATE_WRITING_RAW_WAVE) {
        return false;
    }

    // header偏移计算：magic(4)+struct_version(2)+record_status(1)+reserved_head(57)+
    // subject_id(4)+age(1)+gender(1)+handedness(1)+data_quality(1)+test_timestamp(4)+
    // test_duration_sec(2)+snap_rest_rms(4)+snap_max_rms(4)+snap_rest_mdf(4)+snap_max_mdf(4)+
    // snap_calib_quality(1) = 90 bytes offset to mark_feature_idx
    uint32_t markOffset = g_bzone_ctx.slot_addr + 90 + markIndex * 2;
    uint16_t featureIdx = g_bzone_ctx.features_written;

    if (!_writeMultiPage(markOffset, (uint8_t*)&featureIdx, sizeof(uint16_t))) {
        LOG("[STORAGE] BZone_MarkFeaturePoint: write failed at 0x%06X\n", markOffset);
        return false;
    }

    // 更新mark_count
    uint8_t newCount = markIndex + 1;
    uint32_t countOffset = g_bzone_ctx.slot_addr + 90 + 8; // mark_feature_idx[4]=8B后
    if (!_writeMultiPage(countOffset, &newCount, 1)) {
        LOG("[STORAGE] BZone_MarkFeaturePoint: mark_count write failed\n");
        return false;
    }

    LOG("[STORAGE] BZone_MarkFeaturePoint: mark[%d]=%d (at offset 0x%06X)\n",
        markIndex, featureIdx, markOffset);
    return true;
}

bool StorageManager::BZone_AppendRawWaveform(const int16_t* phase1, const int16_t* phase2,
                                              const int16_t* phase3, const int16_t* phase4,
                                              uint16_t samplesPerPhase) {
    if (!phase1 || !phase2 || !phase3 || !phase4 || samplesPerPhase == 0) return false;
    if (g_bzone_ctx.state != B_STATE_WRITING_FEATURES &&
        g_bzone_ctx.state != B_STATE_WRITING_RAW_WAVE) {
        LOG("[STORAGE] BZone: not in wave-write state\n");
        return false;
    }

    g_bzone_ctx.state = B_STATE_WRITING_RAW_WAVE;

    // 4个phase连续写入
    const int16_t* phases[] = { phase1, phase2, phase3, phase4 };
    for (uint8_t p = 0; p < 4; p++) {
        if (!_bzone_stream_write(phases[p], (uint32_t)samplesPerPhase * sizeof(int16_t))) {
            LOG("[STORAGE] BZone: phase%d write failed\n", p + 1);
            return false;
        }
    }

    g_bzone_ctx.samples_written += samplesPerPhase * 4;
    return true;
}

bool StorageManager::BZone_AppendRawPhase(uint8_t phaseIndex, const int16_t* samples, uint16_t count) {
    if (!samples || count == 0 || phaseIndex < 1 || phaseIndex > 4) return false;
    if (g_bzone_ctx.state != B_STATE_WRITING_FEATURES &&
        g_bzone_ctx.state != B_STATE_WRITING_RAW_WAVE) {
        LOG("[STORAGE] BZone_AppendRawPhase: not in wave-write state (state=%d)\n",
            g_bzone_ctx.state);
        return false;
    }

    g_bzone_ctx.state = B_STATE_WRITING_RAW_WAVE;

    // 【P0-Fix】Caller 侧已分配缓冲区，直接传递样本指针写入 Flash
    // 不再使用 static 内部缓冲区，节省 6KB RAM
    uint16_t writeCount = (count > 3000) ? 3000 : count;
    if (!_bzone_stream_write((const uint8_t*)samples, (uint32_t)writeCount * sizeof(int16_t))) {
        LOG("[STORAGE] BZone_AppendRawPhase: phase%d write failed\n", phaseIndex);
        return false;
    }

    g_bzone_ctx.samples_written += writeCount;
    return true;
}

bool StorageManager::BZone_EndRecord() {
    if (g_bzone_ctx.state == B_STATE_IDLE) return false;

    // 写入footer并提交
    bool ok = _bzone_write_footer_and_commit();
    if (ok) {
        LOG("[STORAGE] BZone: record committed at 0x%06X, features=%d, samples=%d\n",
            g_bzone_ctx.slot_addr, g_bzone_ctx.features_written, g_bzone_ctx.samples_written);
    }

    g_bzone_ctx.state = B_STATE_IDLE;
    return ok;
}

// ==================== C区操作实现 ====================

bool StorageManager::CZone_AppendDataPoint(const CZone_DataPoint_t* dataPoint) {
    if (!dataPoint) return false;

    // [C0-5-fix] 异步擦除期间拒绝写入，避免数据丢失
    if (g_asyncEraseState == ASYNC_ERASE_CZONE_BLOCK) {
        return false;  // 擦除进行中，丢弃本帧（信号管线不阻塞）
    }

    // 检查当前块是否已满
    // C区64KB块: header 32字节 + 最多 (65536-32)/12 = 5458 个数据点
    static const uint16_t C_MAX_POINTS_PER_BLOCK = 5458;

    if (!g_czone_initialized || g_czone_current_block_addr == 0) {
        if (!_czone_allocateNewBlock()) {
            LOG("[STORAGE] CZone: failed to allocate new block\n");
            return false;
        }
    }

    // 检查块内偏移是否超出（header 32字节 + N*12字节）
    uint16_t dataOffset = g_czone_block_write_offset;
    uint16_t pointIndex = (dataOffset - sizeof(CZone_BlockHeader_t)) / sizeof(CZone_DataPoint_t);

    if (pointIndex >= C_MAX_POINTS_PER_BLOCK) {
        // 当前块已满，提交并分配新块
        // 更新块头的num_points和CRC
        _czone_flushCache();  // 先刷cache
        _czone_finalizeCurrentBlock();
        if (!_czone_allocateNewBlock()) {
            return false;
        }
        pointIndex = 0;
    }

    // 将数据点写入RAM cache
    uint16_t cacheRemaining = 256 - g_czone_cache_write_pos;
    uint16_t pointSize = sizeof(CZone_DataPoint_t);

    if (g_czone_cache_write_pos + pointSize > 256) {
        // cache满了，先刷到Flash
        if (!_czone_flushCache()) return false;
    }

    memcpy(g_czone_ram_cache + g_czone_cache_write_pos, dataPoint, pointSize);
    g_czone_cache_write_pos += pointSize;
    g_czone_block_write_offset += pointSize;

    // cache接近满时自动刷盘（剩余不足一个数据点时）
    if (g_czone_cache_write_pos + pointSize > 256) {
        _czone_flushCache();
    }

    return true;
}

// [B2-2-fix][C0-4/C0-5-fix] StorageManager::tick - 异步擦除状态机轮询
void StorageManager::tick() {
    if (g_asyncEraseState == ASYNC_ERASE_IDLE) return;

    // Flash正在忙（擦除进行中），等下次tick
    if (flash.isBusy()) return;

    switch (g_asyncEraseState) {

    // ---- A区扇区擦除完成 ----
    case ASYNC_ERASE_AZONE_SECTOR: {
        if (!g_asyncAZonePending) {
            g_asyncEraseState = ASYNC_ERASE_IDLE;
            break;
        }
        // 写入新扇区数据
        uint32_t addr_w = _getAZoneSectorAddr(g_asyncAZoneTargetSector);
        // [DEBUG] 写入前验证SPI通信：写1字节再回读
        {
            uint8_t testByte = 0xDE;
            flash.WritePage(addr_w, &testByte, 1);
            uint8_t readBack = 0;
            flash.readData(addr_w, &readBack, 1);
            LOG("[STORAGE] DBG SPI test: write 0xDE, readback 0x%02X, addr=0x%06X\n", readBack, addr_w);
        }
        if (!_writeMultiPage(addr_w, &g_workBuf, sizeof(AZone_Sector_t))) {
            LOG("[STORAGE] tick: AZone write failed!\n");
            g_asyncEraseState = ASYNC_ERASE_IDLE;
            g_asyncAZonePending = false;
            break;
        }
        // 回读验证CRC（复用g_workBuf，避免栈上4KB分配）
        // 注意：这会覆盖g_workBuf内容，但此时写入已完成，不再需要旧数据
        flash.ReadBytes(addr_w, &g_workBuf, sizeof(AZone_Sector_t));
        LOG("[STORAGE] DBG readback: magic=0x%08X, crc=0x%04X, ver=0x%04X, rest_rms=%.2f\n",
            g_workBuf.magic, g_workBuf.header_crc16, g_workBuf.struct_version, g_workBuf.rest_rms_mv);
        if (!_validateAZoneSector(&g_workBuf)) {
            LOG("[STORAGE] tick: AZone CRC verify failed!\n");
        }
        g_asyncAZonePending = false;
        g_asyncEraseState = ASYNC_ERASE_IDLE;
        LOG("[STORAGE] tick: AZone sector %d written and verified\n", g_asyncAZoneTargetSector);
        break;
    }

    // ---- B区槽位逐扇区擦除 ----
    case ASYNC_ERASE_BZONE_SLOT: {
        // 上一个扇区擦除完成，推进下一个
        g_asyncEraseAddr += SECTOR_SIZE;
        g_bzone_ctx.erase_progress++;

        if (g_asyncEraseAddr < g_asyncEraseEndAddr) {
            // 还有扇区要擦，发起下一个
            flash.EraseSectorAsync(g_asyncEraseAddr);
        } else {
            // 全部扇区擦完，写入header，进入写特征状态
            _bzone_writeHeaderToSlot(g_bzone_ctx.slot_addr, &g_bzone_record_header);
            g_bzone_ctx.write_offset = sizeof(BZone_Header_Payload_t);
            g_bzone_ctx.state = B_STATE_WRITING_FEATURES;
            g_asyncEraseState = ASYNC_ERASE_IDLE;
            LOG("[STORAGE] tick: BZone erase done, header written at 0x%06X\n",
                g_bzone_ctx.slot_addr);
        }
        break;
    }

    // ---- C区64KB块擦除完成 ----
    case ASYNC_ERASE_CZONE_BLOCK: {
        // 写块头
        CZone_BlockHeader_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        hdr.magic = 0x435A424C;  // "CZBL"
        hdr.start_timestamp = millis() / 1000;
        hdr.num_points = 0;
        hdr.block_status = 1;  // 活跃

        uint16_t hdr_crc = _calcCRC16((const uint8_t*)&hdr,
                                       offsetof(CZone_BlockHeader_t, block_crc16),
                                       CRC16_INIT);
        hdr.block_crc16 = hdr_crc;

        _writeMultiPage(g_asyncEraseAddr, &hdr, sizeof(hdr));
        g_asyncEraseState = ASYNC_ERASE_IDLE;
        LOG("[STORAGE] tick: CZone block header written at 0x%06X\n", g_asyncEraseAddr);
        break;
    }

    default:
        g_asyncEraseState = ASYNC_ERASE_IDLE;
        break;
    }
}

// ==================== C区查询接口实现 ====================

bool StorageManager::CZone_QueryByTimeRange(uint32_t startTs, uint32_t endTs,
                                             CZone_DataPoint_t* outBuf, uint16_t maxPoints,
                                             uint16_t* outCount, uint32_t* outNextTs) {
    if (!outBuf || !outCount || !outNextTs || maxPoints == 0) return false;
    
    *outCount = 0;
    *outNextTs = 0;
    
    // 遍历 C 区所有 64KB 块
    for (uint32_t blockAddr = C_ZONE_BASE_ADDR; 
         blockAddr < C_ZONE_BASE_ADDR + C_ZONE_TOTAL_SIZE; 
         blockAddr += C_BLOCK_SIZE) {
        
        // 读块头
        CZone_BlockHeader_t hdr;
        flash.ReadBytes(blockAddr, &hdr, sizeof(hdr));
        
        // 跳过无效块
        if (hdr.magic != 0x435A424C || hdr.block_status == 0) continue;
        
        // 计算块内数据点数量
        uint16_t numPoints = hdr.num_points;
        if (numPoints == 0) continue;
        
        // 计算块的基准时间戳（秒级）
        uint32_t blockBaseTs = hdr.start_timestamp;
        
        // 遍历块内数据点
        for (uint16_t i = 0; i < numPoints && *outCount < maxPoints; i++) {
            CZone_DataPoint_t pt;
            uint32_t ptAddr = blockAddr + sizeof(CZone_BlockHeader_t) + i * sizeof(CZone_DataPoint_t);
            flash.ReadBytes(ptAddr, &pt, sizeof(pt));
            
            // 计算绝对时间戳（秒级）
            uint32_t ptTsSec = blockBaseTs + (pt.timestamp_ms / 1000);
            
            // 时间范围过滤
            if (ptTsSec >= startTs && ptTsSec <= endTs) {
                outBuf[*outCount] = pt;
                (*outCount)++;
            }
        }
        
        // 如果已满，记录下一个块地址用于分页
        if (*outCount >= maxPoints) {
            uint32_t nextBlockAddr = blockAddr + C_BLOCK_SIZE;
            if (nextBlockAddr < C_ZONE_BASE_ADDR + C_ZONE_TOTAL_SIZE) {
                *outNextTs = startTs;  // 继续从当前时间戳查
            }
            break;
        }
    }
    
    return (*outCount > 0);
}

bool StorageManager::BZone_ListRecords(BZone_ListEntry_t* outEntries, uint8_t maxEntries, uint8_t* outCount) {
    if (!outEntries || !outCount || maxEntries == 0) return false;
    
    *outCount = 0;
    
    for (uint8_t slot = 0; slot < B_MAX_SLOTS && *outCount < maxEntries; slot++) {
        uint32_t slotAddr = B_ZONE_BASE_ADDR + (uint32_t)slot * B_SLOT_SIZE_BYTES;
        
        // 读 header 前 64 字节
        struct {
            char magic[4];
            uint16_t version;
            uint8_t status;
            uint8_t reserved[57];
            uint32_t subject_id;
            uint32_t test_timestamp;
            uint16_t test_duration_sec;
            uint8_t age;
            uint8_t gender;
            uint8_t handedness;
            float snap_rest_rms;
            float snap_max_rms;
            float snap_rest_mdf;
            float snap_max_mdf;
            uint8_t snap_quality;
            char name[24];
        } hdr;
        
        flash.ReadBytes(slotAddr, &hdr, sizeof(hdr));
        
        // 跳过空槽位
        if (hdr.magic[0] != 'S' || hdr.magic[1] != 'E' || 
            hdr.magic[2] != 'M' || hdr.magic[3] != 'G') {
            continue;
        }
        
        // 只返回有效记录（VALID 或 WRITING）
        if (hdr.status != B_SLOT_VALID && hdr.status != B_SLOT_WRITING) {
            continue;
        }
        
        // 填充输出结构
        BZone_ListEntry_t* entry = &outEntries[*outCount];
        entry->slot_addr = slotAddr;
        entry->record_status = hdr.status;
        entry->subject_id = hdr.subject_id;
        entry->test_timestamp = hdr.test_timestamp;
        entry->test_duration_sec = hdr.test_duration_sec;
        entry->age = hdr.age;
        entry->gender = hdr.gender;
        entry->handedness = hdr.handedness;
        entry->snap_rest_rms_mv = hdr.snap_rest_rms;
        entry->snap_max_rms_mv = hdr.snap_max_rms;
        entry->snap_rest_mdf_hz = hdr.snap_rest_mdf;
        entry->snap_max_mdf_hz = hdr.snap_max_mdf;
        entry->snap_calib_quality = hdr.snap_quality;
        memcpy(entry->name, hdr.name, 24);
        entry->name[23] = '\0';
        
        (*outCount)++;
    }
    
    return (*outCount > 0);
}

bool StorageManager::BZone_DeleteSlot(uint32_t slotAddr) {
    // 参数校验：地址必须在 B 区范围内
    if (slotAddr < B_ZONE_BASE_ADDR || slotAddr > B_ZONE_END_ADDR) {
        LOG("[STORAGE] BZone_DeleteSlot: invalid slot address 0x%06X\n", slotAddr);
        return false;
    }
    
    // 检查槽位对齐
    if ((slotAddr - B_ZONE_BASE_ADDR) % B_SLOT_SIZE_BYTES != 0) {
        LOG("[STORAGE] BZone_DeleteSlot: slot not aligned 0x%06X\n", slotAddr);
        return false;
    }
    
    // 读取槽位状态，检查是否为空
    uint8_t status = 0;
    flash.ReadBytes(slotAddr + 6, &status, 1);  // offset 6 = record_status 字段
    
    if (status == B_SLOT_EMPTY) {
        LOG("[STORAGE] BZone_DeleteSlot: slot already empty at 0x%06X\n", slotAddr);
        return false;  // 已空，删除失败
    }
    
    // 【删除策略】将槽位状态标记为 EMPTY
    // Flash 只能从 0xFF 擦到 0x00（单向），所以这里不能直接写 EMPTY(0xFF)
    // 必须异步擦除整个槽位
    
    // 槽位索引
    uint8_t slotIndex = (slotAddr - B_ZONE_BASE_ADDR) / B_SLOT_SIZE_BYTES;
    LOG("[STORAGE] BZone_DeleteSlot: deleting slot %d at 0x%06X\n", slotIndex, slotAddr);
    
    // 启动异步擦除：逐扇区擦除槽位
    g_asyncEraseState = ASYNC_ERASE_BZONE_SLOT;
    g_asyncEraseProgress = 0;
    g_asyncEraseAddr = slotAddr;
    g_asyncEraseEndAddr = slotAddr + (uint32_t)B_SLOT_SECTORS * SECTOR_SIZE;
    flash.EraseSectorAsync(slotAddr);  // 发起第一个扇区擦除
    
    // 注意：异步擦除在 tick() 中继续，完成后槽位自动变为 0xFF (EMPTY)
    // 调用者应通过响应确认删除结果
    
    return true;  // 已发起删除，结果在 tick() 中完成
}

// ==================== WiFi 操作（RA4M1 板载 Data Flash / EEPROM） ====================

bool StorageManager::LoadWifiCredentials(WifiCredentials_t* outCreds) {
    if (!outCreds) return false;

    uint8_t magic = EEPROM.read(EEPROM_WIFI_VALID_ADDR);
    if (magic != EEPROM_WIFI_MAGIC) {
        outCreds->isValid = false;
        return false;
    }

    for (uint8_t i = 0; i < 32; i++) {
        outCreds->ssid[i] = (char)EEPROM.read(EEPROM_WIFI_SSID_ADDR + i);
    }
    outCreds->ssid[31] = '\0';

    for (uint8_t i = 0; i < 64; i++) {
        outCreds->pass[i] = (char)EEPROM.read(EEPROM_WIFI_PASS_ADDR + i);
    }
    outCreds->pass[63] = '\0';

    outCreds->isValid = (strlen(outCreds->ssid) > 0);
    LOG("[STORAGE] WiFi creds loaded from EEPROM: SSID='%s'\n", outCreds->ssid);
    return outCreds->isValid;
}

bool StorageManager::SaveWifiCredentials(const WifiCredentials_t* creds) {
    if (!creds) return false;

    // EEPROM.update 只在值变化时写入，减少磨损
    // RA4M1 Data Flash 擦写寿命 > 100,000次
    for (uint8_t i = 0; i < 32; i++) {
        EEPROM.update(EEPROM_WIFI_SSID_ADDR + i, (uint8_t)creds->ssid[i]);
    }
    for (uint8_t i = 0; i < 64; i++) {
        EEPROM.update(EEPROM_WIFI_PASS_ADDR + i, (uint8_t)creds->pass[i]);
    }
    EEPROM.update(EEPROM_WIFI_VALID_ADDR, EEPROM_WIFI_MAGIC);

    LOG("[STORAGE] WiFi creds saved to EEPROM: SSID='%s'\n", creds->ssid);
    return true;
}

// ==================== 私有辅助函数实现 ====================

// [S0-2-fix] CRC16-Modbus 实现（完整，非空壳）
static uint16_t _calcCRC16(const uint8_t* data, uint32_t length, uint16_t init) {
    uint16_t crc = init;
    for (uint32_t i = 0; i < length; i++) {
        crc ^= data[i];
        for (uint8_t j = 0; j < 8; j++) {
            if (crc & 1) crc = (crc >> 1) ^ CRC16_POLYNOMIAL;
            else crc >>= 1;
        }
    }
    return crc;
}

// [S0-2-fix] A区扇区校验（完整，非空壳）
static bool _validateAZoneSector(const AZone_Sector_t* sector) {
    if (!sector) return false;
    if (sector->magic != 0xA55AA55A) return false;
    // 校验CRC：从 user_id 到 sys_reserved（不含 reserved 空闲区）
    uint32_t payload_offset = offsetof(AZone_Sector_t, user_id);
    uint32_t payload_len = offsetof(AZone_Sector_t, reserved) - payload_offset;
    uint16_t calc_crc = _calcCRC16((const uint8_t*)sector + payload_offset, payload_len, CRC16_INIT);
    if (calc_crc != sector->header_crc16) {
        LOG("[STORAGE] AZone CRC mismatch: calc=0x%04X, stored=0x%04X\n", calc_crc, sector->header_crc16);
        return false;
    }
    return true;
}

static uint32_t _getAZoneSectorAddr(uint8_t index) {
    return ZONE_A_BASE_ADDR + (uint32_t)index * SECTOR_SIZE;
}

static bool _writeMultiPage(uint32_t addr, const void* data, uint32_t len) {
    const uint8_t* p = (const uint8_t*)data;
    uint32_t offset = 0;
    while (offset < len) {
        // W25Q128FV: 页内对齐写入，每页最多256字节
        uint32_t page_remaining = PAGE_SIZE - ((addr + offset) % PAGE_SIZE);
        uint32_t chunk = (len - offset < page_remaining) ? (len - offset) : page_remaining;
        if (chunk > PAGE_SIZE) chunk = PAGE_SIZE;
        if (!flash.WritePage(addr + offset, p + offset, chunk)) {
            LOG("[STORAGE] WritePage failed at 0x%06X\n", addr + offset);
            return false;
        }
        offset += chunk;
    }
    return true;
}

// ---- C区私有函数实现 ----

static bool _czone_initFromScratch(void) {
    // 分配第一个C区64KB块
    g_czone_current_block_addr = C_ZONE_BASE_ADDR;
    g_czone_block_write_offset = sizeof(CZone_BlockHeader_t);
    g_czone_cache_write_pos = 0;
    g_czone_initialized = true;

    // [C0-5-fix] 异步擦除第一个块（W25Q128FV: 64KB块擦除 200-1000ms）
    g_asyncEraseState = ASYNC_ERASE_CZONE_BLOCK;
    g_asyncEraseAddr = g_czone_current_block_addr;
    flash.EraseBlock64KAsync(g_czone_current_block_addr);

    LOG("[STORAGE] CZone: async erase first block at 0x%06X\n", g_czone_current_block_addr);
    return true;
}

static bool _czone_loadStateFromAZone(void) {
    // 从A区镜像读取C区锚点信息
    if (g_workBuf.magic != 0xA55AA55A) return false;
    if (g_workBuf.cz_active_block_addr == 0 ||
        g_workBuf.cz_active_block_addr < C_ZONE_BASE_ADDR) {
        return false;
    }

    // 验证C区块头
    CZone_BlockHeader_t hdr;
    flash.ReadBytes(g_workBuf.cz_active_block_addr, &hdr, sizeof(hdr));
    if (hdr.magic != 0x435A424C) return false;

    g_czone_current_block_addr = g_workBuf.cz_active_block_addr;
    g_czone_block_write_offset = g_workBuf.cz_write_offset;
    g_czone_initialized = true;
    g_czone_cache_write_pos = 0;

    LOG("[STORAGE] CZone: loaded state from AZone, block=0x%06X, offset=%u\n",
        g_czone_current_block_addr, g_czone_block_write_offset);
    return true;
}

static bool _czone_flushCache(void) {
    if (g_czone_cache_write_pos == 0) return true;

    uint32_t writeAddr = g_czone_current_block_addr + g_czone_block_write_offset - g_czone_cache_write_pos;

    if (!_writeMultiPage(writeAddr, g_czone_ram_cache, g_czone_cache_write_pos)) {
        LOG("[STORAGE] CZone: cache flush failed at 0x%06X\n", writeAddr);
        return false;
    }

    g_czone_cache_write_pos = 0;
    return true;
}

static bool _czone_allocateNewBlock(void) {
    // 计算下一个64KB块地址
    uint32_t nextAddr = g_czone_current_block_addr + C_BLOCK_SIZE;

    // 检查是否超出C区范围
    if (nextAddr + C_BLOCK_SIZE > FLASH_END_ADDR) {
        nextAddr = C_ZONE_BASE_ADDR;
        LOG("[STORAGE] CZone: wrap-around to beginning\n");
    }

    // [C0-5-fix] 异步擦除新块
    g_asyncEraseState = ASYNC_ERASE_CZONE_BLOCK;
    g_asyncEraseAddr = nextAddr;
    flash.EraseBlock64KAsync(nextAddr);

    // 预设新块地址（tick完成后写入header）
    g_czone_current_block_addr = nextAddr;
    g_czone_block_write_offset = sizeof(CZone_BlockHeader_t);
    g_czone_cache_write_pos = 0;

    // 乐观更新A区锚点
    g_workBuf.cz_active_block_addr = nextAddr;
    g_workBuf.cz_write_offset = g_czone_block_write_offset;
    g_workBuf.cz_wrap_around = (nextAddr == C_ZONE_BASE_ADDR) ? 1 : 0;

    LOG("[STORAGE] CZone: async erase new block at 0x%06X\n", nextAddr);
    return true;
}

static void _czone_finalizeCurrentBlock(void) {
    // 更新当前块的header: num_points和CRC
    CZone_BlockHeader_t hdr;
    flash.ReadBytes(g_czone_current_block_addr, &hdr, sizeof(hdr));

    hdr.num_points = (g_czone_block_write_offset - sizeof(CZone_BlockHeader_t)) / sizeof(CZone_DataPoint_t);
    hdr.block_status = 2;  // 已完成

    // 计算整个块数据的CRC（header不含CRC字段 + 数据区域）
    // 先算header部分的CRC
    uint16_t hdr_crc = _calcCRC16((const uint8_t*)&hdr,
                                   offsetof(CZone_BlockHeader_t, block_crc16),
                                   CRC16_INIT);
    hdr.block_crc16 = hdr_crc;

    // 重写header（在已擦除的块上，需要先读-改-写，但这里header是块开头，单独写一页即可）
    _writeMultiPage(g_czone_current_block_addr, &hdr, sizeof(hdr));
}

// ---- B区私有函数实现 ----

static bool _bzone_stream_write(const void* data, uint32_t len) {
    if (!data || len == 0) return false;

    uint32_t writeAddr = g_bzone_ctx.slot_addr + g_bzone_ctx.write_offset;

    // 检查写入是否超出槽位范围（40KB）
    if (g_bzone_ctx.write_offset + len > B_SLOT_SIZE_BYTES) {
        LOG("[STORAGE] BZone: write overflow! offset=%lu + len=%lu > slot=%lu\n",
            (unsigned long)g_bzone_ctx.write_offset, (unsigned long)len,
            (unsigned long)B_SLOT_SIZE_BYTES);
        return false;
    }

    if (!_writeMultiPage(writeAddr, data, len)) {
        LOG("[STORAGE] BZone: stream write failed at 0x%06X\n", writeAddr);
        return false;
    }

    g_bzone_ctx.write_offset += len;
    return true;
}

static bool _bzone_write_footer_and_commit(void) {
    // 计算整个槽位数据的CRC16（跳过头部前8字节magic+version+status）
    // 由于W25Q128FV读取需要逐页，这里计算已写入部分的CRC
    // [C0-6-fix] 使用专用工作缓冲区 g_workBuf 做CRC计算，不再破坏A区镜像
    uint16_t crc = CRC16_INIT;
    uint32_t totalLen = g_bzone_ctx.write_offset;
    uint32_t offset = 0;

    while (offset < totalLen) {
        uint32_t chunkLen = totalLen - offset;
        if (chunkLen > sizeof(AZone_Sector_t)) chunkLen = sizeof(AZone_Sector_t);
        flash.ReadBytes(g_bzone_ctx.slot_addr + offset, &g_workBuf, chunkLen);

        // 第一页跳过magic(4)+version(2)+status(1)+reserved(57) = 64字节头部
        uint32_t startByte = (offset == 0) ? 64 : 0;
        if (chunkLen > startByte) {
            crc = _calcCRC16((const uint8_t*)&g_workBuf + startByte,
                             chunkLen - startByte, crc);
        }
        offset += chunkLen;
    }

    // 写入footer: data_crc16 + commit_marker
    // BZone_Record_t尾部: data_crc16(2) + commit_marker(4) + tail_reserved(6)
    uint32_t footerAddr = g_bzone_ctx.slot_addr + B_SLOT_SIZE_BYTES - 12;
    uint8_t footer[12];
    memset(footer, 0, sizeof(footer));
    footer[0] = crc & 0xFF;
    footer[1] = (crc >> 8) & 0xFF;
    // commit_marker = 0xAA55AA55
    footer[2] = 0x55; footer[3] = 0xAA; footer[4] = 0x55; footer[5] = 0xAA;

    if (!_writeMultiPage(footerAddr, footer, 12)) {
        LOG("[STORAGE] BZone: footer write failed\n");
        return false;
    }

    // 更新header的record_status为VALID
    uint8_t validStatus = B_SLOT_VALID;
    if (!flash.WritePage(g_bzone_ctx.slot_addr + 6, &validStatus, 1)) {
        LOG("[STORAGE] BZone: status update failed\n");
        return false;
    }

    LOG("[STORAGE] BZone: footer committed, CRC=0x%04X\n", crc);
    return true;
}

static void _bzone_writeHeaderToSlot(uint32_t slotAddr, const BZone_Header_Payload_t* hdr) {
    _writeMultiPage(slotAddr, hdr, sizeof(BZone_Header_Payload_t));
}


// ==================== B区曲线库接口实现 ====================

static uint32_t _getCurveLibraryAddr(void) {
    return B_ZONE_BASE_ADDR + (uint32_t)B_MAX_SLOTS * B_SLOT_SIZE_BYTES;
}

bool StorageManager::BZone_ListCurves(uint8_t gender, uint8_t handedness, uint8_t age,
                                     BZone_CurveEntry_t* outEntries, uint8_t maxEntries,
                                     uint8_t* outCount) {
    if (!outEntries || !outCount || maxEntries == 0) return false;
    *outCount = 0;

    uint32_t libAddr = _getCurveLibraryAddr();
    BZone_CurveLibrary_t lib;
    flash.ReadBytes(libAddr, &lib, sizeof(lib));

    // 简单 magic 校验
    if (lib.library_version == 0xFFFF) {
        LOG("[STORAGE] CurveLibrary: uninitialized\n");
        return false;
    }

    for (uint8_t i = 0; i < 63 && *outCount < maxEntries; i++) {
        const auto& c = lib.curves[i];

        // 跳过空槽或需刷新槽
        if (c.curve_status == 0x00 || c.active_slot_count == 0) continue;

        // 条件过滤：性别
        if (gender != 0 && c.gender_hint != 0 && c.gender_hint != gender) continue;
        // 条件过滤：左右手
        if (handedness != 0 && c.handedness_hint != 0 && c.handedness_hint != handedness) continue;
        // 条件过滤：年龄段
        if (c.age_min > 0 && c.age_max > 0 && (age < c.age_min || age > c.age_max)) continue;

        BZone_CurveEntry_t* e = &outEntries[*outCount];
        e->curve_id = c.curve_id;
        e->curve_type = c.curve_type;
        memcpy(e->coefficients, c.coefficients, sizeof(float) * 5);
        e->group_baseline_rms = c.coefficients[0];       // 系数0 = RMS基准
        e->group_baseline_mdf  = c.coefficients[1];      // 系数1 = MDF基准
        e->gender_hint = c.gender_hint;
        e->age_min = c.age_min;
        e->age_max = c.age_max;
        e->handedness_hint = c.handedness_hint;
        e->active_slot_count = c.active_slot_count;
        e->curve_status = c.curve_status;
        e->fit_quality = c.fit_quality;
        memset(e->description, 0, sizeof(e->description));

        (*outCount)++;
    }

    LOG("[STORAGE] BZone_ListCurves: found %d matching curves\n", *outCount);
    return (*outCount > 0);
}

bool StorageManager::BZone_GetCurve(uint8_t curveId, BZone_CurveEntry_t* outCurve) {
    if (!outCurve) return false;

    uint32_t libAddr = _getCurveLibraryAddr();
    BZone_CurveLibrary_t lib;
    flash.ReadBytes(libAddr, &lib, sizeof(lib));

    for (uint8_t i = 0; i < 63; i++) {
        const auto& c = lib.curves[i];
        if (c.curve_id == curveId && c.curve_status == 0x01) {
            outCurve->curve_id = c.curve_id;
            outCurve->curve_type = c.curve_type;
            memcpy(outCurve->coefficients, c.coefficients, sizeof(float) * 5);
            outCurve->group_baseline_rms = c.coefficients[0];
            outCurve->group_baseline_mdf  = c.coefficients[1];
            outCurve->gender_hint = c.gender_hint;
            outCurve->age_min = c.age_min;
            outCurve->age_max = c.age_max;
            outCurve->handedness_hint = c.handedness_hint;
            outCurve->active_slot_count = c.active_slot_count;
            outCurve->curve_status = c.curve_status;
            outCurve->fit_quality = c.fit_quality;
            memset(outCurve->description, 0, sizeof(outCurve->description));
            LOG("[STORAGE] BZone_GetCurve: found id=%d\n", curveId);
            return true;
        }
    }
    LOG("[STORAGE] BZone_GetCurve: not found id=%d\n", curveId);
    return false;
}

// 方案A个人曲线生成：平移群体曲线到个人基准
// 公式: y_personal(x) = y_group(x) × (personal_baseline / group_baseline)
// 这里系数平移: coef[i]_personal = coef[i]_group × (personal_baseline / group_baseline)
bool StorageManager::GeneratePersonalCurve(uint8_t bcurve_id,
                                          const float personal_baseline_rms,
                                          const float personal_baseline_mdf) {
    if (!g_isCalibDataValid) {
        LOG("[STORAGE] GeneratePersonalCurve: no calib data\n");
        return false;
    }

    BZone_CurveEntry_t curve;
    if (!BZone_GetCurve(bcurve_id, &curve)) {
        return false;
    }

    // 计算 RMS 基准比值
    float rms_ratio = (curve.group_baseline_rms > 0.001f)
                      ? (personal_baseline_rms / curve.group_baseline_rms)
                      : 1.0f;
    rms_ratio = constrain(rms_ratio, 0.5f, 2.0f);  // 限制±50%范围

    float mdf_ratio = (curve.group_baseline_mdf > 0.001f)
                      ? (personal_baseline_mdf / curve.group_baseline_mdf)
                      : 1.0f;
    mdf_ratio = constrain(mdf_ratio, 0.5f, 2.0f);

    // 更新运行时校准数据中的个人曲线系数
    g_currentCalibData.curve_coef[0] = curve.coefficients[0];  // RMS基准锚点
    g_currentCalibData.curve_coef[1] = curve.coefficients[1]; // MDF基准锚点
    g_currentCalibData.curve_coef[2] = curve.coefficients[2]; // RMS斜率
    g_currentCalibData.curve_coef[3] = curve.coefficients[3] * rms_ratio; // 个人RMS偏移
    g_currentCalibData.curve_coef[4] = curve.coefficients[4] * mdf_ratio; // 个人MDF偏移

    g_currentCalibData.curve_baseline_rms = personal_baseline_rms;
    g_currentCalibData.curve_baseline_mdf  = personal_baseline_mdf;
    g_currentCalibData.matched_bcurve_id   = bcurve_id;
    g_currentCalibData.curve_status        = 0x01;  // 有效
    g_currentCalibData.has_curve            = 1;    // [v3.9.12] 曲线已拟合
    g_currentCalibData.curve_version++;

    // 写 Flash A 区（异步，tick完成）
    bool ok = UpdatePersonalCalib(&g_currentCalibData);
    if (ok) {
        LOG("[STORAGE] GeneratePersonalCurve: done id=%d, rms_ratio=%.3f, mdf_ratio=%.3f\n",
            bcurve_id, rms_ratio, mdf_ratio);
    }
    return ok;
}

// [v3.9.13] 用户档案接口实现
bool StorageManager::SaveUserProfile(uint8_t slot, const char* userId, const char* name,
                                     uint8_t age, uint8_t gender, uint8_t handedness) {
    if (slot > 1) {
        LOG("[STORAGE] SaveUserProfile: invalid slot %d\n", slot);
        return false;
    }

    // 读取当前A区扇区到工作缓冲
    uint32_t sectorAddr = (slot == 0) ? ZONE_A_BASE_ADDR : (ZONE_A_BASE_ADDR + SECTOR_SIZE);
    flash.ReadBytes(sectorAddr, &g_workBuf, sizeof(g_workBuf));

    // 更新用户信息字段
    strncpy(g_workBuf.user_id, userId, sizeof(g_workBuf.user_id) - 1);
    g_workBuf.user_id[sizeof(g_workBuf.user_id) - 1] = '\0';
    strncpy(g_workBuf.name, name, sizeof(g_workBuf.name) - 1);
    g_workBuf.name[sizeof(g_workBuf.name) - 1] = '\0';
    g_workBuf.age = age;
    g_workBuf.gender = gender;
    g_workBuf.handedness = handedness;

    // 擦除并写回
    flash.EraseSector(sectorAddr);
    bool ok = _writeMultiPage(sectorAddr, (const uint8_t*)&g_workBuf, sizeof(g_workBuf));

    if (ok) {
        LOG("[STORAGE] SaveUserProfile: slot=%d, name=%s, age=%d, gender=%d, hand=%d\n",
            slot, name, age, gender, handedness);
    } else {
        LOG("[STORAGE] SaveUserProfile: write failed\n");
    }
    return ok;
}

bool StorageManager::LoadUserProfile(uint8_t slot, char* outUserId, char* outName,
                                     uint8_t* outAge, uint8_t* outGender, uint8_t* outHandedness) {
    if (slot > 1) {
        LOG("[STORAGE] LoadUserProfile: invalid slot %d\n", slot);
        return false;
    }

    uint32_t sectorAddr = (slot == 0) ? ZONE_A_BASE_ADDR : (ZONE_A_BASE_ADDR + SECTOR_SIZE);
    flash.ReadBytes(sectorAddr, &g_workBuf, sizeof(g_workBuf));

    // 检查magic是否有效
    if (g_workBuf.magic != 0xA55AA55A) {
        LOG("[STORAGE] LoadUserProfile: slot %d empty (magic=0x%08X)\n", slot, g_workBuf.magic);
        return false;
    }

    // 复制用户信息
    if (outUserId) strncpy(outUserId, g_workBuf.user_id, 15), outUserId[15] = '\0';
    if (outName) strncpy(outName, g_workBuf.name, 31), outName[31] = '\0';
    if (outAge) *outAge = g_workBuf.age;
    if (outGender) *outGender = g_workBuf.gender;
    if (outHandedness) *outHandedness = g_workBuf.handedness;

    LOG("[STORAGE] LoadUserProfile: slot=%d, name=%s, age=%d\n", slot, g_workBuf.name, g_workBuf.age);
    return true;
}