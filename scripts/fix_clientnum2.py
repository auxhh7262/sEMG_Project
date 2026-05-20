#!/usr/bin/env python3
"""Add _lastDbClientNum = clientNum in handleStartDbFeature and handleDbMark"""

cpp_path = r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp'
with open(cpp_path, 'r', encoding='utf-8') as f:
    ac = f.read()

# In handleStartDbFeature, after opening brace, add assignment
ac = ac.replace(
    'void AppController::handleStartDbFeature(uint8_t clientNum, JsonObject doc) {\n    // \u89e3\u6790\u88ab\u8bd5\u4fe1\u606f',
    'void AppController::handleStartDbFeature(uint8_t clientNum, JsonObject doc) {\n    _lastDbClientNum = clientNum;\n    // \u89e3\u6790\u88ab\u8bd5\u4fe1\u606f'
)

# Also save in handleDbMark
ac = ac.replace(
    'void AppController::handleDbMark(uint8_t clientNum, JsonObject doc) {\n    // \u3010\u4fee\u590d\u3011\u4fdd\u5b58\u524d\u4e00\u9636\u6bb5\u539f\u59cb\u6570\u636e\uff0c\u542f\u52a8\u5f53\u524d\u9636\u6bb5\u91c7\u96c6',
    'void AppController::handleDbMark(uint8_t clientNum, JsonObject doc) {\n    _lastDbClientNum = clientNum;\n    // \u3010\u4fee\u590d\u3011\u4fdd\u5b58\u524d\u4e00\u9636\u6bb5\u539f\u59cb\u6570\u636e\uff0c\u542f\u52a8\u5f53\u524d\u9636\u6bb5\u91c7\u96c6'
)

with open(cpp_path, 'w', encoding='utf-8') as f:
    f.write(ac)

print("OK: _lastDbClientNum assigned in handleStartDbFeature and handleDbMark")
