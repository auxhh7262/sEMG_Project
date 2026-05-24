// pages/realtime/index.js
const app = getApp();
const { log, warn, error } = require('../../utils/logger');
const wifiClient = require('../../utils/wifiClient');
const storage = require('../../utils/storage');

const MAX_HISTORY = 5;
const HARD_LIMIT = MAX_HISTORY * 2;

Page({
  data: {
    connected: false,
    quality: '--',
    timeStr: '--:--:--',
    queueLength: 0,
    isReconnecting: false,
    connectionLost: false,
    historyRows: [],
    algorithm: '--',
  },

  _historyRows: [],
  _restRms: null,
  _maxRms: null,
  _restMdf: null,
  _maxMdf: null,
  _realtimeHandler: null,
  _tabVisible: false,
  _algorithmState: 'idle',
  _isResuming: false,
  _stallTimers: [],
  _lastRenderTime: 0,
  _lastSampleTime: 0,

  onLoad() {
    log('[realtime] onLoad()');
    this._loadCalibFromCache();
    this._realtimeHandler = (d) => { this._onSample(d); };
    wifiClient.onRealtimeData(this._realtimeHandler);
  },

  onShow() {
    log('[realtime] onShow()');
    this._tabVisible = true;
    this._algorithmState = 'idle';

    const cachedIp = wx.getStorageSync('device_ip');
    const cachedPort = wx.getStorageSync('device_port') || 8888;

    wifiClient.onStatusChange((status) => {
      if (status === 'connected') {
        this.setData({ connected: true, isReconnecting: false, connectionLost: false });
        this._resumeStream();
      } else if (status === 'connecting') {
        this.setData({ isReconnecting: true });
      } else {
        this.setData({ connected: false, isReconnecting: false, connectionLost: true });
        this._stopStream();
      }
    });

    if (!cachedIp) {
      this.setData({ connected: false, connectionLost: true });
      return;
    }

    if (wifiClient.isConnected()) {
      this.setData({ connected: true, connectionLost: false });
      this._resumeStream();
    } else if (wifiClient.isConnecting()) {
      this.setData({ isReconnecting: true });
    } else {
      this.setData({ isReconnecting: true });
      wifiClient.connect(cachedIp, cachedPort).catch(() => {});
    }
  },

  onHide() {
    this._tabVisible = false;
    this._stopStream();
    wifiClient.offStatusChange();
  },

  onUnload() {
    this._stopStream();
    wifiClient.offStatusChange();
    wifiClient.offRealtimeData();
    this._realtimeHandler = null;
  },

  _stopStream() {
    this._isResuming = false;
    this._clearStallTimers();
    this._lastSampleTime = 0;
    this._historyRows = [];
    this.setData({ queueLength: 0 });
    wifiClient.sendCmd('stop').catch(() => {});
  },

  async _resumeStream() {
    if (this._isResuming) return;
    this._isResuming = true;

    try {
      await wifiClient.sendCmd('start_stream', {});
      // ✅ 强制清卡顿 UI（极罕见时序防护）
      this.setData({
        quality: '--',
        timeStr: '--:--:--',
        connectionLost: false
      });
    } catch (e) {
      warn('[realtime] start_stream failed:', e);
    }

    if (this._algorithmState === 'idle') {
      this._algorithmState = 'loading';
      wifiClient.sendCmd('query_cz', {})
        .then(status => {
          if (status?.has_curve === true || status?.has_curve === 1) {
            this.setData({ algorithm: '个性化曲线' });
          } else if (status?.ref_mdf > 0) {
            this.setData({ algorithm: '默认MDF下降率' });
          } else {
            this.setData({ algorithm: '无校准' });
          }
        })
        .catch(() => {
          this.setData({ algorithm: '无校准' });
        })
        .finally(() => {
          this._algorithmState = 'done';
        });
    }

    this._isResuming = false;
  },

  _startStallTimer() {
    if (this._stallTimers.length > 0) return;
    if (!this._lastSampleTime) return;

    const mainTimer = setTimeout(() => {
      const gap = Date.now() - this._lastSampleTime;
      if (gap >= 5000 && this._tabVisible && wifiClient.isConnected()) {
        warn('[realtime] 数据超过5秒未更新，可能存在卡顿');
        this.setData({ quality: '卡顿', timeStr: '数据延迟' });
      }
      this._clearStallTimers();
    }, 5000);

    const guardTimer = setTimeout(() => {
      this._clearStallTimers();
    }, 6000);

    this._stallTimers = [mainTimer, guardTimer];
  },

  _clearStallTimers() {
    if (!this._stallTimers || !this._stallTimers.length) return;
    this._stallTimers.forEach(t => clearTimeout(t));
    this._stallTimers = [];
  },

  _loadCalibFromCache() {
    try {
      const c = wx.getStorageSync('calib_data');
      if (c?.rest_rms) {
        this._restRms = c.rest_rms;
        this._maxRms = c.max_rms;
        this._restMdf = c.rest_mdf;
        this._maxMdf = c.max_mdf;
      }
    } catch (_) {}
  },

  _onSample(d) {
    this._lastSampleTime = Date.now();
    this._clearStallTimers();
    this._startStallTimer();

    try {
      const { ts, r: rms, m: mdf, f: fatigue, q: quality, a: activation } = d;
      if (rms == null) return;

      let timeStr = '--';
      if (ts != null) {
        const date = new Date(ts);
        timeStr = `${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}.${String(date.getMilliseconds()).padStart(3, '0')}`;
      } else {
        // ✅ ts 为空时补齐毫秒位，格式完全统一
        const now = new Date();
        timeStr = `${String(now.getHours()).padStart(2, '0')}:${String(now.getMinutes()).padStart(2, '0')}:${String(now.getSeconds()).padStart(2, '0')}.${String(now.getMilliseconds()).padStart(3, '0')}`;
      }

      const actPct = activation != null ? Math.max(0, Math.min(100, activation * 100)) : null;
      const fatPct = fatigue != null ? Math.max(0, Math.min(100, fatigue * 100)) : null;

      const histRow = {
        time: timeStr,
        rms: rms.toFixed(2),
        act: actPct != null ? actPct.toFixed(1) + '%' : '--',
        mdf: mdf != null ? mdf.toFixed(1) : '--',
        fat: fatPct != null ? fatPct.toFixed(1) + '%' : '--',
        q: typeof quality === 'string' && quality ? quality : '--'
      };

      this._historyRows.push(histRow);
      if (this._historyRows.length > MAX_HISTORY) this._historyRows.shift();

      // ✅ 硬上限防护（极端异常兜底）
      if (this._historyRows.length > HARD_LIMIT) {
        this._historyRows.splice(0, this._historyRows.length - MAX_HISTORY);
      }

      this.setData({ queueLength: this._historyRows.length });

      const now = Date.now();
      if (now - this._lastRenderTime < 500) {
        this.setData({ connectionLost: !wifiClient.isConnected() });
        return;
      }

      this._lastRenderTime = now;
      this.setData({
        quality: typeof quality === 'string' && quality ? quality : '--',
        timeStr,
        connectionLost: !wifiClient.isConnected(),
        historyRows: this._historyRows
      });

    } catch (e) {
      error('[realtime] _onSample 内部崩溃:', e);
    }
  }
});