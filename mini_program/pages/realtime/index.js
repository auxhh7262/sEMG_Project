// pages/realtime/index.js
const app = getApp();
const { log, warn, error } = require('../../utils/logger');
const wifiClient = require('../../utils/wifiClient');
const storage = require('../../utils/storage');
const { getCurrentUser } = storage;

const MAX_HISTORY = 5;  // 保留最近5行数据

Page({
  data: {
    connected: false,
    quality: '--',
    timeStr: '--:--:--',
    queueLength: 0,
    isReconnecting: false,
    connectionLost: false,
    historyRows: [],
    algorithm: '--',    // [v3.9.12] 个性化曲线 / 默认MDF
  },

  _historyRows: [],
  _restRms: null, _maxRms: null, _restMdf: null, _maxMdf: null,
  _realtimeHandler: null,  // [v3.9.12] onRealtimeData 处理器
  _appDisconnectHandler: null,
  _tabVisible: false,

  // ──── 生命周期 ────

  onLoad() {
    log('[realtime] onLoad() — 实时监控页加载');
    this._loadCalibFromCache();
  },

  onShow() {
    log('[realtime] onShow() — Tab切换进入');
    this._tabVisible = true;
    // 每次切回页面都重查算法标识（用户切换校准槽后需要刷新）
    this._algorithmChecked = false;

    const prevMode = app.setDataMode('realtime');
    log('[realtime] 数据模式切换:', prevMode, '→ realtime');

    const cachedIp = wx.getStorageSync('device_ip');
    const cachedPort = wx.getStorageSync('device_port') || 8888;

    if (!cachedIp) {
      warn('[realtime] 无缓存IP，显示未连接');
      this.setData({ connected: false, connectionLost: true, isReconnecting: false });
      return;
    }

    if (wifiClient.isConnected()) {
      log('[realtime] WiFi已连接，恢复数据流');
      this.setData({ connected: true, connectionLost: false, isReconnecting: false });
      this._resumeStream();
    } else if (wifiClient._isConnecting) {
      log('[realtime] WiFi正在连接中...');
      this.setData({ isReconnecting: true, connectionLost: false });
      setTimeout(() => {
        if (wifiClient.isConnected()) {
          this.setData({ isReconnecting: false });
          this._resumeStream();
        }
      }, 2000);
    } else {
      log('[realtime] WiFi未连接，主动连接:', cachedIp, cachedPort);
      this.setData({ isReconnecting: true, connectionLost: false });
      wifiClient.connect(cachedIp, cachedPort).then(() => {
        log('[realtime] WiFi连接成功，恢复数据流');
        this.setData({ connected: true, isReconnecting: false, connectionLost: false });
        this._resumeStream();
      }).catch(() => {
        warn('[realtime] WiFi连接失败，重试');
        this._silentReconnect(cachedIp);
      });
    }
  },

  onHide() {
    log('[realtime] onHide() — Tab切走');
    this._tabVisible = false;
    this._stopStream();
  },

  onUnload() {
    log('[realtime] onUnload() — 页面销毁');
    this._stopStream();
    const appInst = getApp();
    if (appInst.onRealtimeDisconnect === this._appDisconnectHandler) {
      appInst.onRealtimeDisconnect = null;
    }
  },

  // ──── 1. 静默重连 ────

  _silentReconnect(ip) {
    let retry = 0;
    const tryReconnect = () => {
      if (!this._tabVisible || retry >= 5) {
        if (retry >= 5) this.setData({ connected: false, connectionLost: true, isReconnecting: false });
        return;
      }
      retry++;
      wifiClient.connect(ip, 8888).then(() => {
        this.setData({ connected: true, isReconnecting: false, connectionLost: false });
        this._resumeStream();
      }).catch(() => {
        setTimeout(tryReconnect, 2500);
      });
    };
    setTimeout(tryReconnect, 1000);
  },

  // [v3.9.12] 停止数据流
  _stopStream() {
    wifiClient.enableHeartbeat(true, 120000);  // 恢复idle模式120秒超时（避免误判）
    if (this._realtimeHandler) {
      wifiClient.offRealtimeData(this._realtimeHandler);
      this._realtimeHandler = null;
    }
    wifiClient.sendCmd('stop').catch(() => {});
  },

  // ──── 2. 恢复数据流 ────

  _resumeStream() {
    log('[realtime] _resumeStream() — 恢复数据流');
    wifiClient.enableHeartbeat(true, 120000);  // 数据流模式启用心跳，120秒超时（避免误判）

    // 清理旧监听器
    if (this._realtimeHandler) {
      wifiClient.offRealtimeData(this._realtimeHandler);
    }

    // [v3.9.12] 使用 wifiClient.onRealtimeData 替代 onMessage
    this._realtimeHandler = (d) => {
      this._onSample(d);
    };
    wifiClient.onRealtimeData(this._realtimeHandler);

    // [v3.9.12] 每次进入页面都重查算法标识（切换用户后需要刷新）
    if (!this._algorithmChecked) {
      this._algorithmChecked = true;
      const user = getCurrentUser();
      const loadUser = user ? wifiClient.sendCmd('load_user', { slot: user.slot }).catch(() => {}) : Promise.resolve();
      loadUser.then(() => {
        wifiClient.sendCmd('query_cz', {}).then(status => {
          // realtime页绝不触发校准；无校准时algorithm标记为无校准，WXML显示提示
          if (status.has_curve) {
            this.setData({ algorithm: '个性化曲线', hasPersonalCurve: true });
            log('[realtime] 个性化曲线已激活 has_curve:', status.has_curve);
          } else if (status.ref_mdf && status.ref_mdf > 0) {
            this.setData({ algorithm: '默认MDF下降率', hasPersonalCurve: false });
            log('[realtime] 无个性化曲线，使用默认MDF下降率 ref_mdf:', status.ref_mdf);
          } else {
            // A区无校准数据，仅显示原始RMS/MDF，疲劳值无效
            this.setData({ algorithm: '无校准', hasPersonalCurve: false });
            log('[realtime] A区无校准数据，请先在「肌电校准」页面完成校准');
          }
        }).catch(() => {});
      });
    }

    // [修复] 发送 start_stream 命令（不校准，纯数据流）
    wifiClient.sendCmd('start_stream', {}).then(() => {
      log('[realtime] ✅ 已发送 start_stream 命令');
    }).catch((e) => {
      warn('[realtime] 发送 start_stream 命令失败:', e);
    });
  },



  // ──── 3. 校准参数（本地缓存，备选） ────

  _loadCalibFromCache() {
    try {
      const c = wx.getStorageSync('calib_data');
      if (c && c.rest_rms && c.max_rms && c.rest_mdf && c.max_mdf) {
        this._restRms = c.rest_rms; this._maxRms = c.max_rms;
        this._restMdf = c.rest_mdf; this._maxMdf = c.max_mdf;
      }
    } catch (e) { error('[realtime] 加载校准数据异常:', e); }
  },

  // ──── 4. 数据处理 ────

  _onSample(d) {
    const { ts, r: rms, m: mdf, f: fatigue, q: quality, a: activation } = d;
    if (rms == null) return;

    let timeStr = '--';
    if (ts != null) {
      try {
        const date = new Date(ts);
        const h = String(date.getHours()).padStart(2, '0');
        const m = String(date.getMinutes()).padStart(2, '0');
        const s = String(date.getSeconds()).padStart(2, '0');
        const ms = String(date.getMilliseconds()).padStart(3, '0');
        timeStr = `${h}:${m}:${s}.${ms}`;
      } catch (_) {}
    }

    const actPct = activation != null ? Math.max(0, Math.min(100, activation * 100)) : null;
    const fatPct = fatigue != null ? Math.max(0, Math.min(100, fatigue * 100)) : null;

    const histRow = {
      time: timeStr,
      rms: rms.toFixed(2),
      act: actPct !== null ? actPct.toFixed(1) + '%' : '--',
      mdf: mdf != null ? mdf.toFixed(1) : '--',
      fat: fatPct !== null ? fatPct.toFixed(1) + '%' : '--',
      q: quality != null ? quality : '--',
    };
    this._historyRows.push(histRow);
    if (this._historyRows.length > MAX_HISTORY) this._historyRows.shift();

    this.setData({
      quality: quality != null ? quality : '--',
      timeStr,
      connectionLost: false,
      historyRows: this._historyRows,
    });
  },

  // ──── 5. 断网监听 ────

  registerDisconnectHandler() {
    const appInst = getApp();
    this._appDisconnectHandler = () => {
      if (!this._tabVisible) return;
      if (this.data.isReconnecting || !this.data.connected) return;
      const cachedIp = wx.getStorageSync('device_ip');
      if (!cachedIp) { this.setData({ connected: false, connectionLost: true }); return; }
      this.setData({ connected: false, isReconnecting: true });
      this._silentReconnect(cachedIp);
    };
    appInst.onRealtimeDisconnect = this._appDisconnectHandler;
  },
});
