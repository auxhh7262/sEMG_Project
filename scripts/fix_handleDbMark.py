#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
修复 AppController.cpp 中的 handleDbMark() 函数
"""

import sys

file_path = r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp'

# 读取文件（二进制模式，保留原始换行符）
with open(file_path, 'rb') as f:
    content = f.read()

# 旧函数文本（从文件读取的实际字节）
old_func = b'''void AppController::handleDbMark(uint8_t clientNum, JsonObject doc) {
    // 仅在 ST_DB_FEATURE 状态下允许
    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\\"cmd\\":\\"db_marked\\",\\"ok\\":false,\\"err\\":\\"not_in_db_feature\\"}");
        return;
    }

    uint8_t markIndex = doc["stage"] | 0;  // 0~3
    if (markIndex >= 4) {
        gNetManager.sendJsonTo(clientNum, "{\\"cmd\\":\\"db_marked\\",\\"ok\\":false,\\"err\\":\\"invalid_stage\\"}");
        return;
    }

    // 【修复】标记前，先保存当前阶段剩余的原始数据到Flash \
    if (_rawPhaseBuf && _rawPhaseCount > 0) {
        if (_storageMgr->BZone_AppendRawPhase(_currentStage + 1, _rawPhaseBuf, _rawPhaseCount)) {
            _stageDataSaved[_currentStage] = true;
            LOG("[CTRL] DB: stage %d raw data flushed (%d samples)\\n", _currentStage, _rawPhaseCount);
        }
        _rawPhaseCount = 0;  // 重置计数器，准备下一阶段
        memset(_rawPhaseBuf, 0, 3000 * sizeof(int16_t));  // 清空缓冲区
    }

    uint16_t featureIdx = _storageMgr->BZone_GetFeatureCount();
    bool ok = _storageMgr->BZone_MarkFeaturePoint(markIndex);

    LOG("[CTRL] DB: db_mark stage=%d, featureIdx=%d, ok=%s\\n",
        markIndex, featureIdx, ok ? "YES" : "NO");

    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp),
            "{\\"cmd\\":\\"db_marked\\",\\"ok\\":true,\\"stage\\":%d,\\"feature_idx\\":%d}",
            markIndex, featureIdx);
    } else {
        snprintf(resp, sizeof(resp),
            "{\\"cmd\\":\\"db_marked\\",\\"ok\\":false,\\"err\\":\\"mark_failed\\",\\"stage\\":%d}",
            markIndex);
    }
    gNetManager.sendJsonTo(clientNum, resp);

    // 【修复】切换到下一 stage \
    _currentStage = markIndex + 1;
    if (_currentStage >= 4) {
        LOG("[CTRL] DB: all 4 stages marked, ready to save\\n");
    }
}'''

# 新函数文本（修复版）
new_func = b'''void AppController::handleDbMark(uint8_t clientNum, JsonObject doc) {
    // 仅在 ST_DB_FEATURE 状态下允许
    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\\"cmd\\":\\"db_marked\\",\\"ok\\":false,\\"err\\":\\"not_in_db_feature\\"}");
        return;
    }

    uint8_t markIndex = doc["stage"] | 0;  // 0~3
    if (markIndex >= 4) {
        gNetManager.sendJsonTo(clientNum, "{\\"cmd\\":\\"db_marked\\",\\"ok\\":false,\\"err\\":\\"invalid_stage\\"}");
        return;
    }

    // 【修复】保存前一阶段的原始数据
    if (markIndex > 0 && _rawPhaseBuf && _rawPhaseCount > 0) {
        uint8_t prevStage = markIndex - 1;
        LOG("[CTRL] DB: Saving raw phase %d (%d samples) to Flash\\n", prevStage, _rawPhaseCount);
        
        bool saveOk = _storageMgr->BZone_AppendRawPhase(prevStage, _rawPhaseBuf, _rawPhaseCount);
        
        if (saveOk) {
            LOG("[CTRL] DB: Raw phase %d saved successfully\\n", prevStage);
        } else {
            LOG("[CTRL] DB: ERROR saving raw phase %d\\n", prevStage);
        }
        
        // 清空缓冲区，准备当前阶段
        _rawPhaseCount = 0;
        memset(_rawPhaseBuf, 0, 3000 * sizeof(int16_t));
    }

    // 【修复】启动当前阶段的采集
    _currentStage = markIndex;
    _stageStarted[markIndex] = true;
    LOG("[CTRL] DB: Stage %d started collecting\\n", markIndex);

    uint16_t featureIdx = _storageMgr->BZone_GetFeatureCount();
    bool ok = _storageMgr->BZone_MarkFeaturePoint(markIndex);

    LOG("[CTRL] DB: db_mark stage=%d, featureIdx=%d, ok=%s\\n",
        markIndex, featureIdx, ok ? "YES" : "NO");

    char resp[128];
    if (ok) {
        snprintf(resp, sizeof(resp),
            "{\\"cmd\\":\\"db_marked\\",\\"ok\\":true,\\"stage\\":%d,\\"feature_idx\\":%d}",
            markIndex, featureIdx);
    } else {
        snprintf(resp, sizeof(resp),
            "{\\"cmd\\":\\"db_marked\\",\\"ok\\":false,\\"err\\":\\"mark_failed\\",\\"stage\\":%d}",
            markIndex);
    }
    gNetManager.sendJsonTo(clientNum, resp);

    // 检查是否所有4个阶段都已标记
    if (markIndex >= 3) {
        LOG("[CTRL] DB: all 4 stages marked, ready to save\\n");
    }
}'''

# 检查是否找到旧函数
if old_func in content:
    content = content.replace(old_func, new_func)
    
    # 写回文件（二进制模式）
    with open(file_path, 'wb') as f:
        f.write(content)
    
    print("✅ 替换成功！handleDbMark() 已修复")
    sys.exit(0)
else:
    print("❌ 未找到匹配的文本")
    print("文件前1000字节:")
    print(content[:1000])
    sys.exit(1)
