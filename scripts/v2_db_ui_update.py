#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Complete v2 mini program update for database page:
1. NetManager: add clearAutoSeq() 
2. AppController: call clearAutoSeq() before raw_phase_auto push
3. wifiClient.js: add push message listener mechanism
4. database/index.js: handle raw_phase_auto, show recording state
5. database/index.wxml: add recording indicator
"""

import re

# === 1. NetManager.h: add clearAutoSeq declaration ===
nmh = r'E:\Personal\sEMG_Project\firmware\src\4_Network\NetManager.h'
with open(nmh, 'r', encoding='utf-8') as f:
    content = f.read()
content = content.replace(
    'void setAutoSeq(int seq);  // [v3.9.14] \u8bbe\u7f6e\u81ea\u52a8seq\uff08\u547d\u4ee4\u54cd\u5e94\u81ea\u52a8\u56de\u4f20\uff09',
    'void setAutoSeq(int seq);       // [v3.9.14] \u8bbe\u7f6e\u81ea\u52a8seq\uff08\u547d\u4ee4\u54cd\u5e94\u81ea\u52a8\u56de\u4f20\uff09\n    void clearAutoSeq() { _autoSeq = -1; }  // \u6e05\u9664\u81ea\u52a8seq\uff08push\u6d88\u606f\u7528\uff09'
)
with open(nmh, 'w', encoding='utf-8') as f:
    f.write(content)
print('1. NetManager.h: clearAutoSeq() added')

# === 2. AppController.cpp: clear auto seq before raw_phase_auto ===
ac = r'E:\Personal\sEMG_Project\firmware\src\5_AppController\AppController.cpp'
with open(ac, 'r', encoding='utf-8') as f:
    content = f.read()
# The raw_phase_auto send line is unique
content = content.replace(
    '_netMgr->sendJsonTo(_lastDbClientNum, resp);\n    }\n    }\n\n    // \u5efa\u5e93\u7279\u5f81\u91c7\u96c6\u72b6\u6001',
    '_netMgr->clearAutoSeq();  // push\u6d88\u606f\u4e0d\u5e26seq\n    _netMgr->sendJsonTo(_lastDbClientNum, resp);\n    }\n    }\n\n    // \u5efa\u5e93\u7279\u5f81\u91c7\u96c6\u72b6\u6001'
)
with open(ac, 'w', encoding='utf-8') as f:
    f.write(content)
print('2. AppController.cpp: clearAutoSeq before raw_phase_auto')

# === 3. wifiClient.js: add push message routing ===
wc = r'E:\Personal\sEMG_Project\mini_program\utils\wifiClient.js'
with open(wc, 'r', encoding='utf-8') as f:
    content = f.read()

# Add push listener infrastructure
old_onmessage = '''function onMessage(fn) {'''
new_onmessage = '''let _pushListeners = []
function onPush(fn) {
  if (typeof fn === 'function' && !_pushListeners.includes(fn)) _pushListeners.push(fn)
}
function offPush(fn) {
  if (!fn) { _pushListeners = [] }
  else { const i = _pushListeners.indexOf(fn); if (i !== -1) _pushListeners.splice(i, 1) }
}

function onMessage(fn) {'''

content = content.replace(old_onmessage, new_onmessage)

# Update _routeTypedMessage to also route push messages (cmd without seq match)
old_return_false = '''  return false
}

// \u62e6\u622a\u539f\u59cb\u6d88\u606f'''
new_return_false = '''  // Push message: cmd with no matching seq (unsolicited from firmware)
  if (data.cmd && _pushListeners.length > 0) {
    _pushListeners.forEach(fn => { try { fn(data) } catch (e) {} })
    return true
  }

  return false
}

// \u62e6\u622a\u539f\u59cb\u6d88\u606f'''

content = content.replace(old_return_false, new_return_false)

# Add onPush/offPush to exports
content = content.replace(
    "module.exports = { connect, disconnect, send, sendCmd, isConnected, setCallbacks, enableHeartbeat, onMessage, offMessage, onRealtimeData, offRealtimeData, onCalibData, offCalibData, close }",
    "module.exports = { connect, disconnect, send, sendCmd, isConnected, setCallbacks, enableHeartbeat, onMessage, offMessage, onRealtimeData, offRealtimeData, onCalibData, offCalibData, onPush, offPush, close }"
)

with open(wc, 'w', encoding='utf-8') as f:
    f.write(content)
print('3. wifiClient.js: onPush/offPush + push routing added')

# === 4. database/index.js: handle raw_phase_auto ===
dbjs = r'E:\Personal\sEMG_Project\mini_program\pages\database\index.js'
with open(dbjs, 'r', encoding='utf-8') as f:
    content = f.read()

# Add stageCompleted tracking to data
content = content.replace(
    "    currentMark: -1, // \u5f53\u524d\u6807\u8bb0\u5230\u7b2c\u51e0\u6bb5(-1=\u672a\u5f00\u59cb)",
    "    currentMark: -1, // \u5f53\u524d\u6807\u8bb0\u5230\u7b2c\u51e0\u6bb5(-1=\u672a\u5f00\u59cb)\n    stageCompleted: [false, false, false, false], // \u5404\u9636\u6bb5\u539f\u59cb\u6570\u636e\u662f\u5426\u91c7\u96c6\u5b8c\u6210\n    isRecording: false, // \u5f53\u524d\u662f\u5426\u6b63\u5728\u5f55\u5236\u539f\u59cb\u6570\u636e"
)

# Add _pushHandler cleanup in onUnload
content = content.replace(
    "  onUnload() {\n    this._stopRealtimeListener();\n    this._stopCalibListeners();",
    "  onUnload() {\n    this._stopRealtimeListener();\n    this._stopCalibListeners();\n    this._stopPushListener();"
)

# Add push listener methods and recording state management
# Insert after _stopRealtimeListener
old_stop = '''  _stopRealtimeListener() {
    if (this._realtimeHandler) {
      wifiClient.offRealtimeData(this._realtimeHandler);
      this._realtimeHandler = null;
    }
    this.setData({ liveRms: null, liveMdf: null, liveFatigue: null, liveFatigueStr: '--' });
  },'''

new_stop = '''  _stopRealtimeListener() {
    if (this._realtimeHandler) {
      wifiClient.offRealtimeData(this._realtimeHandler);
      this._realtimeHandler = null;
    }
    this.setData({ liveRms: null, liveMdf: null, liveFatigue: null, liveFatigueStr: '--' });
  },

  _startPushListener() {
    this._stopPushListener();
    this._pushHandler = (data) => {
      if (data.cmd === 'raw_phase_auto') {
        this._onRawPhaseAuto(data);
      }
    };
    wifiClient.onPush(this._pushHandler);
  },
  _stopPushListener() {
    if (this._pushHandler) {
      wifiClient.offPush(this._pushHandler);
      this._pushHandler = null;
    }
  },
  _onRawPhaseAuto(data) {
    const stage = data.stage;
    const ok = data.ok;
    const completed = [...this.data.stageCompleted];
    completed[stage] = true;
    this.setData({ stageCompleted: completed, isRecording: false });
    if (ok) {
      wx.showToast({ title: `${MARK_LABELS[stage]}采集完成`, icon: 'success', duration: 1500 });
    } else {
      wx.showToast({ title: `${MARK_LABELS[stage]}保存失败`, icon: 'none' });
    }
    // Check if all 4 stages done
    if (completed.every(Boolean)) {
      wx.showToast({ title: '4阶段全部完成', icon: 'success' });
    }
  },'''

content = content.replace(old_stop, new_stop)

# Update _startCollection to start push listener
content = content.replace(
    "  async _startCollection() {\n    this._startRealtimeListener();",
    "  async _startCollection() {\n    this._startRealtimeListener();\n    this._startPushListener();"
)

# Update onMarkConfirm: send db_mark, set isRecording
old_mark = '''  async onMarkConfirm() {
    const cur = this.data.currentMark;
    if (cur < 0 || cur >= 4) return;
    try {
      const res = await wifiClient.sendCmd('db_mark', { stage: cur });
      if (!res.ok) {
        wx.showToast({ title: '标记失败: ' + (res.err || '未知错误'), icon: 'none' });
        return;
      }
    } catch(e) {
      wx.showToast({ title: '标记失败: ' + e.message, icon: 'none' });
      return;
    }
    if (cur === 3) {
      // 4个阶段全部标记完毕，自动保存
      if (this._collectionTimer) { clearInterval(this._collectionTimer); this._collectionTimer = null; }
      this._stopRealtimeListener();
      this._doSave();
    } else {
      this.setData({ currentMark: cur + 1, statusText: `标记第${cur + 2}段: ${MARK_LABELS[cur + 1]}` });
    }
  },'''

new_mark = '''  async onMarkConfirm() {
    const cur = this.data.currentMark;
    if (cur < 0 || cur >= 4) return;
    try {
      const res = await wifiClient.sendCmd('db_mark', { stage: cur });
      if (!res.ok) {
        wx.showToast({ title: '标记失败: ' + (res.err || '未知错误'), icon: 'none' });
        return;
      }
      // Mark confirmed, firmware starts collecting raw data (3s @1kHz)
      this.setData({ isRecording: true, statusText: `正在采集: ${MARK_LABELS[cur]}（约3秒）...` });
    } catch(e) {
      wx.showToast({ title: '标记失败: ' + e.message, icon: 'none' });
      return;
    }
    // Move to next stage (button activates for next click)
    if (cur < 3) {
      this.setData({ currentMark: cur + 1 });
    }
    // Note: don't auto-save on stage 3 anymore — wait for raw_phase_auto or timer
  },'''

content = content.replace(old_mark, new_mark)

# Update _resetAll to also stop push listener and reset states
content = content.replace(
    "  _resetAll() {\n    if (this._collectionTimer) { clearInterval(this._collectionTimer); this._collectionTimer = null; }\n    this._stopRealtimeListener();\n    this._resetCalib();\n    this.setData({ dbPhase: DB_PHASE.IDLE, elapsedSec: 0, currentMark: -1, subject: null });\n  },",
    "  _resetAll() {\n    if (this._collectionTimer) { clearInterval(this._collectionTimer); this._collectionTimer = null; }\n    this._stopRealtimeListener();\n    this._stopPushListener();\n    this._resetCalib();\n    this.setData({ dbPhase: DB_PHASE.IDLE, elapsedSec: 0, currentMark: -1, subject: null, stageCompleted: [false,false,false,false], isRecording: false });\n  },"
)

with open(dbjs, 'w', encoding='utf-8') as f:
    f.write(content)
print('4. database/index.js: raw_phase_auto handling + recording state')

# === 5. database/index.wxml: add recording indicator ===
wxml = r'E:\Personal\sEMG_Project\mini_program\pages\database\index.wxml'
with open(wxml, 'r', encoding='utf-8') as f:
    content = f.read()

# Add recording pulse indicator after the mark hint
content = content.replace(
    '''        <text class="mark-hint" wx:if="{{currentMark >= 0 && currentMark < 4}}">点击当前高亮按钮标记：{{MARK_LABELS[currentMark]}}</text>
      </view>''',
    '''        <text class="mark-hint" wx:if="{{currentMark >= 0 && currentMark < 4 && !isRecording}}">点击当前高亮按钮标记：{{MARK_LABELS[currentMark]}}</text>
        <!-- \u5f55\u5236\u4e2d\u6307\u793a -->
        <view class="recording-indicator" wx:if="{{isRecording}}">
          <view class="rec-dot"></view>
          <text class="rec-text">正在采集原始数据...</text>
        </view>
      </view>'''
)

# Update button styles: show completed stages
content = content.replace(
    '''                class="db-phase-btn {{currentMark > index ? 'collected' : (currentMark === index ? 'collecting' : 'disabled')}}"''',
    '''                class="db-phase-btn {{stageCompleted[index] ? 'collected' : (currentMark === index && !isRecording ? 'collecting' : (isRecording ? 'disabled' : 'disabled'))}}"'''
)

# Update button icon for completed stages
content = content.replace(
    '''                <text class="phase-btn-icon">{{currentMark > index ? '✓' : (index + 1)}}</text>''',
    '''                <text class="phase-btn-icon">{{stageCompleted[index] ? '✓' : (currentMark === index && !isRecording ? '◉' : (index + 1))}}</text>'''
)

# Update button tap handler: only active when not recording
content = content.replace(
    '''                data-mark="{{index}}" bindtap="{{currentMark === index ? 'onMarkConfirm' : ''}}"''',
    '''                data-mark="{{index}}" bindtap="{{currentMark === index && !isRecording ? 'onMarkConfirm' : ''}}"''',
)

with open(wxml, 'w', encoding='utf-8') as f:
    f.write(content)
print('5. database/index.wxml: recording indicator + button state')

# === 6. database/index.wxss: add recording styles ===
wxss = r'E:\Personal\sEMG_Project\mini_program\pages\database\index.wxss'
with open(wxss, 'r', encoding='utf-8') as f:
    content = f.read()

recording_css = '''
/* \u5f55\u5236\u6307\u793a\u5668 */
.recording-indicator {
  display: flex;
  align-items: center;
  justify-content: center;
  margin-top: 20rpx;
  padding: 16rpx 32rpx;
  background: rgba(255, 60, 60, 0.15);
  border-radius: 12rpx;
  border: 1rpx solid rgba(255, 60, 60, 0.3);
}
.rec-dot {
  width: 20rpx;
  height: 20rpx;
  border-radius: 50%;
  background: #ff3c3c;
  margin-right: 16rpx;
  animation: rec-pulse 1s ease-in-out infinite;
}
@keyframes rec-pulse {
  0%, 100% { opacity: 1; }
  50% { opacity: 0.3; }
}
.rec-text {
  color: #ff6b6b;
  font-size: 28rpx;
  font-weight: 500;
}
'''

content = content + recording_css
with open(wxss, 'w', encoding='utf-8') as f:
    f.write(content)
print('6. database/index.wxss: recording indicator styles added')

print('\nAll done! Files modified:')
print('  - firmware: NetManager.h, AppController.cpp')
print('  - mini_program: wifiClient.js, database/index.js, database/index.wxml, database/index.wxss')
