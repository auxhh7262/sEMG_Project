// ============================================================
// TCP Client - sEMG肌电监测设备通信模块 V5.2 (粘包修复+诊断版)
// ============================================================
let _socketTask = null
let _isConnected = false
let _isConnecting = false
let _reconnectTimer = null
let _reconnectCount = 0
let _maxReconnect = 3
let _reconnectInterval = 2000
let _onMessageCallback = null
let _onStatusChangeCallback = null
let _currentIP = ''
let _currentPort = 8888
let _messageListeners = []
let _connectResolve = null
let _connectReject = null
let _pageStatusListeners = []

function _updateStatus(status, message) {
  _isConnected = status === 'connected'
  _isConnecting = status === 'connecting'
  if (_onStatusChangeCallback && typeof _onStatusChangeCallback === 'function') {
    try { _onStatusChangeCallback(status, message) } catch (e) {}
  }
  _pageStatusListeners.forEach(fn => {
    try { fn(status, message) } catch (e) {}
  })
}

function _cleanup() {
  if (_socketTask) {
    try { _socketTask.close() } catch (e) {}
    _socketTask = null
  }
  if (_reconnectTimer) {
    clearTimeout(_reconnectTimer); _reconnectTimer = null
  }
  _isConnecting = false; _connectResolve = null; _connectReject = null
}

let _notifyListeners = function(rawData) {
  _messageListeners.forEach(fn => {
    try { fn(rawData) } catch (e) {}
  });
  if (_onMessageCallback) {
    try { _onMessageCallback(rawData) } catch (e) {}
  }
}

function _bindSocketEvents() {
  _socketTask.onOpen(() => {
    console.log('[TCP] onOpen — WebSocket 连接成功')
    _isConnecting = false; _isConnected = true
    _updateStatus('connected', '连接成功')
    if (_connectResolve) {
      _connectResolve(true); _connectResolve = null; _connectReject = null
    }
  })

  _socketTask.onMessage((res) => {
    try {
      let rawData = '';
      if (typeof res.data === 'string') {
        rawData = res.data;
      } else if (res.data instanceof ArrayBuffer) {
        try {
          rawData = String.fromCharCode.apply(null, new Uint8Array(res.data));
        } catch (e) {
          return;
        }
      } else {
        return;
      }
      if (!rawData) return;
      
      // 🌟 日志 1：查看底层原始收到的数据
      console.log('[TCP] RAW RX:', rawData);

      // 🌟 终极修复：使用大括号层级计数法安全切割粘包
      if (rawData.startsWith('{')) {
        let depth = 0;
        let startIdx = -1;
        for (let i = 0; i < rawData.length; i++) {
          if (rawData[i] === '{') {
            if (depth === 0) startIdx = i;
            depth++;
          } else if (rawData[i] === '}') {
            depth--;
            if (depth === 0 && startIdx !== -1) {
              // 成功匹配一个完整的 JSON
              const jsonStr = rawData.substring(startIdx, i + 1);
              _notifyListeners(jsonStr);
              _routeTypedMessage(jsonStr);
              startIdx = -1; // 重置，准备匹配下一个
            }
          }
        }
      } else {
        _realtimeDataListeners.forEach(fn => {
          try { fn(rawData) } catch (e) {}
        })
      }
    } catch (e) {
      console.error('[TCP] onMessage 解析异常:', e)
    }
  })

  _socketTask.onClose(() => {
    console.log('[TCP] onClose — WebSocket 关闭')
    if (_isConnecting && _connectReject) {
      _connectReject(new Error('Connection closed'));
      _connectResolve = null; _connectReject = null
    }
    _handleDisconnect()
  })

  _socketTask.onError((err) => {
    console.error('[TCP] onError — WebSocket 错误:', err)
    if (_connectReject) {
      _connectReject(err); _connectResolve = null; _connectReject = null
    }
    _handleDisconnect()
  })
}

function _handleDisconnect() {
  if (!_isConnected && !_isConnecting) return
  console.log('[TCP] 连接断开，IP:', _currentIP, '已重连:', _reconnectCount, '次')
  _updateStatus('disconnected', '连接断开');
  _isConnected = false; _isConnecting = false
  if (_socketTask) {
    try { _socketTask.close() } catch (e) {}
    _socketTask = null
  }
  _tryReconnect()
}

function _tryReconnect() {
  if (_reconnectCount >= _maxReconnect) {
    console.log('[TCP] 重连已达上限');
    _updateStatus('reconnect_failed', '重连失败');
    return
  }
  if (!_currentIP || _isConnecting) return
  _reconnectCount++
  console.log('[TCP] 自动重连 ' + _reconnectCount + '/' + _maxReconnect)
  _reconnectTimer = setTimeout(() => {
    connect(_currentIP, _currentPort)
  }, _reconnectInterval)
}

function connect(ip, port) {
  const isNewTarget = (ip !== _currentIP || (port || 8888) !== _currentPort)
  if (_isConnecting || _isConnected) return Promise.resolve(_isConnected)
  _cleanup();
  _currentIP = ip; _currentPort = port || 8888; _isConnecting = true
  if (isNewTarget) { _reconnectCount = 0 }
  let safeIp = _currentIP.replace(/^ws:\/\/\/?/i, '').replace(/:\d+$/i, '')
  let url = `ws://${safeIp}:${_currentPort}`
  _updateStatus('connecting', `连接到 ${safeIp}:${_currentIP}`)
  _socketTask = wx.connectSocket({ url: url });
  _bindSocketEvents();
  return new Promise((resolve, reject) => { _connectResolve = resolve; _connectReject = reject })
}

function disconnect() {
  console.log('[TCP] disconnect() — 手动断开');
  _cleanup(); _updateStatus('disconnected', '手动断开'); _currentIP = ''; _currentPort = 8888
}

function send(data) {
  if (!_socketTask || !_isConnected) return Promise.resolve(false);
  const dataStr = typeof data === 'object' ? JSON.stringify(data) : String(data);
  return new Promise((resolve) => {
    try {
      _socketTask.send({
        data: dataStr,
        success: () => resolve(true),
        fail: () => resolve(false)
      })
    } catch (e) { resolve(false) }
  })
}

function isConnected() { return _isConnected };
function isConnecting() { return _isConnecting }

function setCallbacks(onMessage, onStatusChange) {
  _onMessageCallback = onMessage; _onStatusChangeCallback = onStatusChange
}

let _pushListeners = [];
function onPush(fn) { if (!_pushListeners.includes(fn)) _pushListeners.push(fn) };
function offPush(fn) { if (!fn) _pushListeners = []; else { const i = _pushListeners.indexOf(fn); if (i !== -1) _pushListeners.splice(i, 1) } }

function onMessage(fn) { if (!_messageListeners.includes(fn)) _messageListeners.push(fn) };
function offMessage(fn) { if (!fn) _messageListeners = []; else { const idx = _messageListeners.indexOf(fn); if (idx !== -1) _messageListeners.splice(idx, 1) } }

function onStatusChange(fn) { if (!_pageStatusListeners.includes(fn)) _pageStatusListeners.push(fn) };
function offStatusChange(fn) { if (!fn) _pageStatusListeners = []; else { const i = _pageStatusListeners.indexOf(fn); if (i !== -1) _pageStatusListeners.splice(i, 1) } }

let _realtimeDataListeners = [];
let _calibDataListeners = [];
let _cmdSeq = 0;
let _pendingCmds = {};
const _CMD_TIMEOUT_MS = 5000

function _routeTypedMessage(rawData) {
  let data;
  try { 
    data = JSON.parse(rawData) 
  } catch (e) { 
    console.warn('[TCP] JSON PARSE FAIL:', rawData);
    return false 
  }
  
  console.log('[TCP] ROUTER TYPE:', data.type, 'CMD:', data.cmd);

  // 🌟 终极修复：去掉 _realtimeDataListeners.length > 0 的限制！
  // 只要识别出是实时数据，就算监听器被微信异常清空，也必须走完逻辑！
  if (data.type === 'data' || (data.ts !== undefined && data.r !== undefined)) {
    if (_realtimeDataListeners.length > 0) {
      _realtimeDataListeners.forEach(fn => {
        try { fn(data) } catch (e) {}
      });
    } else {
      // 🌟 兜底防线：如果监听器丢了，直接在控制台报警，证明数据已送达！
      console.warn('[TCP] ⚠️ 实时数据到达，但页面监听器丢失！数据:', data.r);
    }
    return true
  }
  
  if (data.type === 'calib_data' && _calibDataListeners.length > 0) {
    _calibDataListeners.forEach(fn => {
      try { fn(data) } catch (e) {}
    });
    return true
  }
  
  if (data.cmd && data.seq !== undefined) {
    const seq = String(data.seq);
    if (_pendingCmds[seq]) {
      clearTimeout(_pendingCmds[seq].timer);
      const { resolve } = _pendingCmds[seq];
      delete _pendingCmds[seq];
      resolve(data);
      return true
    }
  }
  
  if (data.cmd && _pushListeners.length > 0) {
    _pushListeners.forEach(fn => {
      try { fn(data) } catch (e) {}
    });
    return true
  }
  
  return false
}


function sendCmd(cmd, payload = {}) {
  return new Promise((resolve, reject) => {
    if (!_socketTask || !_isConnected) {
      reject(new Error('未连接')); return
    }
    const seq = String(++_cmdSeq);
    const pkg = { cmd, seq, ...payload };
    const timer = setTimeout(() => {
      if (_pendingCmds[seq]) {
        delete _pendingCmds[seq]; reject(new Error('CMD_TIMEOUT'))
      }
    }, _CMD_TIMEOUT_MS);
    _pendingCmds[seq] = { resolve, reject, timer };
    _socketTask.send({
      data: JSON.stringify(pkg),
      fail: (err) => {
        clearTimeout(timer); delete _pendingCmds[seq]; reject(err || new Error('SEND_ERROR'))
      }
    })
  })
}

function onRealtimeData(fn) { if (!_realtimeDataListeners.includes(fn)) _realtimeDataListeners.push(fn) };
function offRealtimeData(fn) { if (!fn) _realtimeDataListeners = []; else { const i = _realtimeDataListeners.indexOf(fn); if (i !== -1) _realtimeDataListeners.splice(i, 1) } }

function onCalibData(fn) { if (!_calibDataListeners.includes(fn)) _calibDataListeners.push(fn) };
function offCalibData(fn) { if (!fn) _calibDataListeners = []; else { const i = _calibDataListeners.indexOf(fn); if (i !== -1) _calibDataListeners.splice(i, 1) } }

function _cleanupExtended() {
  Object.keys(_pendingCmds).forEach(seq => {
    clearTimeout(_pendingCmds[seq].timer); _pendingCmds[seq].reject(new Error('连接断开'))
  });
  _pendingCmds = {};
  _calibDataListeners = [];
}

const _origCleanup = _cleanup;
_cleanup = function() { _cleanupExtended(); _origCleanup() }

function close() {
  _cleanup(); _updateStatus('disconnected', 'close()'); _currentIP = ''; _currentPort = 8888
}

module.exports = {
  connect, disconnect, send, sendCmd, isConnected, isConnecting,
  setCallbacks, onMessage, offMessage, onRealtimeData, offRealtimeData,
  onCalibData, offCalibData, onPush, offPush, onStatusChange, offStatusChange, close, _currentIP
}
