// pages/realtime/index.js
const app = getApp();
const { log, warn, error } = require('../../utils/logger');
const wifiClient = require('../../utils/wifiClient');
const storage = require('../../utils/storage');

const MAX_HISTORY = 10; // 显示10条数据

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
    scrollIntoId: 'row-0' // 滚动到最新数据（索引0=最新）
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
  _lastRenderTime: 0,
  _statusChangeHandler: null,

  onLoad() {
    log('[realtime] onLoad()');
    this._loadCalibFromCache();
    this._realtimeHandler = (d) => { this._onSample(d); };
    this._statusChangeHandler = (status, data) => {
      if (status === 'connected') {
        this.setData({ connected: true, isReconnecting: false, connectionLost: false });
        setTimeout(() => { this._resumeStream(); }, 100);
      } else if (status === 'connecting') {
        this.setData({ isReconnecting: true });
      } else {
        this.setData({ connected: false, isReconnecting: false, connectionLost: true });
        this._stopStream();
      }
    };
    wifiClient.onRealtimeData(this._realtimeHandler);
    wifiClient.onStatusChange(this._statusChangeHandler);
  },

  onShow() {
    log('[realtime] onShow()');
    this._tabVisible = true;
    this._algorithmState = 'idle';
    const cachedIp = wx.getStorageSync('device_ip');
    const cachedPort = wx.getStorageSync('device_port') || 8888;
    wifiClient.offRealtimeData(this._realtimeHandler);
    wifiClient.onRealtimeData(this._realtimeHandler);
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
  },

  onUnload() {
    this._stopStream();
    if (this._realtimeHandler) {
      wifiClient.offRealtimeData(this._realtimeHandler);
      this._realtimeHandler = null;
    }
    if (this._statusChangeHandler) {
      wifiClient.offStatusChange(this._statusChangeHandler);
      this._statusChangeHandler = null;
    }
  },

  _stopStream() {
    this._isResuming = false;
    this._lastRenderTime = 0;
    this._historyRows = [];
    this.setData({ queueLength: 0, historyRows: [], scrollIntoId: 'row-0' });
    wifiClient.send({ cmd: 'stop', seq: Date.now() }).catch(() => {});
  },

  async _resumeStream() {
    if (this._isResuming) return;
    this._isResuming = true;
    try {
      wifiClient.send({ cmd: 'start_stream', seq: Date.now() });
      log('[realtime] start_stream 指令已发出');
      this.setData({ quality: '--', timeStr: '--:--:--', connectionLost: false });
    } catch (e) {
      warn('[realtime] start_stream 发送异常:', e);
    }

    if (this._algorithmState === 'idle') {
      this._algorithmState = 'loading';
      wifiClient.sendQuery('query_cz', {})
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
    try {
      const { ts, r: rms, m: mdf, f: fatigue, q: quality, a: activation } = d;
      if (rms == null) return;

      let timeStr = '--';
      if (ts != null) {
        const date = new Date(ts);
        timeStr = `${String(date.getHours()).padStart(2, '0')}:${String(date.getMinutes()).padStart(2, '0')}:${String(date.getSeconds()).padStart(2, '0')}.${String(date.getMilliseconds()).padStart(3, '0')}`;
      } else {
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

      // 新数据始终放在数组头部 (索引0=最新)
      this._historyRows.unshift(histRow); 
      
      // 超过 10 条，把最老的（尾部）删掉
      if (this._historyRows.length > MAX_HISTORY) this._historyRows.pop(); 

      const now = Date.now();
      // 每次收到数据，都拷贝新数组驱动 WXML 滚动到最新行
      this.setData({ 
        historyRows: this._historyRows.slice(), // 必须用 slice() 拷贝新数组触发脏检查
        scrollIntoId: 'row-0' // 滚动到最新数据
      });

      // 限频只针对顶部状态栏文字，不拦截列表数据更新
      if (now - this._lastRenderTime < 500) {
        return;
      }
      this._lastRenderTime = now;

      this.setData({
        quality: typeof quality === 'string' && quality ? quality : '--',
        timeStr,
        connectionLost: !wifiClient.isConnected()
      });
    } catch (e) {
      error('[realtime] _onSample 内部崩溃:', e);
    }
  }
});
