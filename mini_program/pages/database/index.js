// pages/database/index.js - 建库页
// 职责：新建档案(校准+5min采集+4段标记) + 记录列表 + 曲线库管理
const app = getApp();
const storage = require('../../utils/storage.js');
const { getAgeGroup } = storage;
const wifiClient = require('../../utils/wifiClient.js');

// ===================== 状态常量 =====================
const DB_PHASE = {
  IDLE:         'idle',
  FORM:         'form',        // 填写受试者信息
  CALIBRATING:  'calibrating', // 校准中
  COLLECTING:   'collecting',  // 5分钟采集中
  MARKING:      'marking',     // 4段疲劳标记
  SAVING:       'saving',      // 保存中
  SAVED:        'saved',       // 保存完成
};

// 校准子阶段（独立于 calibrate 页面）
const CALIB_PHASE = {
  IDLE:         'idle',
  RESTING:      'resting',      // 静息态采集 10s
  MAX_READY:    'max_ready',    // 等待用户点击"开始MAX"
  MAX_CONTRACT: 'max_contract', // 最大收缩采集 15s
  RESULT:       'result',       // 结果展示
};

const REST_DURATION = 10;  // 秒
const MAX_DURATION  = 15;  // 秒
const MARK_LABELS = ['放松', '初步疲劳', '中等疲劳', '重度疲劳'];

// ===================== Page =====================
Page({
  data: {
    dbPhase: DB_PHASE.IDLE,
    calibPhase: CALIB_PHASE.IDLE,
    MARK_LABELS: ['放松', '初步疲劳', '中等疲劳', '重度疲劳'],
    connected: false,
    deviceId: null,
    // 弹窗控制
    showDbForm: false,
    // 表单临时数据
    _dbForm: { name: '', gender: '1', age: '', handedness: '2' },
    // 受试者信息
    subject: null,  // {name, gender, age, handedness, slot}
    // 校准数据
    calibData: null,
    calibValidation: null,
    liveRestRms: null, liveRestMdf: null,
    liveMaxRms: null,  liveMaxMdf:  null,
    calibCountdown: 0,
    calibStatusText: '',
    // 采集进度
    elapsedSec: 0,   // 已采集秒数
    totalSec: 300,   // 总计300秒
    currentMark: 0, // [Bug A fix] 初始=0，第一个标记按钮可点击, // 当前标记到第几段(-1=未开始)
    stageCompleted: [false, false, false, false], // 各阶段原始数据是否采集完成
    isRecording: false, // 当前是否正在录制原始数据
    // 实时数据（采集中显示）
    liveRms: null,
    liveMdf: null,
    liveFatigue: null,
    liveFatigueStr: '--',
    // 记录列表
    showRecordModal: false,
    recordList: [],
    // 曲线库
    showCurveLibModal: false,
    curveList: [],
    // 状态文本
    statusText: '',
  },

  _realtimeHandler: null,
  _restInterval: null,
  _maxInterval: null,
  _restHandler: null,
  _maxHandler: null,

  // ===================== 生命周期 =====================
  onLoad() {},

  onUnload() {
    this._stopRealtimeListener();
    this._stopCalibListeners();
    this._stopPushListener();
    if (this._collectionTimer) { clearInterval(this._collectionTimer); this._collectionTimer = null; }
  },

  onShow() {
    this.setData({ connected: wifiClient.isConnected() });
  },

  // ===================== 主入口 =====================
  onNewRecord() {
    // 恢复上次未完成的录入
    const pending = wx.getStorageSync('_pendingDbSubject');
    if (pending) {
      this.setData({ 
        showDbForm: true, 
        _dbForm: { 
          name: pending.name, 
          gender: String(pending.gender), 
          age: String(pending.age), 
          handedness: String(pending.handedness) 
        }
      });
      wx.removeStorageSync('_pendingDbSubject');
      return;
    }
    this.setData({ showDbForm: true, _dbForm: { name: '', gender: '1', age: '', handedness: '2' } });
  },

  _checkWifi() {
    if (!wifiClient.isConnected()) {
      wx.showToast({ title: '请先在"网络配置"页连接设备', icon: 'none' });
      return false;
    }
    return true;
  },

  // ===================== 弹窗表单（录入受试者信息）=====================
  onDbFormInput(e) {
    const key = e.currentTarget.id;
    if (!key) return;
    const form = { ...this.data._dbForm };
    form[key] = e.detail.value;
    this.setData({ _dbForm: form });
  },

  onDbFormRadio(e) {
    const key = e.currentTarget.name;
    if (!key) return;
    const form = { ...this.data._dbForm };
    form[key] = e.detail.value;
    this.setData({ _dbForm: form });
  },

  onDbFormConfirm() {
    const f = this.data._dbForm;
    if (!f.name || !f.gender || !f.age || !f.handedness) {
      wx.showToast({ title: '请填写完整信息', icon: 'none' }); return;
    }
    const age = parseInt(f.age);
    if (isNaN(age) || age < 1 || age > 120) {
      wx.showToast({ title: '年龄无效', icon: 'none' }); return;
    }
    
    const subject = { name: f.name, gender: parseInt(f.gender), age, handedness: parseInt(f.handedness), slot: -1 };
    
    // 离线保存：纯本地操作不阻塞
    if (!wifiClient.isConnected()) {
      wx.setStorageSync('_pendingDbSubject', subject);
      wx.showToast({ title: '已保存，连设备后继续', icon: 'none' });
      this.setData({ showDbForm: false });
      return;
    }
    
    this.setData({ showDbForm: false, subject });
    this._startCalibration();
  },

  onCancelForm() {
    this.setData({ showDbForm: false });
  },

  // ===================== 旧表单提交（保留兼容）=====================
  onFormSubmit(e) {
    this.onDbFormConfirm();
  },

  // ===================== 校准流程（独立，不含曲线拟合）=====================
  async _startCalibration() {
    if (!this._checkWifi()) return;

    this._resetCalib();
    this.setData({
      dbPhase: DB_PHASE.CALIBRATING,
      calibPhase: CALIB_PHASE.RESTING,
      calibStatusText: '请保持放松，采集静息态数据...',
      calibCountdown: REST_DURATION,
    });

    try {
      await wifiClient.sendCmd('start_calib', { phase: 'REST', slot: 0 });
    } catch(e) {
      wx.showToast({ title: '启动校准失败', icon: 'none' });
      this._resetCalib();
      this.setData({ dbPhase: DB_PHASE.FORM, calibPhase: CALIB_PHASE.IDLE });
      return;
    }

    await this._runRestCountdown();
    if (!wifiClient.isConnected()) {
      this._resetCalib();
      this.setData({ dbPhase: DB_PHASE.FORM, calibPhase: CALIB_PHASE.IDLE });
      return;
    }
  },

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
        this.setData({ calibCountdown: REST_DURATION - elapsed });
        if (elapsed >= REST_DURATION) {
          clearInterval(interval);
          wifiClient.offCalibData(handler);
          this.setData({
            calibPhase: CALIB_PHASE.MAX_READY,
            calibCountdown: 0,
            calibStatusText: '请握紧拳头至最大力，准备好了就点击下方按钮',
          });
          resolve();
        }
      }, 1000);
      this._restInterval = interval;
      this._restHandler = handler;
    });
  },

  async onStartMax() {
    if (!this._checkWifi()) return;
    this.setData({
      calibPhase: CALIB_PHASE.MAX_CONTRACT,
      calibStatusText: '请全力握紧拳头！',
      calibCountdown: MAX_DURATION,
    });

    try {
      await wifiClient.sendCmd('start_calib', { phase: 'MAX', slot: 0 });
    } catch(e) {
      wx.showToast({ title: '启动MAX失败', icon: 'none' });
      this._resetCalib();
      this.setData({ dbPhase: DB_PHASE.FORM, calibPhase: CALIB_PHASE.IDLE });
      return;
    }

    await this._runMaxCountdown();
    if (!wifiClient.isConnected()) {
      this._resetCalib();
      this.setData({ dbPhase: DB_PHASE.FORM, calibPhase: CALIB_PHASE.IDLE });
      return;
    }

    this.setData({ calibPhase: CALIB_PHASE.RESULT, calibStatusText: '计算中...', calibCountdown: 0 });
    await this._fetchCalibResult();
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
        this.setData({ calibCountdown: MAX_DURATION - elapsed });
        if (elapsed >= MAX_DURATION) {
          clearInterval(interval);
          wifiClient.offCalibData(handler);
          resolve();
        }
      }, 1000);
      this._maxInterval = interval;
      this._maxHandler = handler;
    });
  },

  async _fetchCalibResult() {
    try {
      const res = await wifiClient.sendCmd('get_calib_result', { slot: 0 });
      this.setData({ calibData: res });
      this._validateCalibResult(res);
    } catch(e) {
      wx.showToast({ title: '读取结果失败', icon: 'none' });
      this._resetCalib();
      this.setData({ dbPhase: DB_PHASE.FORM, calibPhase: CALIB_PHASE.IDLE });
    }
  },

  _validateCalibResult(data) {
    const { rest_rms, max_rms, rest_mdf, max_mdf } = data;
    const rms_ok = max_rms > rest_rms * 2;
    const mdf_ok = max_mdf < rest_mdf * 0.9;
    const mdf_drop_pct = rest_mdf > 0 ? (1 - max_mdf / rest_mdf) * 100 : 0;
    this.setData({ calibValidation: { ok: rms_ok && mdf_ok, rms_ok, mdf_ok, mdf_drop_pct } });
  },

  async onConfirmCalibResult() {
    if (!this.data.calibValidation.ok) {
      wx.showModal({
        title: '校准未通过',
        content: '请重新校准，确保最大收缩时MDF下降≥10%',
        showCancel: false,
      });
      return;
    }
    // 校准通过，直接进入采集阶段
    wx.showToast({ title: '校准通过', icon: 'success' });
    this._resetCalib();
    this._startCollection();
  },

  onRetryCalib() {
    this._resetCalib();
    this._startCalibration();
  },

  onCancelCalib() {
    this._resetCalib();
    this.setData({ dbPhase: DB_PHASE.IDLE, calibPhase: CALIB_PHASE.IDLE });
  },

  _resetCalib() {
    this._stopCalibListeners();
    this.setData({
      calibPhase: CALIB_PHASE.IDLE,
      calibData: null,
      calibValidation: null,
      calibCountdown: 0,
      liveRestRms: null, liveRestMdf: null,
      liveMaxRms: null,  liveMaxMdf:  null,
      calibStatusText: '',
    });
  },

  _stopCalibListeners() {
    if (this._restInterval) { clearInterval(this._restInterval); this._restInterval = null; }
    if (this._maxInterval) { clearInterval(this._maxInterval); this._maxInterval = null; }
    if (this._restHandler) { wifiClient.offCalibData(this._restHandler); this._restHandler = null; }
    if (this._maxHandler) { wifiClient.offCalibData(this._maxHandler); this._maxHandler = null; }
  },

  // ===================== 采集流程 =====================
  _startRealtimeListener() {
    if (this._realtimeHandler) { wifiClient.offRealtimeData(this._realtimeHandler); }
    this._realtimeHandler = (d) => {
      const rms = d.r != null ? d.r : (d.rms != null ? d.rms : null);
      const mdf = d.m != null ? d.m : (d.mdf != null ? d.mdf : null);
      const fat = d.f != null ? d.f : (d.fatigue != null ? d.fatigue : null);
      this.setData({ liveRms: rms, liveMdf: mdf, liveFatigue: fat, liveFatigueStr: (fat !== null && !isNaN(fat)) ? (fat * 100).toFixed(1) + '%' : '--' });
    };
    wifiClient.onRealtimeData(this._realtimeHandler);
  },

  _stopRealtimeListener() {
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
  },

  async _startCollection() {
    this._startRealtimeListener();
    this._startPushListener();
    this.setData({
      dbPhase: DB_PHASE.COLLECTING,
      elapsedSec: 0,
      currentMark: 0,
      statusText: '开始采集...',
      liveRms: null, liveMdf: null, liveFatigue: null, liveFatigueStr: '--',
    });
    try {
      await wifiClient.sendCmd('start_db_feature', { subject_id: Math.floor(Date.now() / 1000), age: this.data.subject.age, gender: this.data.subject.gender, handedness: this.data.subject.handedness });
    } catch(e) {
      wx.showToast({ title: '启动建库失败', icon: 'none' }); this._resetAll(); return;
    }

    // 启动定时器：每秒更新elapsedSec
    this._collectionTimer = setInterval(() => {
      const cur = this.data.elapsedSec + 1;
      this.setData({ elapsedSec: cur });
      if (cur >= 300) {
        clearInterval(this._collectionTimer);
        this._collectionTimer = null;
        this._stopRealtimeListener();
        this._doSave();
      }
    }, 1000);
  },

  // ===================== 标记流程（采集中即时标记）=====================
  async onMarkConfirm() {
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
  },

  // ===================== 保存流程 =====================
  async _doSave() {
    this.setData({ dbPhase: DB_PHASE.SAVING, statusText: '保存中...' });
    try {
      const res = await wifiClient.sendCmd('raw_phase_done');
      const slot = res && res.slot !== undefined ? res.slot : -1;
      this.setData({ subject: { ...this.data.subject, slot }, dbPhase: DB_PHASE.SAVED, statusText: `已保存到用户${slot + 1}` });
      wx.showToast({ title: `建库完成，用户${slot + 1}`, icon: 'success' });
    } catch(e) {
      wx.showToast({ title: '保存失败', icon: 'none' }); this._resetAll();
    }
  },

  // ===================== 记录列表弹窗 =====================
  async onShowRecords() {
    if (!this._checkWifi()) return;
    this.setData({ showRecordModal: true });
    try {
      const records = await wifiClient.sendCmd('list_records', {});
      this.setData({ recordList: records || [] });
    } catch(e) { this.setData({ recordList: [] }); }
  },

  onCloseRecords() { this.setData({ showRecordModal: false }); },

  async onDeleteRecord(e) {
    const slot = e.currentTarget.dataset.slot;
    wx.showModal({
      title: '确认删除',
      content: `删除用户${slot + 1}的建库档案？`,
      success: async (res) => {
        if (!res.confirm) return;
        try {
          await wifiClient.sendCmd('delete_record', { slot });
          wx.showToast({ title: '已删除', icon: 'success' });
          this.onShowRecords();
        } catch(e) { wx.showToast({ title: '删除失败', icon: 'none' }); }
      }
    });
  },

  // ===================== 曲线库弹窗 =====================
  async onShowCurves() {
    if (!this._checkWifi()) return;
    this.setData({ showCurveLibModal: true });
    try {
      const curves = await wifiClient.sendCmd('list_curves', {});
      this.setData({ curveList: curves || [] });
    } catch(e) { this.setData({ curveList: [] }); }
  },

  onCloseCurves() { this.setData({ showCurveLibModal: false }); },

  // ===================== 工具 =====================
  _resetAll() {
    if (this._collectionTimer) { clearInterval(this._collectionTimer); this._collectionTimer = null; }
    this._stopRealtimeListener();
    this._stopPushListener();
    this._resetCalib();
    this.setData({ dbPhase: DB_PHASE.IDLE, elapsedSec: 0, currentMark: -1, subject: null, stageCompleted: [false,false,false,false], isRecording: false });
  },

  onFinish() {
    this._resetAll();
  },

  formatTime(sec) {
    const m = Math.floor(sec / 60);
    const s = sec % 60;
    return `${m}:${String(s).padStart(2, '0')}`;
  },

  formatGender(v) { return ['', '男', '女'][v] || '-'; },
  formatHand(v)  { return ['', '左利', '右利'][v] || '-'; },
  formatAgeGroup(v) { return ['<18','18-35','36-55','56+'][v] || '-'; },
});
