// 文件: CalibrationManager.cpp
// 描述: 校准管理器实现文件
// 版本: V1.1 (上升沿检测 + REST稳定期)
// 日期: 2026-05-14

#include "CalibrationManager.h"
#include "0_Base/Logger.h"
#include <cmath>

// ==================== 配置参数 ====================
#define CALIB_MIN_RMS_RATIO   2.0f   // 保持硬编码，符合原始设计
#define CALIB_MIN_RMS_mV      0.5f
#define CALIB_MIN_MDF_HZ      10.0f  // [FIX] 修复致命断层：15.0f -> 10.0f
#define CALIB_MAX_MDF_HZ      250.0f // [FIX] 放宽上限：200.0f -> 250.0f

// [v3.9.11] 上升沿检测参数
#define RISE_RATIO_THRESHOLD  1.5f   // RMS超过running_avg的1.5倍视为"开始收缩"
#define MIN_STABLE_SAMPLES    3      // 至少3个连续高RMS样本才确认上升沿

void CalibrationManager::init() {
    reset();
}

void CalibrationManager::reset() {
    _data.bias_voltage = 0.0f;
    _data.rest_rms = 0.0f;
    _data.ref_rms = 0.0f;
    
    // 初始值兜底，防止后续乘法拦截失效
    _data.rest_mdf = 80.0f;
    _data.ref_mdf = 60.0f;
    _data.peak_rms = 0.0f;
    
    _data.valid = false;
    _sampleCount = 0;
    _rmsRunningSum = 0.0f;
    _mdfRunningSum = 0.0f;
}

void CalibrationManager::beginPhase() {
    _sampleCount = 0;
    _rmsRunningSum = 0.0f;
    _mdfRunningSum = 0.0f;
    LOG("[CALIB] Phase sampling started (RMS + MDF)\n");
}

void CalibrationManager::addSample(float rms_mV, float mdf_hz) {
    // 拦截异常浮点值
    if (!isfinite(rms_mV) || !isfinite(mdf_hz)) {
        LOG("[CALIB] WARNING: Invalid sample value (NaN/Inf) received\n");
        return;
    }
    if (_sampleCount >= MAX_SAMPLES) return;

    // 存入样本缓冲区
    _samples[_sampleCount].rms = rms_mV;
    _samples[_sampleCount].mdf = mdf_hz;
    
    // 实时查询用累加器
    _rmsRunningSum += rms_mV;
    _mdfRunningSum += mdf_hz;
    _sampleCount++;
}

void CalibrationManager::endPhase(bool isRestPhase) {
    if (_sampleCount == 0) {
        LOG("[CALIB] WARNING: 0 samples collected!\n");
        return;
    }

    const char* phaseName = isRestPhase ? "REST" : "MAX";

    if (isRestPhase) {
        _endRestPhase();
    } else {
        _endMaxPhase();
    }
}

// ==================== REST阶段：丢弃前3秒不稳定数据 ====================
void CalibrationManager::_endRestPhase() {
    // [v3.9.11] 丢弃前3秒（30个样本@10Hz），消除EMA残留和接触稳定期
    const int DISCARD_SAMPLES = 30;  // 3秒 @ 10Hz
    int startIdx = (_sampleCount > DISCARD_SAMPLES + 10) ? DISCARD_SAMPLES : 0;
    int count = (int)_sampleCount - startIdx;

    if (count < 3) {
        // 丢弃后样本不足，退化为全样本
        startIdx = 0;
        count = (int)_sampleCount;
    }

    // 对稳定期样本取简单平均（REST信号稳定，不需要截尾）
    float avg_rms = 0.0f, avg_mdf = 0.0f;
    float min_rms = 1e9f, max_rms = 0.0f;
    float min_mdf = 1e9f, max_mdf = 0.0f;

    for (int i = startIdx; i < startIdx + count; i++) {
        avg_rms += _samples[i].rms;
        avg_mdf += _samples[i].mdf;
        if (_samples[i].rms < min_rms) min_rms = _samples[i].rms;
        if (_samples[i].rms > max_rms) max_rms = _samples[i].rms;
        if (_samples[i].mdf < min_mdf) min_mdf = _samples[i].mdf;
        if (_samples[i].mdf > max_mdf) max_mdf = _samples[i].mdf;
    }
    avg_rms /= (float)count;
    avg_mdf /= (float)count;

    float rms_cv = (avg_rms > 1e-6f) ? ((max_rms - min_rms) / avg_rms) * 100.0f : 999.0f;
    float mdf_cv = (avg_mdf > 1e-6f) ? ((max_mdf - min_mdf) / avg_mdf) * 100.0f : 999.0f;

    _data.rest_rms = avg_rms;
    _data.rest_mdf = avg_mdf;

    LOG("[CALIB] REST done: RMS avg=%.2f mV (CV=%.1f%%), MDF avg=%.1f Hz (CV=%.1f%%), N=%d(discard=%d, used=%d)\n",
        avg_rms, rms_cv, avg_mdf, mdf_cv, _sampleCount, startIdx, count);
}

// ==================== MAX阶段：上升沿检测 + 稳定期均值 ====================
void CalibrationManager::_endMaxPhase() {
    // [v3.9.11] 上升沿检测算法：
    // 1. 计算前5个样本的RMS均值作为基线
    // 2. 找到RMS持续超过基线RISE_RATIO_THRESHOLD倍的起始点
    // 3. 从上升沿开始取后半段数据的均值和峰值
    
    int baselineCount = (_sampleCount >= 10) ? 5 : (_sampleCount / 2);
    if (baselineCount < 2) baselineCount = 2;

    // 计算基线RMS
    float baselineRms = 0.0f;
    for (int i = 0; i < baselineCount; i++) {
        baselineRms += _samples[i].rms;
    }
    baselineRms /= (float)baselineCount;

    // 上升沿检测：找到连续MIN_STABLE_SAMPLES个样本RMS超过基线RISE_RATIO_THRESHOLD倍
    int riseStartIdx = -1;
    int consecHigh = 0;
    float threshold = baselineRms * RISE_RATIO_THRESHOLD;
    
    for (int i = baselineCount; i < (int)_sampleCount; i++) {
        if (_samples[i].rms >= threshold) {
            consecHigh++;
            if (consecHigh >= MIN_STABLE_SAMPLES) {
                riseStartIdx = i - MIN_STABLE_SAMPLES + 1;  // 回溯到连续高值的起始点
                break;
            }
        } else {
            consecHigh = 0;
        }
    }

    int startIdx, usedCount;
    
    if (riseStartIdx >= 0) {
        // 检测到上升沿：从上升沿开始取全部后续样本
        startIdx = riseStartIdx;
        usedCount = (int)_sampleCount - startIdx;
        LOG("[CALIB] Rise detected at sample %d (baseline=%.2f, threshold=%.2f)\n",
            riseStartIdx, baselineRms, threshold);
    } else {
        // 未检测到上升沿：传感器可能一直贴着（信号始终较高）
        // 用后半段50%的样本
        startIdx = (int)_sampleCount / 2;
        usedCount = (int)_sampleCount - startIdx;
        LOG("[CALIB] No rise detected, using latter 50%% from sample %d\n", startIdx);
    }

    if (usedCount < 3) {
        // 样本不足，退化为全样本简单平均
        startIdx = 0;
        usedCount = (int)_sampleCount;
        LOG("[CALIB] WARNING: Too few samples after rise detection, using all\n");
    }

    // 对稳定期样本取均值和峰值
    float avg_rms = 0.0f, avg_mdf = 0.0f;
    float peak_rms = 0.0f;
    float min_mdf = 1e9f, max_mdf = 0.0f;

    for (int i = startIdx; i < startIdx + usedCount; i++) {
        avg_rms += _samples[i].rms;
        avg_mdf += _samples[i].mdf;
        if (_samples[i].rms > peak_rms) peak_rms = _samples[i].rms;
        if (_samples[i].mdf < min_mdf) min_mdf = _samples[i].mdf;
        if (_samples[i].mdf > max_mdf) max_mdf = _samples[i].mdf;
    }
    avg_rms /= (float)usedCount;
    avg_mdf /= (float)usedCount;

    float mdf_cv = (avg_mdf > 1e-6f) ? ((max_mdf - min_mdf) / avg_mdf) * 100.0f : 999.0f;
    float rms_cv = (avg_rms > 1e-6f) ? ((peak_rms - _samples[startIdx].rms) / avg_rms) * 100.0f : 999.0f;

    _data.ref_rms = avg_rms;
    _data.ref_mdf = avg_mdf;
    _data.peak_rms = peak_rms;  // [v3.9.11] 峰值RMS用于激活度计算

    LOG("[CALIB] MAX done: RMS avg=%.2f mV (peak=%.2f, CV=%.1f%%), MDF avg=%.1f Hz (CV=%.1f%%), N=%d(rise@%d, used=%d)\n",
        avg_rms, peak_rms, rms_cv, avg_mdf, mdf_cv, _sampleCount, startIdx, usedCount);
}

const CalibData_t& CalibrationManager::getData() const {
    return _data;
}

bool CalibrationManager::validateResult() {
    // 1. RMS 基础校验
    if (_data.rest_rms <= 0.0f || _data.ref_rms <= 0.0f) {
        LOG("[CALIB] FAIL: non-positive RMS (rest=%.2f, ref=%.2f)\n", _data.rest_rms, _data.ref_rms);
        return false;
    }
    if (_data.ref_rms <= _data.rest_rms * CALIB_MIN_RMS_RATIO) {
        LOG("[CALIB] FAIL: ref not 2x rest (ref=%.2f, rest=%.2f)\n", _data.ref_rms, _data.rest_rms);
        return false;
    }
    if (_data.ref_rms < CALIB_MIN_RMS_mV) {
        LOG("[CALIB] FAIL: ref too low, likely noise (%.2f mV)\n", _data.ref_rms);
        return false;
    }

    // 2. MDF 生理学合理性校验
    if (_data.rest_mdf < CALIB_MIN_MDF_HZ || _data.rest_mdf > CALIB_MAX_MDF_HZ) {
        LOG("[CALIB] FAIL: Abnormal rest MDF (%.1f Hz), check sensor\n", _data.rest_mdf);
        return false;
    }

    if (_data.ref_mdf < CALIB_MIN_MDF_HZ || _data.ref_mdf > CALIB_MAX_MDF_HZ) {
        LOG("[CALIB] FAIL: Abnormal ref MDF (%.1f Hz), check sensor\n", _data.ref_mdf);
        return false;
    }

    _data.valid = true;
    return true;
}

uint16_t CalibrationManager::getSampleCount() const {
    return _sampleCount;
}

float CalibrationManager::getCurrentRmsAvg() const {
    return (_sampleCount > 0) ? (_rmsRunningSum / (float)_sampleCount) : 0.0f;
}

float CalibrationManager::getCurrentMdfAvg() const {
    return (_sampleCount > 0) ? (_mdfRunningSum / (float)_sampleCount) : 0.0f;
}
