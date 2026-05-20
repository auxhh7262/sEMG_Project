import sys

filepath = r'E:\Personal\sEMG_Project\mini_program\pages\calibrate\index.wxml'

with open(filepath, 'r', encoding='utf-8') as f:
    content = f.read()

old = '''<!-- ========== 建库信息输入 (全屏页面) ========== -->
<view class="db-input-page {{dbShowInput ? 'show' : ''}}" wx:if="{{dbShowInput}}">
  <view class="db-input-header">
    <text class="db-input-title">新建参考曲线库</text>
    <text class="db-input-sub">B 区个人参考曲线建库</text>
  </view>

  <view class="modal-flow-hint">
    <view class="flow-step"><text class="flow-num active">1</text><text class="flow-text">录入信息</text></view>
    <view class="flow-arrow">→</view>
    <view class="flow-step"><text class="flow-num">2</text><text class="flow-text">特征采集</text></view>
  </view>

  <view class="db-intro-hint">
    <text class="db-intro-text">采集4个疲劳阶段的肌电特征，存入设备 B 区作为个人参考曲线。</text>
    <text class="db-intro-note">注意：请贴紧传感器，每阶段约 3 秒，建议从静息→轻度→中度→疲劳依次采集。</text>
  </view>

    <view class="form-group">
      <text class="form-label">建库 ID</text>
      <view class="auto-id-text">{{autoDbId}}</view>
    </view>

    <view class="form-row">
      <view class="form-group" style="flex:1">
        <text class="form-label">姓名</text>
        <input class="form-input" placeholder="选填" value="{{dbSubjectId}}" bindinput="onDbSubjectIdInput" />
      </view>
      <view class="form-group" style="flex:1">
        <text class="form-label">性别 *</text>
        <picker bindchange="onDbGenderChange" value="{{dbGenderIndex - 1}}" range="{{['男','女']}}">
          <view class="form-picker">
            <text>{{dbGenderIndex ? (dbGenderIndex === 1 ? '男' : '女') : '请选择'}}</text>
          </view>
        </picker>
      </view>
    </view>

    <view class="form-row">
      <view class="form-group" style="flex:1">
        <text class="form-label">年龄 *</text>
        <input class="form-input" type="number" placeholder="18-80" value="{{dbAge}}" bindinput="onDbAgeInput" />
      </view>
      <view class="form-group" style="flex:1">
        <text class="form-label">左右手 *</text>
        <picker bindchange="onDbHandChange" value="{{dbHandIndex - 1}}" range="{{['左手','右手']}}">
          <view class="form-picker">
            <text>{{dbHandIndex ? (dbHandIndex === 1 ? '左手' : '右手') : '请选择'}}</text>
          </view>
        </picker>
      </view>
    </view>

    <view class="db-input-actions">
      <button class="btn btn-secondary" bindtap="cancelDbInput">取消</button>
      <button class="btn btn-primary" bindtap="confirmDbStart">开始特征采集</button>
    </view>
  </view>
</view>'''

new = '''<!-- ========== 建库信息输入 (全屏页面) ========== -->
<view class="db-input-page {{dbShowInput ? 'show' : ''}}" wx:if="{{dbShowInput}}">
  <view class="db-input-header">
    <text class="db-input-title">新建参考曲线库</text>
    <text class="db-input-sub">B 区个人参考曲线建库</text>
  </view>

  <view class="modal-flow-hint">
    <view class="flow-step"><text class="flow-num active">1</text><text class="flow-text">录入信息</text></view>
    <view class="flow-arrow">→</view>
    <view class="flow-step"><text class="flow-num">2</text><text class="flow-text">特征采集</text></view>
  </view>

  <view class="db-intro-hint">
    <text class="db-intro-text">采集4个疲劳阶段的肌电特征，存入设备 B 区作为个人参考曲线。</text>
    <text class="db-intro-note">注意：请贴紧传感器，每阶段约 3 秒，建议从静息→轻度→中度→疲劳依次采集。</text>
  </view>

  <view class="form-group">
    <text class="form-label">建库 ID</text>
    <view class="auto-id-text">{{autoDbId}}</view>
  </view>

  <view class="form-row">
    <view class="form-group" style="flex:1">
      <text class="form-label">姓名</text>
      <input class="form-input" placeholder="选填" value="{{dbSubjectId}}" bindinput="onDbSubjectIdInput" />
    </view>
    <view class="form-group" style="flex:1">
      <text class="form-label">性别 *</text>
      <picker bindchange="onDbGenderChange" value="{{dbGenderIndex - 1}}" range="{{['男','女']}">
        <view class="form-picker">
          <text>{{dbGenderIndex ? (dbGenderIndex === 1 ? '男' : '女') : '请选择'}}</text>
        </view>
      </picker>
    </view>
  </view>

  <view class="form-row">
    <view class="form-group" style="flex:1">
      <text class="form-label">年龄 *</text>
      <input class="form-input" type="number" placeholder="18-80" value="{{dbAge}}" bindinput="onDbAgeInput" />
    </view>
    <view class="form-group" style="flex:1">
      <text class="form-label">左右手 *</text>
      <picker bindchange="onDbHandChange" value="{{dbHandIndex - 1}}" range="{{['左手','右手']}">
        <view class="form-picker">
          <text>{{dbHandIndex ? (dbHandIndex === 1 ? '左手' : '右手') : '请选择'}}</text>
        </view>
      </picker>
    </view>
  </view>

  <view class="db-input-actions">
    <button class="btn btn-secondary" bindtap="cancelDbInput">取消</button>
    <button class="btn btn-primary" bindtap="confirmDbStart">开始特征采集</button>
  </view>
</view>
</view>'''

if old in content:
    content = content.replace(old, new, 1)
    with open(filepath, 'w', encoding='utf-8') as f:
        f.write(content)
    print('OK - replaced')
else:
    print('ERROR - pattern not found')
    # Debug: find where db-input-page starts
    idx = content.find('db-input-page')
    if idx >= 0:
        print(f'Found at index {idx}')
        print(repr(content[idx:idx+200]))
