#!/usr/bin/env python3
# -*- coding: utf-8 -*-
import sys

fp = r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp'

with open(fp, 'rb') as f:
    data = f.read()

# Replace _stageDataSaved with _stageStarted in remaining references
data = data.replace(b'_stageDataSaved[i]', b'_stageStarted[i]')
data = data.replace(b'memset(_stageDataSaved, 0, sizeof(_stageDataSaved))', 
                    b'memset(_stageStarted, 0, sizeof(_stageStarted));\n    _currentStage = 0;')

with open(fp, 'wb') as f:
    f.write(data)

# Verify no remaining references
with open(fp, 'r', encoding='utf-8') as f:
    content = f.read()
if '_stageDataSaved' in content:
    print("WARNING: still has _stageDataSaved references!")
else:
    print("OK - all _stageDataSaved references removed")
