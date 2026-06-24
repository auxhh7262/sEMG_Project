// pages/calibrate/index.js - 校准页（合并版：恢复功能 + 本地验证防崩溃）
const app = getApp();
const storage = require('../../utils/storage.js');
const { getCurrentUser, setCurrentUser, getSlotProfile, saveSlotProfile, getAgeGroup } = storage;
const wifiClient = require('../../utils/wifiClient.js');
const logger = require('../../utils/logger.js');

// ===================== 状态常量 =====================
const PHASE = {
  IDLE:          'idle',
  RESTING:       'resting',     // 静息态采集 10s
  MAX_READY:     'max_ready',   // 等待用户点击"开始MAX"按钮
  MAX_CONTRACT:  'max_contract',// 最大收缩采集 15s
  RESULT:        'result',      // 结果展示（含保存后状态）
};
const REST_DURATION = 10;   // 秒
const MAX_DURATION  = 15;   // 秒

// ===================== Page =====================
Page({
  data: {
    phase: PHASE.IDLE,
    connected: false,
    currentUser: null,
    userMetaStr: '',
    showUserForm: false,
    showSwitchPanel: false,
    validation: null,
    saved: false,           // 标记校准数据是否已保存
    liveRestRms: null, liveRestMdf: null,
    liveMaxRms: null,  liveMaxMdf:  null,
    countdown: 0,
    statusText: '点击下方按钮开始校准',
  },

  onLoad() {
    this._refreshUser();
  },

  // 状态变化处理
  _onStatusChange(status) {
    this.setData({ connected: status === 'connected' });
    if (status === 'disconnected' || status === 'reconnect_failed') {
      wx.showToast({ title: '设备连接断开', icon: 'none' });
      this._resetCalib();
    }
  },

  // 注册校准回调
  _registerCalibCallbacks() {
    this._unregisterCalibCallbacks();
    // 只注册 calib_data 监听器，由固件 phase 字段区分 REST/MAX
    this._calibHandler = (data) => {
      if (data.phase === 'REST') {
        this.setData({ 
          liveRestRms: data.rms != null ? data.rms.toFixed(3) : null,
          liveRestMdf: data.mdf != null ? data.mdf.toFixed(1) : null
        });
      } else if (data.phase === 'MAX') {
        this.setData({ 
          liveMaxRms: data.rms != null ? data.rms.toFixed(3) : null,
          liveMaxMdf: data.mdf != null ? data.mdf.toFixed(1) : null
        });
      }
    };
    wifiClient.onCalibData(this._calibHandler);
  },

  // 取消注册
  _unregisterCalibCallbacks() {
    if (this._calibHandler) {
      wifiClient.offCalibData(this._calibHandler);
      this._calibHandler = null;
    }
  },

  onShow() { 
    logger.log('[calibrate] onShow');
    this.setData({ connected: wifiClient.isConnected() });
    this._refreshUser();
    
    wifiClient.onStatusChange(this._onStatusChange);
    
    if (this.data.phase === PHASE.RESTING || this.data.phase === PHASE.MAX_CONTRACT) {
      logger.log('[calibrate] onShow: re-registering callbacks');
      this._registerCalibCallbacks();
    }
  },

  onHide() {
    wifiClient.offStatusChange(this._onStatusChange);
    this._unregisterCalibCallbacks();
    // 校准期间切Tab，清除倒计时并重置状态
    if (this.data.phase === PHASE.RESTING || this.data.phase === PHASE.MAX_CONTRACT) {
      logger.log('[calibrate] onHide during calib, resetting');
      this._resetCalib();
    }
  },

  onUnload() {
    // 页面被回收时清除所有定时器和回调
    if (this._restInterval) { clearInterval(this._restInterval); this._restInterval = null; }
    if (this._maxInterval)  { clearInterval(this._maxInterval);  this._maxInterval  = null; }
    this._unregisterCalibCallbacks();
    wifiClient.offStatusChange(this._onStatusChange);
  },

  // ===================== 工具 =====================
  _checkWifi() {
    if (!wifiClient.isConnected()) {
      wx.showToast({ title: '请先在"网络配置"页连接设备', icon: 'none' });
      return false;
    }
    return true;
  },

  _refreshUser() {
    const user = getCurrentUser();
    this.setData({ currentUser: user, userMetaStr: this._buildUserMeta(user) });
  },

  // ===================== 用户profile =====================
  onFirstUseCalibrate() {
    if (!this._checkWifi()) return;
    this.setData({ showUserForm: true });
  },

  onOpenUserForm() {
    const user = this.data.currentUser;
    this.setData({
      showUserForm: true,
      isEditUser: !!user,
      formData: user ? { name: user.name, gender: String(user.gender), age: String(user.age), handedness: String(user.handedness) } : { name: '', gender: '', age: '', handedness: '' },
    });
  },

  onCloseUserForm() {
    this.setData({ showUserForm: false });
  },

  onStopPropagation() {
    // 阻止事件冒泡到 modal-mask 触发关闭
  },

  onUserFormSubmit(e) {
    const form = e.detail.value;
    if (!form.name || !form.gender || !form.age || !form.handedness) {
      wx.showToast({ title: '请填写完整信息', icon: 'none' }); return;
    }
    const age = parseInt(form.age);
    if (isNaN(age) || age < 1 || age > 120) {
      wx.showToast({ title: '年龄无效', icon: 'none' }); return;
    }
    const s0 = getSlotProfile(0), s1 = getSlotProfile(1);
    const user = { slot: 0, name: form.name, gender: parseInt(form.gender), age, handedness: parseInt(form.handedness) };
    
    const doSave = (slot) => {
      user.slot = slot;
      saveSlotProfile(slot, user);
      setCurrentUser(user);
      this.setData({ currentUser: user, userMetaStr: this._buildUserMeta(user), showUserForm: false });
      
      if (wifiClient.isConnected()) {
        wifiClient.sendCmd('save_user', {
          slot, user_id: `user_${slot}`, name: user.name,
          age: user.age, gender: user.gender, handedness: user.handedness
        }).then(res => {
          wx.showToast({ title: res.ok ? `已保存到用户${slot + 1}` : '设备保存失败', icon: 'none' });
        }).catch(() => {
          wx.showToast({ title: '同步设备失败', icon: 'none' });
        });
      } else {
        wx.showToast({ title: `已保存到用户${slot + 1}`, icon: 'none' });
      }
    };
    
    const cur = this.data.currentUser;
    if (cur && this.data.isEditUser) {
      doSave(cur.slot);
    } else if (!s0) {
      doSave(0);
    } else if (!s1) {
      doSave(1);
    } else {
      wx.showActionSheet({
        itemList: [`覆盖用户1: ${s0.name}`, `覆盖用户2: ${s1.name}`],
        success: (res) => { doSave(res.tapIndex); },
        fail: () => { this.setData({ showUserForm: true }); }
      });
    }
  },

  onSwitchUser() {
    const slot0 = getSlotProfile(0);
    const slot1 = getSlotProfile(1);
    if (!slot0 && !slot1) {
      wx.showToast({ title: '暂无已存用户，请先录入', icon: 'none' }); return;
    }
    const items = [
      slot0 ? `用户1: ${slot0.name}` : '用户1: (空)',
      slot1 ? `用户2: ${slot1.name}` : '用户2: (空)',
      '新建用户...',
    ];
    if (this.data.currentUser) {
      items.push(`删除「${this.data.currentUser.name}」`);
    }
    wx.showActionSheet({
      itemList: items,
      success: (res) => {
        if (res.tapIndex === 2) {
          this.setData({ showUserForm: true, isEditUser: false });
        } else if (res.tapIndex === 3 && this.data.currentUser) {
          this.onDeleteUser();
        } else {
          const slot = res.tapIndex;
          const profile = slot === 0 ? slot0 : slot1;
          if (!profile) { wx.showToast({ title: '该用户为空', icon: 'none' }); return; }
          setCurrentUser(profile);
          this.setData({ currentUser: profile, userMetaStr: this._buildUserMeta(profile), showSwitchPanel: false });
          wx.showToast({ title: `已切换: ${profile.name}`, icon: 'none' });
          // 通知固件切换活跃用户
          if (wifiClient.isConnected()) {
            wifiClient.sendCmd('load_user', { slot: profile.slot }).catch(() => {});
          }
        }
      }
    });
  },

  onDeleteUser() {
    const user = this.data.currentUser;
    if (!user) return;
    wx.showModal({
      title: '确认删除',
      content: `确定删除用户「${user.name}」？此操作不可恢复。`,
      confirmText: '删除',
      confirmColor: '#e74c3c',
      success: (res) => {
        if (!res.confirm) return;
        saveSlotProfile(user.slot, null);
        const otherSlot = user.slot === 0 ? 1 : 0;
        const other = getSlotProfile(otherSlot);
        if (other) {
          setCurrentUser(other);
          this.setData({ currentUser: other, userMetaStr: this._buildUserMeta(other) });
          // 通知固件切换活跃用户
          if (wifiClient.isConnected()) {
            wifiClient.sendCmd('load_user', { slot: other.slot }).catch(() => {});
          }
          wx.showToast({ title: `已删除，已切换到${other.name}`, icon: 'none' });
        } else {
          setCurrentUser(null);
          this.setData({ currentUser: null, userMetaStr: '' });
          // 删除最后一个用户，通知固件重置校准
          if (wifiClient.isConnected()) {
            wifiClient.sendCmd('reset_calib', { slot: user.slot }).catch(() => {});
          }
          wx.showToast({ title: '已删除用户', icon: 'none' });
        }
      }
    });
  },

  // ===================== 校准流程 =====================
  async startCalibration() {
    logger.log('[calibrate] startCalibration entered');
    const user = getCurrentUser();
    if (!user) { logger.log('[calibrate] no user'); this.onFirstUseCalibrate(); return; }
    if (!this.data.connected) { logger.log('[calibrate] not connected'); wx.showToast({ title: '请先连接设备', icon: 'none' }); return; }
    if (!this._checkWifi()) { logger.log('[calibrate] wifi check failed'); return; }

    this._resetCalib();
    this.setData({ phase: PHASE.RESTING, statusText: '请保持放松，采集静息态数据...', countdown: REST_DURATION });

    try {
      logger.log('[calibrate] sending start_calib REST cmd...');
      await wifiClient.sendQuery('start_calib', { phase: 'REST', slot: user.slot });
      logger.log('[calibrate] start_calib REST ack received');
    } catch(e) {
      logger.log('[calibrate] start_calib REST failed: ' + (e.message || e));
      wx.showToast({ title: '启动校准失败', icon: 'none' }); this._resetCalib(); return;
    }

    await this._runRestCountdown();
    if (!wifiClient.isConnected()) { this._resetCalib(); return; }
  },

  _runRestCountdown() {
    return new Promise((resolve) => {
      this._registerCalibCallbacks();
      
      let elapsed = 0;
      const interval = setInterval(() => {
        elapsed++;
        this.setData({ countdown: REST_DURATION - elapsed });
        if (elapsed >= REST_DURATION) {
          clearInterval(interval);
          this._unregisterCalibCallbacks();
          this.setData({ phase: PHASE.MAX_READY, countdown: 0, statusText: '请握紧拳头至最大力，准备好了就点击下方按钮' });
          resolve();
        }
      }, 1000);
      this._restInterval = interval;
    });
  },

  // ===================== MAX 阶段 =====================
  async onStartMax() {
    logger.log('[calibrate] onStartMax clicked');
    if (!this._checkWifi()) return;
    const user = getCurrentUser();
    if (!user) { wx.showToast({ title: '请先录入用户信息', icon: 'none' }); return; }
    this.setData({ phase: PHASE.MAX_CONTRACT, statusText: '请全力握紧拳头！', countdown: MAX_DURATION });
    
    try {
      logger.log('[calibrate] sending start_calib MAX cmd...');
      await wifiClient.sendQuery('start_calib', { phase: 'MAX', slot: user.slot });
      logger.log('[calibrate] start_calib MAX ack received');
    } catch(e) {
      logger.log('[calibrate] start_calib MAX failed: ' + (e.message || e));
      wx.showToast({ title: '启动MAX失败', icon: 'none' }); this._resetCalib(); return;
    }
    await this._runMaxCountdown();
    if (!wifiClient.isConnected()) { this._resetCalib(); return; }
    // 等待固件完成 MAX 阶段处理（endPhase + 状态切换）
    await new Promise(r => setTimeout(r, 500));
    this.setData({ phase: PHASE.RESULT, statusText: '计算中...', countdown: 0 });
    // ✅ 使用本地缓存数据验证，不调用 get_calib_result（避免栈溢出崩溃）
    this._validateResult();
  },

  _runMaxCountdown() {
    return new Promise((resolve) => {
      this._registerCalibCallbacks();
      
      let elapsed = 0;
      const interval = setInterval(() => {
        elapsed++;
        this.setData({ countdown: MAX_DURATION - elapsed });
        if (elapsed >= MAX_DURATION) {
          clearInterval(interval);
          this._unregisterCalibCallbacks();
          resolve();
        }
      }, 1000);
      this._maxInterval = interval;
    });
  },

  // ===================== 结果校验（✅ 只用本地数据，不发 get_calib_result） =====================
  _validateResult() {
    const {
      liveRestRms,
      liveMaxRms,
      liveRestMdf,
      liveMaxMdf,
    } = this.data;

    if (
      liveRestRms == null ||
      liveMaxRms == null ||
      liveRestMdf == null ||
      liveMaxMdf == null
    ) {
      this.setData({
        validation: { ok: false, rms_ok: false, mdf_ok: false },
        statusText: '数据不完整，请重新校准',
      });
      return;
    }

    const rms_ok =
      liveMaxRms > liveRestRms * 2.0 && liveMaxRms >= 0.5;

    const mdf_ok =
      liveRestMdf >= 10 && liveRestMdf <= 250 &&
      liveMaxMdf >= 10 && liveMaxMdf <= 250;

    this.setData({
      validation: { ok: rms_ok && mdf_ok, rms_ok, mdf_ok },
      statusText: rms_ok && mdf_ok ? '校准通过' : '校准未达标',
    });
  },

  /**
   * 确认保存校准数据（PASS时点击）
   * 保存后不自动跳转，在同页面显示保存成功状态
   */
  async _doSaveCalib() {
    const user = this.data.currentUser;
    if (!user) { wx.showToast({ title: '请先录入用户信息', icon: 'none' }); return; }
    const { slot } = user;
    try {
      const res = await wifiClient.sendQuery('save_calib', { slot });
      if (res && res.ok) {
        wx.showToast({ title: '已保存', icon: 'success' });
        this.setData({ saved: true, statusText: '' });
      } else {
        const err = (res && res.err) ? res.err : '固件拒绝保存';
        wx.showModal({ title: '保存失败', content: err, showCancel: false });
      }
    } catch(e) {
      wx.showToast({ title: '网络错误，保存失败', icon: 'none' });
    }
  },

  async onConfirmResult() {
    const { validation } = this.data;
    if (!validation.ok) {
      const reasons = [];
      if (!validation.rms_ok) reasons.push('RMS未达标准(需>2倍静息值且>=0.5mV)');
      if (!validation.mdf_ok) reasons.push('MDF超出范围(需10-250Hz)');
      wx.showModal({
        title: '校准数据偏低',
        content: reasons.join('\n') + '\n是否仍要保存？',
        success: (res) => { if (res.confirm) this._doSaveCalib(); }
      });
      return;
    }
    // PASS：直接保存
    await this._doSaveCalib();
  },

  /**
   * 保存成功后，进入实时监测（跳转realtime Tab）
   */
  onGoToMonitor() {
    logger.log('[calibrate] onGoToMonitor: switching to realtime tab');
    // 固件在 save_calib 后已转 ST_IDLE，发 start_stream 进入 MONITORING
    // 用send(fire-and-forget)避免与realtime页面的start_stream冲突
    if (wifiClient.isConnected()) {
      wifiClient.send({ cmd: 'start_stream' }).catch(() => {});
    }
    wx.switchTab({ url: '/pages/realtime/index' });
  },

  /**
   * 重新校准（FAIL时或保存后都可以用）
   * 清空所有状态，回到IDLE页面
   */
  onRetryCalib() {
    logger.log('[calibrate] onRetryCalib clicked');
    this._resetCalib();
  },

  // ===================== 清理 =====================
  _resetCalib() {
    logger.log('[calibrate] _resetCalib');
    if (this._restInterval) { clearInterval(this._restInterval); this._restInterval = null; }
    if (this._maxInterval)  { clearInterval(this._maxInterval);  this._maxInterval  = null; }
    if (this._calibHandler) { wifiClient.offCalibData(this._calibHandler); this._calibHandler = null; }
    // 通知固件重置校准状态，让固件回到 IDLE
    if (wifiClient.isConnected()) {
      // 【修复】改为 fire-and-forget，避免固件发 ACK 时 WebSocket 回调栈溢出崩溃
      // 固件 handleResetCalib() 只做状态重置，不需要等 ACK
      wifiClient.send({ cmd: 'reset_calib', slot: getCurrentUser() ? getCurrentUser().slot : 0 });
    }
    this.setData({
      phase: PHASE.IDLE,
      statusText: '点击下方按钮开始校准',
      validation: null,
      saved: false,
      countdown: 0,
      liveRestRms: null, liveRestMdf: null,
      liveMaxRms: null, liveMaxMdf: null
    });
  },

  // helpers
  formatGender(v)  { return ['', '男', '女'][v] || '-'; },
  formatHand(v)   { return ['', '左手腕', '右手腕'][v] || '-'; },
  formatAgeGroup(v) { return ['<18','18-35','36-55','56+'][v] || '-'; },
  _buildUserMeta(u) {
    if (!u) return '';
    let s = `存储位置：${u.slot} | ${u.age}岁`;
    if (u.gender) s += ` | ${this.formatGender(u.gender)}`;
    if (u.handedness) s += ` | ${this.formatHand(u.handedness)}`;
    return s;
  },
});

