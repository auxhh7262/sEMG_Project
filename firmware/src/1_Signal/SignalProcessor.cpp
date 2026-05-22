// #define MDF_DBG_ENABLED  // 鍙栨秷娉ㄩ噴浠ュ惎鐢∕DF璋冭瘯鏃ュ織锛堜細浜х敓澶ч噺涓插彛杈撳嚭锛?
#include "0_Base/Board.h"
#include "0_Base/Logger.h"
#include "SignalProcessor.h"

#include <Arduino.h>
#include <cmath>
#include <cstring>

// ==================== 鍏冩暟鎹?====================
#define SIGNAL_PROCESSOR_VERSION "V1.0.0"
#define SIGNAL_PROCESSOR_DATE "2026-05-12"

// ==================== 闃插尽鎬у畯 ====================
#ifndef RING_BUFFER_SIZE
#define RING_BUFFER_SIZE 512
#endif
#ifndef RING_BUFFER_MASK
#define RING_BUFFER_MASK 511
#endif
#ifndef MAX_FFT_SIZE
#define MAX_FFT_SIZE 256
#endif
#ifndef QUALITY_WINDOW_SIZE
#define QUALITY_WINDOW_SIZE 50
#endif

// ==================== 璋冭瘯鏃ュ織瀹忥紙宸蹭慨澶嶅揩鎹峰畯缂哄け闂锛?====================
#define SP_LOG(level, fmt, ...) do { \
    if (m_debugEnabled && level <= m_debugLevel) \
        LOG("[SIGNAL] " fmt, ##__VA_ARGS__); \
} while(0)

#define SP_LOG_MINIMAL(fmt, ...) SP_LOG(DEBUG_MINIMAL, fmt, ##__VA_ARGS__)
#define SP_LOG_NORMAL(fmt, ...) SP_LOG(DEBUG_NORMAL, fmt, ##__VA_ARGS__)
#define SP_LOG_VERBOSE(fmt, ...) SP_LOG(DEBUG_VERBOSE, fmt, ##__VA_ARGS__)
#define SP_LOG_FULL(fmt, ...) SP_LOG(DEBUG_FULL, fmt, ##__VA_ARGS__)

// ==================== 鏋勯€犲嚱鏁?====================
SignalProcessor::SignalProcessor() :
    m_writeIndex(0), m_readIndex(0),
    m_fatigue(0.0f), m_activation(0.0f),
    m_restRMS_mV(0.1f), m_maxRMS_mV(1.0f),
    m_peakRMS_mV(1.0f),  // [v3.9.11]
    m_restMDF_hz(100.0f), m_maxMDF_hz(80.0f),
    m_contractionStartMDF(0.0f),  // [v3.9.11]
    m_isCalibrated(false),
    m_isContracting(false),
    m_currentMDF(50.0f), m_lastValidMDF(50.0f), m_isMdfValid(false),
    m_signalQuality(0.0f),
    m_fftWindowSize(DEFAULT_FFT_SIZE),
    m_mdfMinFreq(10.0f), m_mdfMaxFreq(250.0f),
    m_lastTotalPower(0.0f), m_rawMDF(0.0f),
    m_debugEnabled(false), m_debugLevel(DEBUG_NONE),
    m_fftTwiddleInitialized(false),
    m_lastSampleTime(0), m_actualSampleRate(1000.0f),
    m_sampleCount(0), m_sampleTimeAccum(0),
    m_availableSamples(0),
    m_droppedSamples(0),
    m_lastDebugTime(0), m_debugSkipCount(0),
    m_consecutivePhysioFrames(0),
    m_qualityValidFrames(0), m_qualityTotalFrames(0), m_qualityWindowFull(false),
    m_snapshotDCBias(0.0f), m_snapshotValid(false), m_snapshotSize(0),
    m_mvPerAdcUnit(0.0f)
{
    memset(m_ringBuffer, 0, sizeof(m_ringBuffer));
    memset(m_fftInputBuffer, 0, sizeof(m_fftInputBuffer));
    memset(m_fftImagBuffer, 0, sizeof(m_fftImagBuffer));
    memset(m_powerSpectrum, 0, sizeof(m_powerSpectrum));
    memset(m_lastPowerSpectrum, 0, sizeof(m_lastPowerSpectrum));
    memset(m_fftTwiddleReal, 0, sizeof(m_fftTwiddleReal));
    memset(m_fftTwiddleImag, 0, sizeof(m_fftTwiddleImag));
    memset(m_snapshot, 0, sizeof(m_snapshot));
}

// ==================== 鍒濆鍖栦笌閲嶇疆 ====================
void SignalProcessor::init() {
    m_writeIndex = 0; m_readIndex = 0;
    m_fatigue = 0.0f; m_activation = 0.0f; m_isCalibrated = false;
    m_peakRMS_mV = 1.0f;  // [v3.9.11]
    m_contractionStartMDF = 0.0f;  // [v3.9.11]
    m_isContracting = false;
    m_currentMDF = 0.0f; m_lastValidMDF = 80.0f; m_isMdfValid = false;
    m_signalQuality = 0.0f; m_lastTotalPower = 0.0f; m_rawMDF = 0.0f;
    m_debugEnabled = false; m_debugLevel = DEBUG_NONE;
    m_lastSampleTime = micros();
    m_actualSampleRate = 1000.0f;
    m_sampleCount = 0; m_sampleTimeAccum = 0;
    m_availableSamples = 0; m_droppedSamples = 0;
    m_lastDebugTime = 0; m_debugSkipCount = 0;
    m_consecutivePhysioFrames = 0;
    m_qualityValidFrames = 0; m_qualityTotalFrames = 0; m_qualityWindowFull = false;
    m_snapshotDCBias = 0.0f; m_snapshotValid = false; m_snapshotSize = 0;
    
    m_mvPerAdcUnit = ADC_REF_MV / (float)ADC_MAX_VALUE;
    initializeFFTTwiddles();
    SP_LOG_NORMAL("SignalProcessor initialized. Version: %s (%s)\n", SIGNAL_PROCESSOR_VERSION, SIGNAL_PROCESSOR_DATE);
}


// v2: drain all available new samples from ring buffer
uint16_t SignalProcessor::drainNewSamples(int16_t* outBuf, uint16_t maxCount) {
    noInterrupts();
    uint16_t avail = m_availableSamples;
    if (avail == 0) {
        interrupts();
        return 0;
    }
    uint16_t count = (avail < maxCount) ? avail : maxCount;
    // oldest samples first: read from (writeIndex - avail)
    uint16_t startIdx = (m_writeIndex - avail) & RING_BUFFER_MASK;
    for (uint16_t i = 0; i < count; i++) {
        outBuf[i] = m_ringBuffer[(startIdx + i) & RING_BUFFER_MASK];
    }
    m_availableSamples -= count;
    m_readIndex = (startIdx + count) & RING_BUFFER_MASK;
    interrupts();
    return count;
}

void SignalProcessor::resetBuffer() {
    noInterrupts();
    m_writeIndex = 0;
    m_availableSamples = 0;
    interrupts();
    SP_LOG_NORMAL("Buffer reset\n");
}

// ==================== ISR 瀹夊叏鐜舰缂撳啿鍖?====================
uint16_t SignalProcessor::safeGetStartIndex(uint16_t window_size) {
    if (window_size > RING_BUFFER_SIZE) window_size = RING_BUFFER_SIZE;
    uint32_t write_idx = m_writeIndex;
    if (write_idx >= window_size)
        return static_cast<uint16_t>(write_idx - window_size);
    return RING_BUFFER_SIZE - (window_size - static_cast<uint16_t>(write_idx));
}

void SignalProcessor::isrPushSample(int16_t sample) {
    noInterrupts();
    m_ringBuffer[m_writeIndex] = sample;
    m_writeIndex = (m_writeIndex + 1) & RING_BUFFER_MASK;
    if (m_availableSamples < RING_BUFFER_SIZE) {
        m_availableSamples++;
    } else {
        m_droppedSamples++;
    }
    interrupts();
}

// ==================== 閲囨牱鐜囩粺璁?====================
void SignalProcessor::updateSampleRateStats() {
    uint32_t currentTime = micros();
    if (m_lastSampleTime > 0 && m_lastSampleTime < currentTime) {
        uint32_t interval = currentTime - m_lastSampleTime;
        m_sampleTimeAccum += interval;
        m_sampleCount++;
        if (m_sampleCount >= 100) {
            float avgIntervalSec = m_sampleTimeAccum / 1000000.0f / m_sampleCount;
            if (avgIntervalSec > 0.0f) {
                m_actualSampleRate = 1.0f / avgIntervalSec;
            }
            m_sampleCount = 0;
            m_sampleTimeAccum = 0;
        }
    }
    m_lastSampleTime = currentTime;
}

// ==================== 鍏变韩蹇収缂撳瓨 ====================
// [FIX-v3.9.10] 涓嶅啀鍘熷湴鎺掑簭锛佹帓搴忎細鐮村潖鏃跺簭锛屽鑷?RMS 璁＄畻缁撴灉閿欒
// DC鍋忕Щ鐢ㄧ畝鍗曞潎鍊硷紙512鏍锋湰宸茶冻澶熺ǔ瀹氾紝鏃犻渶瑁佸壀鍧囧€硷級
void SignalProcessor::takeSnapshotIfNeeded(uint16_t window_size) {
    if (m_snapshotValid && m_snapshotSize == window_size) return;
    if (window_size == 0 || window_size > RING_BUFFER_SIZE) return;

    noInterrupts();
    uint16_t start_idx = safeGetStartIndex(window_size);
    float sum = 0.0f;
    for (uint16_t i = 0; i < window_size; i++) {
        m_snapshot[i] = m_ringBuffer[(start_idx + i) & RING_BUFFER_MASK];
        sum += m_snapshot[i];
    }
    interrupts();

    // 绠€鍗曞潎鍊艰绠桪C鍋忕Щ锛堜繚鐣欐椂搴忓畬鏁存€э級
    m_snapshotDCBias = (sum / (float)window_size) * m_mvPerAdcUnit;
    m_snapshotSize = window_size;
    m_snapshotValid = true;

    // [DEBUG-v3.9.10b] 鍘熷ADC璇婃柇锛氭墦鍗癿in/max/mean
    static uint32_t _lastSnapDbgMs = 0;
    if (millis() - _lastSnapDbgMs >= 2000) {
        _lastSnapDbgMs = millis();
        int16_t snapMin = m_snapshot[0], snapMax = m_snapshot[0];
        for (uint16_t i = 1; i < window_size; i++) {
            if (m_snapshot[i] < snapMin) snapMin = m_snapshot[i];
            if (m_snapshot[i] > snapMax) snapMax = m_snapshot[i];
        }
        float snapMean = sum / (float)window_size;
        LOG("[SNAP_DBG] N=%u min=%d max=%d mean=%.1f DCbias=%.2fmV\n",
            window_size, snapMin, snapMax, snapMean, m_snapshotDCBias);
    }
}

// ==================== RMS 璁＄畻锛堜笉鍔犵獥 + P1 鎬ц兘浼樺寲锛?====================
float SignalProcessor::calculateRMS() {
    const uint16_t window_size = m_fftWindowSize;
    if (m_availableSamples < window_size) return 0.0f;
    takeSnapshotIfNeeded(window_size);

    float sum_squares = 0.0f;
    for (uint16_t i = 0; i < window_size; i++) {
        float voltage = m_snapshot[i] * m_mvPerAdcUnit;
        float ac = voltage - m_snapshotDCBias;
        sum_squares += ac * ac;
    }
    return sqrtf(sum_squares / (float)window_size);
}

// ==================== FFT 鏍稿績 ====================
void SignalProcessor::initializeFFTTwiddles() {
    if (m_fftTwiddleInitialized) return;
    uint16_t half_n = m_fftWindowSize / 2;
    for (uint16_t i = 0; i < half_n; i++) {
        float theta = -2.0f * PI * i / m_fftWindowSize;
        m_fftTwiddleReal[i] = cosf(theta);
        m_fftTwiddleImag[i] = sinf(theta);
    }
    m_fftTwiddleInitialized = true;
}

void SignalProcessor::bitReverse(float* real, float* imag, uint16_t n) {
    uint16_t j = 0;
    for (uint16_t i = 0; i < n - 1; i++) {
        if (i < j) {
            float tr = real[i], ti = imag[i];
            real[i] = real[j]; imag[i] = imag[j];
            real[j] = tr; imag[j] = ti;
        }
        uint16_t k = n >> 1;
        while (k <= j) { j -= k; k >>= 1; }
        j += k;
    }
}

void SignalProcessor::fftRealInPlace(float* real, float* imag, uint16_t n) {
    bitReverse(real, imag, n);
    for (uint16_t len = 2; len <= n; len <<= 1) {
        uint16_t half_len = len >> 1;
        uint16_t step = n / len;
        for (uint16_t i = 0; i < n; i += len) {
            for (uint16_t j = 0; j < half_len; j++) {
                uint16_t tidx = j * step;
                float wr = m_fftTwiddleReal[tidx];
                float wi = m_fftTwiddleImag[tidx];
                uint16_t u = i + j, v = i + j + half_len;
                float tr = real[v] * wr - imag[v] * wi;
                float ti = real[v] * wi + imag[v] * wr;
                real[v] = real[u] - tr; imag[v] = imag[u] - ti;
                real[u] += tr; imag[u] += ti;
            }
        }
    }
}

// ==================== 鍔熺巼璋辫绠?====================
void SignalProcessor::calculatePowerSpectrum() {
    if (m_fftWindowSize < 2) return;
    if (!m_fftTwiddleInitialized) initializeFFTTwiddles();
    memset(m_fftImagBuffer, 0, sizeof(float) * m_fftWindowSize);

    for (uint16_t i = 0; i < m_fftWindowSize; i++) {
        float v = m_snapshot[i] * m_mvPerAdcUnit - m_snapshotDCBias;
        float w = 0.5f * (1.0f - cosf(2.0f * PI * i / (m_fftWindowSize - 1)));
        m_fftInputBuffer[i] = v * w;
    }

    fftRealInPlace(m_fftInputBuffer, m_fftImagBuffer, m_fftWindowSize);

    uint16_t half_n = m_fftWindowSize / 2;
    m_lastTotalPower = 0.0f;
    bool hasNaN = false;
    
    for (uint16_t i = 0; i < half_n; i++) {
        float real = m_fftInputBuffer[i];
        float imag = m_fftImagBuffer[i];
        float p = (real * real + imag * imag) / (float)m_fftWindowSize;
        // [FIX-v3.9.8] NaN/Inf 淇濇姢锛欰DC 楗卞拰瀵艰嚧 FFT 缁撴灉寮傚父
        if (!isnan(p) && !isinf(p)) {
            m_powerSpectrum[i] = p;
            m_lastTotalPower += p;
        } else {
            m_powerSpectrum[i] = 0.0f;  // 闈欓粯鏇挎崲寮傚父鍊?
            hasNaN = true;
        }
    }
    if (hasNaN) {
#ifdef MDF_DBG_ENABLED
        LOG("[MDF_DBG] NaN_IN_FFT!");
#endif
    }

    uint16_t copy_len = (half_n < (MAX_FFT_SIZE / 2)) ? half_n : (MAX_FFT_SIZE / 2);
    memcpy(m_lastPowerSpectrum, m_powerSpectrum, sizeof(float) * copy_len);
}

// ==================== MDF 璁＄畻 ====================
float SignalProcessor::findMedianFrequency(
    const float* power_spectrum,
    uint16_t num_bins,
    float sample_rate,
    float min_freq,
    float max_freq
) {
    // [DEBUG] 鍏ュ彛鍙傛暟璇婃柇
#ifdef MDF_DBG_ENABLED
    LOG("[MDF_DBG] ENTER sr=%.0f fmin=%.0f fmax=%.0f bins=%d",
        (double)sample_rate, (double)min_freq, (double)max_freq, (int)num_bins);
#endif

    // [FIX-v3.9.7] 寮傚父鏃惰繑鍥?-1.0f锛堥敊璇爣璁帮級锛屼笉杩斿洖 m_lastValidMDF
    // 鏃т唬鐮佽繑鍥?m_lastValidMDF 瀵艰嚧鑷紩鐢ㄥ惊鐜細
    // resetEMA()璁?m_lastValidMDF=80.0 鈫?fallback杩斿洖80.0 鈫?琚綋rawMDF 鈫?EMA鎺ュ彈80.0 鈫?閿佹
    if (sample_rate < 100.0f) {
#ifdef MDF_DBG_ENABLED
        LOG("[MDF_DBG] BAD_SR sr=%.1f", (double)sample_rate);
#endif
        return -1.0f;
    }

    float nyquist = sample_rate / 2.0f;
    float effective_max = fmin(max_freq, nyquist);
    float freq_res = sample_rate / m_fftWindowSize;

    float total_power = 0.0f;
    for (uint16_t i = 3; i < num_bins; i++) {
        float freq = i * freq_res;
        if (freq >= min_freq && freq <= effective_max) {
            total_power += power_spectrum[i];
        }
    }
    // [DEBUG] 鎵撳嵃 total_power锛堝叧閿瘖鏂細1e-12f闃堝€煎垽鏂級
    if (total_power < 1e-12f) {
#ifdef MDF_DBG_ENABLED
        LOG("[MDF_DBG] LOW_POWER tp=%.8f < 1e-12", (double)total_power);
#endif
        return -1.0f;
    }

    float half_power = total_power * 0.5f;
    float accumulated = 0.0f, prev_accumulated = 0.0f;
    float prev_freq = 0.0f;

    for (uint16_t i = 1; i < num_bins; i++) {
        float freq = i * freq_res;
        if (freq >= min_freq && freq <= effective_max) {
            float bp_val = power_spectrum[i];
            // [FIX-v3.9.8] 璺宠繃 NaN/Inf bins
            if (isnan(bp_val) || isinf(bp_val)) continue;
            prev_accumulated = accumulated;
            accumulated += bp_val;
            if (accumulated >= half_power) {
                if (bp_val > 0.0f) {
                    float ratio = (half_power - prev_accumulated) / bp_val;
                    float mdf_result = prev_freq + ratio * freq_res;
#ifdef MDF_DBG_ENABLED
                    LOG("[MDF_DBG] OK tp=%.4f MDF=%.1f", (double)total_power, (double)mdf_result);
#endif
                    return mdf_result;
                }
                return freq;
            }
        }
        prev_freq = freq;
    }
    // [DEBUG] 璇婃柇锛氫负浣昦ccumulated鏈揪鍒癶alf_power
#ifdef MDF_DBG_ENABLED
    LOG("[MDF_DBG] LOOP_END tp=%.4f acc=%.4f hp=%.4f bins=%d fmin=%.0f fmax=%.0f",
        (double)total_power, (double)accumulated, (double)half_power,
        (int)num_bins, (double)min_freq, (double)effective_max);
#endif
    return -1.0f;  // [FIX-v3.9.7] 棰戣氨寮傚父鏃犳硶瀹氫綅MDF
}

float SignalProcessor::calculateMDF() {
    if (m_availableSamples < m_fftWindowSize) {
        m_debugSkipCount++;
        m_isMdfValid = false;
        return 0.0f;
    }
    m_debugSkipCount = 0;

    takeSnapshotIfNeeded(m_fftWindowSize);
    calculatePowerSpectrum();

    // [FIX-v3.9.9] 浣跨敤 ADC 瀹氭椂鍣ㄥ浐瀹氶噰鏍风巼锛屼笉鐢?m_actualSampleRate
    // m_actualSampleRate 娴嬮噺鐨勬槸 loop 杩唬閫熺巼锛屼笉鏄?ADC 鐪熷疄閲囨牱鐜?
    // WiFi 閫氫俊浼氭嫋鎱?loop锛屽鑷?m_actualSampleRate 宕╁鍒?7-25Hz
    // ADC 瀹氭椂鍣ㄩ厤缃负 1000Hz锛堣 main.cpp adc_timer.begin(1000.0f)锛?
    constexpr float ADC_SAMPLE_RATE = 1000.0f;
    m_rawMDF = findMedianFrequency(
        m_powerSpectrum,
        m_fftWindowSize / 2,
        ADC_SAMPLE_RATE,
        m_mdfMinFreq,
        m_mdfMaxFreq
    );
#ifdef MDF_DBG_ENABLED
    LOG("[MDF_DBG] rawMDF=%.2f curMDF=%.2f lastValid=%.2f",
        (double)m_rawMDF, (double)m_currentMDF, (double)m_lastValidMDF);
#endif

    // [FIX-v3.9.7] findMedianFrequency 寮傚父鏃惰繑鍥?-1.0f锛岃烦杩囨湰娆?EMA 鏇存柊
    if (m_rawMDF < 0.0f) {
        // FFT鏃犳晥锛堝姛鐜囧お灏忔垨閲囨牱鐜囧紓甯革級锛屼繚鎸佷笂娆℃湁鏁?MDF锛屼笉鏇存柊 EMA
        m_consecutivePhysioFrames = 0;
        // 涓嶄慨鏀?m_currentMDF / m_lastValidMDF / m_isMdfValid
#ifdef MDF_DBG_ENABLED
        LOG("[MDF_DBG] -> rawMDF<0, return m_currentMDF=%.2f (hold)", (double)m_currentMDF);
#endif
        return m_currentMDF;
    }

    // [FIX-v3.9.6] 鏀惧涓婇檺 180鈫?50Hz锛氳倢鑲夋敹缂╂椂 MDF 鍙揪 200+Hz锛?
    // 涔嬪墠 180Hz 涓婇檺瀵艰嚧 rawMDF 琚涪寮冿紝EMA 姘歌繙杈撳嚭涓婃鍊?鈫?MAX闃舵閿佹
    bool is_physiological = (m_rawMDF >= 10.0f && m_rawMDF <= 250.0f);
    bool is_acceptable = (m_rawMDF >= 8.0f && m_rawMDF < 10.0f);

    if (is_physiological || is_acceptable) {
        m_consecutivePhysioFrames++;
        float alpha;
        // [FIX-v3.9.6] 鏀剁缉鐘舵€佹椂浣跨敤鏇撮珮 alpha锛屾洿蹇窡韪璋卞彉鍖?
        if (m_isContracting) {
            alpha = 0.35f;  // 鏀剁缉鏃堕渶瑕佹洿蹇搷搴旓紝閬垮厤 EMA 婊炲悗
        } else if (m_rawMDF < m_lastValidMDF && m_isMdfValid) {
            alpha = 0.35f;  // MDF 涓嬮檷鏃惰緝蹇窡韪紙鐤插姵瓒嬪娍锛?
        } else {
            if (m_consecutivePhysioFrames >= 10) {
                alpha = 0.15f;  // 绋虫€佷笅鎱㈤€熷钩婊?
            } else {
                alpha = 0.5f - 0.35f * (m_consecutivePhysioFrames / 10.0f);
            }
        }
        if (m_isMdfValid && m_lastValidMDF > 0.0f) {
            m_currentMDF = m_lastValidMDF * (1.0f - alpha) + m_rawMDF * alpha;
        } else {
            m_currentMDF = m_rawMDF;
        }
        m_lastValidMDF = m_currentMDF;
        m_isMdfValid = true;
#ifdef MDF_DBG_ENABLED
        LOG("[MDF_DBG] EMA OK: rawMDF=%.2f alpha=%.2f -> m_currentMDF=%.2f",
            (double)m_rawMDF, (double)alpha, (double)m_currentMDF);
#endif
    } else {
        // rawMDF 瓒呭嚭 [8, 250]Hz 鑼冨洿锛岃涓哄紓甯?
        m_consecutivePhysioFrames = 0;
        if (m_lastValidMDF > 0.0f) {
            m_currentMDF = m_lastValidMDF;
            m_isMdfValid = false;
        } else {
            m_currentMDF = 0.0f;
            m_isMdfValid = false;
        }
    }
    return m_currentMDF;
}

// ==================== 淇″彿璐ㄩ噺璇勪及锛堢簿绠€閫昏緫锛?====================
void SignalProcessor::evaluateSignalQuality(float rms, float mdf) {
    float quality_score = 0.0f;
    
    if (m_isContracting) {
        if (rms > 0.1f && rms < 5.0f) {
            quality_score += 35.0f;
        } else if (rms > 0.01f) {
            quality_score += 15.0f;
        }
    } else {
        if (rms < 0.5f) {
            quality_score += 35.0f;
        }
    }

    if (m_isMdfValid) {
        quality_score += 35.0f;
    } else {
        quality_score += 15.0f;
    }

    m_qualityTotalFrames++;
    if (m_isMdfValid) {
        m_qualityValidFrames++;
    }

    if (m_qualityTotalFrames >= QUALITY_WINDOW_SIZE) {
        m_qualityTotalFrames = 1;
        m_qualityValidFrames = m_isMdfValid ? 1 : 0;
    }

    float continuity = (m_qualityTotalFrames > 0)
        ? (float)m_qualityValidFrames / m_qualityTotalFrames
        : 0.0f;

    quality_score += 30.0f * continuity;
    m_signalQuality = constrain(quality_score, 0.0f, 100.0f);
}

// ==================== 鐤插姵璇勪及 ====================
void SignalProcessor::updateFatigue(float rms, float mdf) {
    if (!m_isCalibrated || mdf <= 0.0f) {
        m_fatigue = 0.0f; m_activation = 0.0f; m_isContracting = false;
        return;
    }

    // [v3.9.11] 婵€娲诲害锛氱敤 peakRMS 浠ｆ浛 maxRMS
    // peakRMS 鏄疢AX闃舵宄板€硷紝浠ｈ〃鐢ㄦ埛鑳借揪鍒扮殑鏈€澶у姏搴?
    // 杩欐牱鍗充娇 current_rms > avg_max_rms锛宎ct 涔熶笉浼氭孩鍑哄埌 1.0
    float normRms = m_peakRMS_mV;  // 鐢ㄥ嘲鍊煎仛褰掍竴鍖栧垎姣?
    if (normRms <= m_restRMS_mV) normRms = m_maxRMS_mV;  // fallback
    if (normRms > m_restRMS_mV) {
        m_activation = (rms - m_restRMS_mV) / (normRms - m_restRMS_mV);
        m_activation = constrain(m_activation, 0.0f, 1.0f);
    } else {
        m_activation = 0.0f;
    }

    // [v3.9.11] 鏀剁缉妫€娴嬶細RMS瓒呰繃2鍊峳est_rms瑙嗕负姝ｅ湪鏀剁缉
    bool wasContracting = m_isContracting;
    m_isContracting = (rms > m_restRMS_mV * 2.0f);

    // [v3.9.11] 鐤插姵搴︼細鍙湪鎸佺画鏀剁缉鏈熼棿璁＄畻
    // 涓€у寲鏇茬嚎绠楁硶 vs 榛樿 MDF 绠楁硶 [v3.9.12]
    if (m_isContracting) {
        // 鍒氳繘鍏ユ敹缂╃姸鎬侊細璁板綍璧峰MDF
        if (!wasContracting) {
            m_contractionStartMDF = mdf;
        }

        if (m_contractionStartMDF > 10.0f) {
            float ratio = 0.0f;

            if (m_hasPersonalCurve) {
                // 涓€у寲绠楁硶锛氱敤涓汉鏇茬嚎鍩哄噯閿氱偣璁＄畻鐤插姵
                // curveCoef[0]=baseline_rms, [1]=baseline_mdf, [2]=rms鏂滅巼, [3]=mdf鏂滅巼, [4]=鍋忕Щ
                // 褰掍竴鍖朚DF鐩稿浜庝釜浜哄熀鍑嗙殑涓嬮檷
                float personalBaselineMdf = m_curveCoef[1];  // 涓汉MDF鍩哄噯閿氱偣
                if (personalBaselineMdf > 10.0f) {
                    // 褰撳墠MDF鐩稿涓汉鍩哄噯鐨勫亸绂伙紙鐤插姵鏃禡DF涓嬮檷=ratio涓婂崌锛?
                    float deviation = personalBaselineMdf - mdf;
                    // 鐢ㄤ釜浜烘枩鐜囧綊涓€鍖栵紝鍐嶆槧灏勫埌0~1鐤插姵搴?
                    float slope = fabsf(m_curveCoef[3]) > 0.001f ? m_curveCoef[3] : 1.0f;
                    ratio = constrain(deviation / (slope * personalBaselineMdf * 0.5f), 0.0f, 1.0f);
                }
            } else {
                // 榛樿绠楁硶锛歁DF 鐩稿浜庢敹缂╄捣濮嬪€肩殑涓嬮檷鐜?
                ratio = (m_contractionStartMDF - mdf) / m_contractionStartMDF;
            }

            ratio = constrain(ratio, 0.0f, 1.0f);
            m_fatigue = 1.0f - expf(-2.0f * ratio);
        }
    } else {
        // 鏈敹缂╂椂鐤插姵搴﹀綊闆?
        m_fatigue = 0.0f;
        m_contractionStartMDF = 0.0f;
    }
}

// ==================== 瀵瑰鎺ュ彛 ====================
float SignalProcessor::update() {
    if (m_availableSamples < m_fftWindowSize) return 0.0f;
    m_snapshotValid = false;

    float rms = calculateRMS();
    if (rms <= 0.0f) return 0.0f;

    float mdf = calculateMDF();
    evaluateSignalQuality(rms, mdf);
    updateFatigue(rms, mdf);

    return rms;
}

void SignalProcessor::setCalibration(float restRMS_mV, float maxRMS_mV,
                                      float restMDF_hz, float maxMDF_hz,
                                      float peakRMS_mV,
                                      bool hasCurve,
                                      const float* curveCoef) {
    m_restRMS_mV = restRMS_mV;
    m_maxRMS_mV = maxRMS_mV;
    m_peakRMS_mV = (peakRMS_mV > maxRMS_mV) ? peakRMS_mV : maxRMS_mV;
    m_restMDF_hz = restMDF_hz;
    m_maxMDF_hz = maxMDF_hz;
    m_isCalibrated = true;
    m_contractionStartMDF = 0.0f;
    // [v3.9.12] 涓€у寲鏇茬嚎鍙傛暟
    m_hasPersonalCurve = hasCurve;
    if (hasCurve && curveCoef != nullptr) {
        memcpy(m_curveCoef, curveCoef, sizeof(float) * 5);
        LOG("[SIG] Personalized curve loaded: baseline_rms=%.2f, baseline_mdf=%.1f\n",
            curveCoef[0], curveCoef[1]);
    } else {
        memset(m_curveCoef, 0, sizeof(m_curveCoef));
    }
}

void SignalProcessor::clearCalibration() {
    m_isCalibrated = false;
    m_fatigue = 0.0f;
    m_activation = 0.0f;
    m_isContracting = false;
    m_peakRMS_mV = 1.0f;  // [v3.9.11]
    m_contractionStartMDF = 0.0f;
    m_hasPersonalCurve = false;
    memset(m_curveCoef, 0, sizeof(m_curveCoef));
    m_lastValidMDF = 80.0f;
    m_isMdfValid = false;
    m_consecutivePhysioFrames = 0;
}

// [FIX-v3.9.6] 鏍″噯闃舵鍒囨崲鏃堕噸缃?EMA 鐘舵€?
// REST鈫扢AX 鍒囨崲鏃堕璋卞舰鎬佸法鍙橈紝EMA 娈嬪€间細涓ラ噸婊炲悗
void SignalProcessor::resetEMA() {
    m_isMdfValid = false;
    m_lastValidMDF = 0.0f;  // [FIX-v3.9.7] 80.0f鈫?.0f锛氶槻姝?findMedianFrequency fallback 鑷紩鐢ㄩ攣姝?
    m_consecutivePhysioFrames = 0;
    m_currentMDF = 0.0f;
}

float SignalProcessor::getMDF() const { return m_currentMDF; }
float SignalProcessor::getFatigue() const { return m_fatigue; }
float SignalProcessor::getSignalQuality() const { return m_signalQuality; }
float SignalProcessor::getActivation() const { return m_activation; }
bool SignalProcessor::isContracting() const { return m_isContracting; }

void SignalProcessor::setFFTWindowSize(uint16_t size) {
    if (size < 64) size = 64;
    if (size > MAX_FFT_SIZE) size = MAX_FFT_SIZE;
    uint16_t pot = 64;
    while (pot < size && pot < MAX_FFT_SIZE) pot <<= 1;
    m_fftWindowSize = pot;
    m_fftTwiddleInitialized = false;
    initializeFFTTwiddles();
}

void SignalProcessor::setMDFFrequencyRange(float min_freq, float max_freq) {
    m_mdfMinFreq = constrain(min_freq, 0.0f, 250.0f);
    m_mdfMaxFreq = constrain(max_freq, m_mdfMinFreq + 1.0f, 250.0f);
}

