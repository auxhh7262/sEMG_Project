#!/usr/bin/env python3
"""Fix: add _lastDbClientNum and use it in _handleDbFeatureState"""

h_path = r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.h'
with open(h_path, 'r', encoding='utf-8') as f:
    h = f.read()

# Add field after _rawPhaseCount
h = h.replace(
    'uint16_t _rawPhaseCount;         // \u5f53\u524d\u9636\u6bb5\u5df2\u91c7\u96c6\u6837\u672c\u6570\uff08\u5355\u7f13\u51b2\u533a\uff09',
    'uint16_t _rawPhaseCount;         // \u5f53\u524d\u9636\u6bb5\u5df2\u91c7\u96c6\u6837\u672c\u6570\uff08\u5355\u7f13\u51b2\u533a\uff09\n    uint8_t _lastDbClientNum;        // \u5efa\u5e93\u8fde\u63a5\u7684clientNum'
)

with open(h_path, 'w', encoding='utf-8') as f:
    f.write(h)

cpp_path = r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp'
with open(cpp_path, 'r', encoding='utf-8') as f:
    ac = f.read()

# Fix: replace gNetManager.getConnectedClientNum() with _lastDbClientNum
ac = ac.replace('gNetManager.getConnectedClientNum()', '_lastDbClientNum')

with open(cpp_path, 'w', encoding='utf-8') as f:
    f.write(ac)

print("OK: added _lastDbClientNum, replaced reference")
