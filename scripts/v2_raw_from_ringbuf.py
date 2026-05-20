#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
v2 raw data collection: read from ADC ring buffer instead of analogRead.
Click button -> collect 3000 samples @1kHz (3s) -> auto-save -> stop.
"""

import re

# === 1. SignalProcessor.h: add drainNewSamples declaration ===
h_path = r'E:\Personal\sEMG_Project\firmware\src\1_Signal\SignalProcessor.h'
with open(h_path, 'r', encoding='utf-8') as f:
    h = f.read()

insert_after = 'void resetBuffer();'
new_decl = '''void resetBuffer();

    // v2: drain all new samples from ring buffer into user buffer
    // returns number of samples drained (0 if empty)
    uint16_t drainNewSamples(int16_t* outBuf, uint16_t maxCount);'''

h = h.replace(insert_after, new_decl)

with open(h_path, 'w', encoding='utf-8') as f:
    f.write(h)
print("1. SignalProcessor.h: drainNewSamples declared")

# === 2. SignalProcessor.cpp: add implementation ===
cpp_path = r'E:\Personal\sEMG_Project\firmware\src\1_Signal\SignalProcessor.cpp'
with open(cpp_path, 'r', encoding='utf-8') as f:
    cpp = f.read()

impl = '''
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

void SignalProcessor::resetBuffer() {'''

cpp = cpp.replace('void SignalProcessor::resetBuffer() {', impl)

with open(cpp_path, 'w', encoding='utf-8') as f:
    f.write(cpp)
print("2. SignalProcessor.cpp: drainNewSamples implemented")

# === 3. AppController.cpp: rewrite _handleDbFeatureState raw collection ===
ac_path = r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp'
with open(ac_path, 'r', encoding='utf-8') as f:
    ac = f.read()

old_raw_block = '''void AppController::_handleDbFeatureState(void) {
    // \u3010\u4fee\u590d\u3011\u4ec5\u5728\u5f53\u524d\u9636\u6bb5\u5df2\u542f\u52a8\u65f6\uff0c\u624d\u91c7\u96c6\u539f\u59cbsEMG\u6570\u636e
    static uint32_t lastSampleTime = 0;
    if (_stageStarted[_currentStage] && millis() - lastSampleTime >= 10 && _rawPhaseBuf && _rawPhaseCount < 3000) {
        int16_t rawSample = analogRead(A0);  // sEMG\u4f20\u611f\u5668\u63a5A0
        _rawPhaseBuf[_rawPhaseCount++] = rawSample;
        lastSampleTime = millis();
    }'''

new_raw_block = '''void AppController::_handleDbFeatureState(void) {
    // v2: \u4eceADC\u73af\u5f62\u7f13\u51b2\u533a\u8bfb\u53d6\u539f\u59cb\u6570\u636e\uff0c1kHz\uff0c\u6ee1 3000 \u81ea\u52a8\u4fdd\u5b58
    if (_stageStarted[_currentStage] && _rawPhaseBuf && _rawPhaseCount < 3000) {
        uint16_t drained = _signalProc->drainNewSamples(_rawPhaseBuf + _rawPhaseCount, 3000 - _rawPhaseCount);
        _rawPhaseCount += drained;
        if (_rawPhaseCount >= 3000) {
            // \u81ea\u52a8\u4fdd\u5b58\u5f53\u524d\u9636\u6bb5
            bool ok = _storageMgr->BZone_AppendRawPhase(_currentStage, _rawPhaseBuf, _rawPhaseCount);
            LOG("[CTRL] DB: Stage %d auto-saved %d samples, ok=%s\\n", _currentStage, _rawPhaseCount, ok ? "YES" : "NO");
            _stageStarted[_currentStage] = false;
            _rawPhaseCount = 0;
            memset(_rawPhaseBuf, 0, 3000 * sizeof(int16_t));
            // \u901a\u77e5\u5c0f\u7a0b\u5e8f
            char resp[128];
            snprintf(resp, sizeof(resp), "{\\"cmd\\":\\"raw_phase_auto\\",\\"stage\\":%d,\\"ok\\":%s}", _currentStage, ok ? "true" : "false");
            _netMgr->sendJsonTo(gNetManager.getConnectedClientNum(), resp);
        }
    }'''

ac = ac.replace(old_raw_block, new_raw_block)

with open(ac_path, 'w', encoding='utf-8') as f:
    f.write(ac)
print("3. AppController.cpp: _handleDbFeatureState rewritten (ring buffer + auto-save)")

print("\nDone! Ready to compile.")
