#ifndef CALIBRATION_MANAGER_H
#define CALIBRATION_MANAGER_H

#include <Arduino.h>
#include "0_Base/Globals.h"

struct CalibSample_t {
    float rms;
    float mdf;
};

class CalibrationManager {
public:
    void init();
    void beginPhase();
    
    // 扩展：同时注入 RMS 和 MDF 样本
    void addSample(float rms_mV, float mdf_hz);
    
    void endPhase(bool isRestPhase);
    const CalibData_t& getData() const;
    bool validateResult();
    
    uint16_t getSampleCount() const;
    float getCurrentRmsAvg() const;
    float getCurrentMdfAvg() const;
    
    void reset();

private:
    CalibData_t _data;
    
    // [v3.9.7] 样本缓冲区，用于10%截尾均值
    static const uint16_t MAX_SAMPLES = 200;  // 15s@10Hz=150, 留余量
    CalibSample_t _samples[MAX_SAMPLES];
    uint16_t _sampleCount;
    
    // 实时查询用累加器（不用于最终结果）
    float _rmsRunningSum;
    float _mdfRunningSum;
    
    // [v3.9.11] 阶段结束处理（分离REST/MAX逻辑）
    void _endRestPhase();
    void _endMaxPhase();
};

#endif // CALIBRATION_MANAGER_H