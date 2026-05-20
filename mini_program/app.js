// app.js
const bleClient = require('./utils/bleClient');
const wifiClient = require('./utils/wifiClient');

/* ================= 日志配置 ================= */
const LOG_ENABLED = true;
const LOG_LOCAL_ENABLED = false;
const LOG_SERVER_URL = 'http://192.168.137.1:9876/log';
const LOG_BATCH_SIZE = 10;
const LOG_BATCH_INTERVAL = 500;

let _logBuffer = [];
let _logTimer = null;

/* ================= 本地日志（Qclaw 专用） ================= */
const fs = wx.getFileSystemManager();

// ✅ 写死到你的电脑桌面（Qclaw 直接读这个文件）
const LOG_FILE = 'C:/Users/honghuang/Desktop/wc_console.log';

function _writeLocalLog(level, args) {
  if (!LOG_LOCAL_ENABLED) return;

  const time = new Date().toLocaleString('zh-CN', { hour12: false });
  const msg = args.map(v => {
    if (v == null) return 'null';
    if (typeof v === 'object') {
      try { return JSON.stringify(v); } catch { return String(v); }
    }
    return String(v);
  }).join(' ');

  try {
    fs.appendFileSync(
      LOG_FILE,
      `[${time}] [${level}] ${msg}\n`,
      'utf8'
    );
  } catch (e) {
    // 写失败不影响业务
  }
}

/* ================= 网络日志 ================= */
function _flushLog() {
  if (_logBuffer.length === 0) return;
  const batch = _logBuffer.splice(0, LOG_BATCH_SIZE);

  wx.request({
    url: LOG_SERVER_URL,
    method: 'POST',
    header: { 'content-type': 'application/json' },
    data: { logs: batch },
    success(res) {
      console.log('[LogForward] 发送成功', batch.length, '条, status=', res.statusCode);
    },
    fail(err) {
      console.error('[LogForward] 发送失败', err.errMsg);
    }
  });
}

function _forwardLog(level, args) {
  if (!LOG_ENABLED) return;

  // ✅ 写本地文件（Qclaw 用）
  _writeLocalLog(level, args);

  const preview = args[0] ? String(args[0]) : '';
  if (preview.startsWith('[LogForward]')) return;

  const msg = args.map(v => {
    if (v == null) return 'null';
    if (typeof v === 'object') {
      try { return JSON.stringify(v); } catch { return String(v); }
    }
    return String(v);
  }).join(' ');

  _logBuffer.push({
    level,
    msg,
    time: new Date().toLocaleTimeString('zh-CN', { hour12: false })
  });

  if (_logBuffer.length >= LOG_BATCH_SIZE) {
    _flushLog();
  } else if (!_logTimer) {
    _logTimer = setTimeout(() => {
      _logTimer = null;
      _flushLog();
    }, LOG_BATCH_INTERVAL);
  }
}

/* ================= App ================= */
App({
  dataMode: 'idle',

  onLaunch() {
    this.ble = bleClient;
    this.wifi = wifiClient;
    this.tcp = wifiClient;  // 向后兼容别名

    const _origLog = console.log;
    const _origWarn = console.warn;
    const _origError = console.error;

    console.log = (...args) => {
      _origLog.apply(console, args);
      _forwardLog('log', args);
    };
    console.warn = (...args) => {
      _origWarn.apply(console, args);
      _forwardLog('warn', args);
    };
    console.error = (...args) => {
      _origError.apply(console, args);
      _forwardLog('error', args);
    };

    _origLog('[App] 小程序启动');

    // 启动时直接ping日志服务器，验证网络连通性
    wx.request({
      url: LOG_SERVER_URL,
      method: 'POST',
      header: { 'content-type': 'application/json' },
      data: { logs: [{ level: 'log', msg: '[PING] 真机日志连通性测试', time: new Date().toLocaleTimeString('zh-CN', { hour12: false }) }] },
      success(res) { _origLog('[PING] 成功 status=' + res.statusCode); },
      fail(err) { _origLog('[PING] 失败 ' + err.errMsg); }
    });

    bleClient.onIpReceived((ip, port, deviceData) => {
      _origLog('[App] 收到设备IP:', ip, port);
      if (!ip || ip === '0.0.0.0') {
        _origLog('[App] 无效IP，不缓存');
        return;
      }
      wx.setStorageSync('device_ip', ip);
      wx.setStorageSync('device_port', port || 8888);
    });
  },

  setDataMode(newMode) {
    const prevMode = this.dataMode;
    if (prevMode === newMode) return prevMode;

    console.log('[App] 数据模式切换:', prevMode, '→', newMode);
    this.dataMode = newMode;

    if (prevMode !== 'idle' && wifiClient.isConnected()) {
      wifiClient.send('{"cmd":"stop"}').catch(() => {});
    }

    return prevMode;
  }
});



