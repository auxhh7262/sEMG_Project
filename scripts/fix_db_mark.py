#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import sys

fp = r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp'

with open(fp, 'rb') as f:
    data = f.read()

# Find function boundaries
start_marker = b'void AppController::handleDbMark(uint8_t clientNum, JsonObject doc) {'
idx = data.find(start_marker)
if idx < 0:
    print("ERROR: handleDbMark not found"); sys.exit(1)

# Find matching closing brace
brace = 0
end = idx
for i in range(idx, len(data)):
    if data[i:i+1] == b'{':
        brace += 1
    elif data[i:i+1] == b'}':
        brace -= 1
        if brace == 0:
            end = i + 1
            break

old = data[idx:end]
print(f"Found function at {idx}-{end}, {len(old)} bytes")

new_func = b'''void AppController::handleDbMark(uint8_t clientNum, JsonObject doc) {
    // \xe3\x80\x90\xe4\xbf\xae\xe5\xa4\x8d\xe3\x80\x91\xe4\xbf\x9d\xe5\xad\x98\xe5\x89\x8d\xe4\xb8\x80\xe9\x98\xb6\xe6\xae\xb5\xe5\x8e\x9f\xe5\xa7\x8b\xe6\x95\xb0\xe6\x8d\xae\xef\xbc\x8c\xe5\x90\xaf\xe5\x8a\xa8\xe5\xbd\x93\xe5\x89\x8d\xe9\x98\xb6\xe6\xae\xb5\xe9\x87\x87\xe9\x9b\x86
    if (_stateMgr->getState() != ST_DB_FEATURE) {
        gNetManager.sendJsonTo(clientNum, "{\\"cmd\\":\\"db_marked\\",\\"ok\\":false,\\"err\\":\\"not_in_db_feature\\"}");
        return;
    }

    uint8_t markIndex = doc["stage"] | 0;  // 0~3
    if (markIndex >= 4) {
        gNetManager.sendJsonTo(clientNum, "{\\"cmd\\":\\"db_marked\\",\\"ok\\":false,\\"err\\":\\"invalid_stage\\"}");
        return;
    }

    // \xe4\xbf\x9d\xe5\xad\x98\xe5\x89\x8d\xe4\xb8\x80\xe9\x98\xb6\xe6\xae\xb5\xe7\x9a\x84\xe5\x8e\x9f\xe5\xa7\x8b\xe6\x95\xb0\xe6\x8d\xae
    if (markIndex > 0 && _rawPhaseBuf && _rawPhaseCount > 0) {
        uint8_t prevStage = markIndex - 1;
        LOG("[CTRL] DB: Saving raw phase %d (%d samples)\\n", prevStage, _rawPhaseCount);
        bool saveOk = _storageMgr->BZone_AppendRawPhase(prevStage, _rawPhaseBuf, _rawPhaseCount);
        if (saveOk) {
            LOG("[CTRL] DB: Raw phase %d saved OK\\n", prevStage);
        } else {
            LOG("[CTRL] DB: ERROR saving raw phase %d\\n", prevStage);
        }
        _rawPhaseCount = 0;
        memset(_rawPhaseBuf, 0, 3000 * sizeof(int16_t));
    }

    // \xe5\x90\xaf\xe5\x8a\xa8\xe5\xbd\x93\xe5\x89\x8d\xe9\x98\xb6\xe6\xae\xb5\xe7\x9a\x84\xe9\x87\x87\xe9\x9b\x86
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

    if (markIndex >= 3) {
        LOG("[CTRL] DB: all 4 stages marked, ready to save\\n");
    }
}'''

data = data[:idx] + new_func + data[end:]

with open(fp, 'wb') as f:
    f.write(data)

print("OK - handleDbMark() replaced successfully")
