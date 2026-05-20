// pages/calibrate/index.js - 校准页
// 职责：用户profile管理 + 双阶段校准（REST 10s + MAX 15s）+ 曲线拟合
const app = getApp();
const storage = require('../../utils/storage.js');
const { getCurrentUser, setCurrentUser, getSlotProfile, saveSlotProfile, getAgeGroup } = storage;
const wifiClient = require('../../utils/wifiClient.js');

// ===================== 状态常量 =====================
const PHASE = {
  IDLE:          'idle',
  RESTING:       'resting',     // 静息态采集 10s
  MAX_READY:     'max_ready',   // 等待用户点击"开始MAX"按钮
  MAX_CONTRACT:  'max_contract',// 最大收缩采集 15s
  RESULT:        'result',      // 结果展示
  SAVED:         'saved',       // 已保存，等待曲线拟合
  FITTING:       'fitting',     // 拟合中
  FIT_DONE:      'fit_done',    // 拟合完成
};
const REST_DURATION = 10;   // 秒
const MAX_DURATION  = 15;   // 秒

// ===================== Page =====================
Page({
  data: {
    phase: PHASE.IDLE,
    connected: false,
    deviceId: null,
    currentUser: null,
    userMetaStr: '',
    showUserForm: false,
    showSwitchPanel: false,
    calibData: null,
    validation: null,
    liveRestRms: null, liveRestMdf: null,
    liveMaxRms: null,  liveMaxMdf:  null,
    countdown: 0,
    statusText: '点击下方按钮开始校准',
  },

  onLoad() {
    this._refreshUser();
  },

  onShow() {
    this.setData({ connected: wifiClient.isConnected() });
    this._refreshUser();
  },

  onHide() {
    // 不在 onHide 重置，切换 Tab 后返回保留校准进度
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
    // 录入个人信息是纯本地操作，不依赖网络
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
      // 修改个人信息：直接覆盖当前用户
      doSave(cur.slot);
    } else if (!s0) {
      doSave(0);
    } else if (!s1) {
      doSave(1);
    } else {
      // 新建但两个槽位都有人，让用户选择覆盖哪个
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
      items.push(`🗑️ 删除「${this.data.currentUser.name}」`);
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
          wx.showToast({ title: `已删除，已切换到${other.name}`, icon: 'none' });
        } else {
          setCurrentUser(null);
          this.setData({ currentUser: null, userMetaStr: '' });
          wx.showToast({ title: '已删除用户', icon: 'none' });
        }
      }
    });
  },
  async startCalibration() {
    const user = getCurrentUser();
    if (!user) { this.onFirstUseCalibrate(); return; }
    if (!this.data.connected) { wx.showToast({ title: '请先连接设备', icon: 'none' }); return; }
    if (!this._checkWifi()) return;

    this._resetCalib();
    this.setData({ phase: PHASE.RESTING, statusText: '请保持放松，采集静息态数据...', countdown: REST_DURATION });

    try {
      await wifiClient.sendCmd('start_calib', { phase: 'REST', slot: user.slot });
    } catch(e) {
      wx.showToast({ title: '启动校准失败', icon: 'none' }); this._resetCalib(); return;
    }

    // REST 倒计时完成后进入 MAX_READY 等待态
    await this._runRestCountdown();
    if (!wifiClient.isConnected()) { this._resetCalib(); return; }
  },

  // REST 阶段倒计时
  _runRestCountdown() {
    return new Promise((resolve) => {
      let elapsed = 0;
      const handler = (data) => {
        if (data.type !== 'calib_data' || data.phase !== 'REST') return;
        this.setData({ liveRestRms: data.rms, liveRestMdf: data.mdf });
      };
      wifiClient.onCalibData(handler);
      const interval = setInterval(() => {
        elapsed++;
        this.setData({ countdown: REST_DURATION - elapsed });
        if (elapsed >= REST_DURATION) {
          clearInterval(interval);
          wifiClient.offCalibData(handler);
          // REST 完成后进入 MAX_READY 等待态
          this.setData({ phase: PHASE.MAX_READY, countdown: 0, statusText: '请握紧拳头至最大力，准备好了就点击下方按钮' });
          resolve();
        }
      }, 1000);
      this._restInterval = interval;
      this._restHandler  = handler;
    });
  },

  // ===================== MAX 阶段 =====================
  async onStartMax() {
    if (!this._checkWifi()) return;
    const user = getCurrentUser();
    this.setData({ phase: PHASE.MAX_CONTRACT, statusText: '请全力握紧拳头！', countdown: MAX_DURATION });
    try {
      await wifiClient.sendCmd('start_calib', { phase: 'MAX', slot: user.slot });
    } catch(e) {
      wx.showToast({ title: '启动MAX失败', icon: 'none' }); this._resetCalib(); return;
    }
    await this._runMaxCountdown();
    if (!wifiClient.isConnected()) { this._resetCalib(); return; }
    this.setData({ phase: PHASE.RESULT, statusText: '计算中...', countdown: 0 });
    await this._fetchResult();
  },

  _runMaxCountdown() {
    return new Promise((resolve) => {
      let elapsed = 0;
      const handler = (data) => {
        if (data.type !== 'calib_data' || data.phase !== 'MAX') return;
        this.setData({ liveMaxRms: data.rms, liveMaxMdf: data.mdf });
      };
      wifiClient.onCalibData(handler);
      const interval = setInterval(() => {
        elapsed++;
        this.setData({ countdown: MAX_DURATION - elapsed });
        if (elapsed >= MAX_DURATION) {
          clearInterval(interval);
          wifiClient.offCalibData(handler);
          resolve();
        }
      }, 1000);
      this._maxInterval = interval;
      this._maxHandler  = handler;
    });
  },

  async _fetchResult() {
    const user = getCurrentUser();
    try {
      const res = await wifiClient.sendCmd('get_calib_result', { slot: user.slot });
      this.setData({ calibData: res });
      this._validateResult(res);
    } catch(e) {
      wx.showToast({ title: '读取结果失败', icon: 'none' }); this._resetCalib();
    }
  },

  _validateResult(data) {
    const { rest_rms, max_rms, rest_mdf, max_mdf } = data;
    const rms_ok = max_rms > rest_rms * 2;
    const mdf_ok = max_mdf < rest_mdf * 0.9;
    const mdf_drop_pct = rest_mdf > 0 ? (1 - max_mdf / rest_mdf) * 100 : 0;
    this.setData({ validation: { ok: rms_ok && mdf_ok, rms_ok, mdf_ok, mdf_drop_pct } });
  },

  async onConfirmResult() {
    if (!this.data.validation.ok) {
      wx.showModal({ title: '校准失败', content: '请重新校准，确保最大收缩时MDF下降≥10%', showCancel: false }); return;
    }
    const { slot } = this.data.currentUser;
    try {
      await wifiClient.sendCmd('save_calib', { slot });
      wx.showToast({ title: '已保存', icon: 'success' });
      this.setData({ phase: PHASE.SAVED, fitAsked: false });
      // 等视图渲染完 SAVED 状态后再弹曲线询问框
      wx.nextTick(() => { this._askFitCurve(); });
    } catch(e) {
      wx.showToast({ title: '保存失败', icon: 'none' });
    }
  },

  onRetryCalib() {
    this._resetCalib();
    this.setData({ phase: PHASE.IDLE, statusText: '点击下方按钮开始校准', liveRestRms: null, liveRestMdf: null, liveMaxRms: null, liveMaxMdf: null });
  },

  // ===================== 曲线拟合流程 =====================
  _askFitCurve() {
    wx.showModal({
      title: '曲线拟合',
      content: '是否使用参考曲线进行个人化拟合？',
      confirmText: '拟合',
      cancelText: '跳过',
      success: (res) => {
        this.setData({ fitAsked: true });
        if (res.confirm) this._doFitCurve();
        else this._saveNoCurve();
      }
    });
  },

  async _doFitCurve() {
    if (!this._checkWifi()) return;
    this.setData({ phase: PHASE.FITTING, statusText: '匹配参考曲线...' });
    try {
      const user = this.data.currentUser;
      const curves = await wifiClient.sendCmd('list_curves', {
        gender: user.gender,
        handedness: user.handedness,
        age: user.age
      });
      if (!curves || curves.length === 0) { wx.showToast({ title: '曲线库为空', icon: 'none' }); this._saveNoCurve(); return; }
      const matched = curves.filter(c =>
        c.status === 1 && c.gender === user.gender &&
        c.age_min <= user.age && user.age <= c.age_max &&
        c.handedness === user.handedness
      );
      if (matched.length === 0) { wx.showToast({ title: '该分组暂无参考曲线', icon: 'none' }); this._saveNoCurve(); return; }
      if (matched.length > 1) {
        wx.showActionSheet({ itemList: matched.map(c => `${c.name || '曲线' + c.curve_id}`), success: (r) => this._execFit(matched[r.tapIndex]) });
        return;
      }
      await this._execFit(matched[0]);
    } catch(e) { wx.showToast({ title: '曲线匹配失败', icon: 'none' }); this._saveNoCurve(); }
  },

  async _execFit(curve) {
    this.setData({ phase: PHASE.FITTING, statusText: '执行拟合...', matchedCurve: curve });
    try {
      // [修复] save_curve 只需要 curve_id，固件从RAM读取校准数据
      await wifiClient.sendCmd('save_curve', { curve_id: curve.curve_id });
      this.setData({ phase: PHASE.FIT_DONE, fitResult: { ok: true, has_curve: true, curve_id: curve.curve_id, curve_name: curve.name } });
      wx.showModal({ title: '拟合完成', content: `已使用 "${curve.name || '曲线' + curve.curve_id}" 完成个人化拟合`, showCancel: false });
    } catch(e) { wx.showToast({ title: '拟合保存失败', icon: 'none' }); this._saveNoCurve(); }
  },

  _saveNoCurve() {
    this.setData({ phase: PHASE.FIT_DONE, fitResult: { ok: true, has_curve: false } });
  },

  // ===================== 清理 =====================
  _resetCalib() {
    if (this._restInterval) { clearInterval(this._restInterval); this._restInterval = null; }
    if (this._maxInterval)  { clearInterval(this._maxInterval);  this._maxInterval  = null; }
    if (this._restHandler)  { wifiClient.offCalibData(this._restHandler); this._restHandler = null; }
    if (this._maxHandler)   { wifiClient.offCalibData(this._maxHandler);  this._maxHandler  = null; }
    this.setData({ phase: PHASE.IDLE, calibData: null, validation: null, countdown: 0, liveRestRms: null, liveRestMdf: null, liveMaxRms: null, liveMaxMdf: null });
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
