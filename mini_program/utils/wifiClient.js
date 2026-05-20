// ============================================================
// TCP Client - sEMG肌电监测设备通信模块 V1.0
// 支持: WebSocket连接、自动重连、心跳保活、多监听器模式
// ============================================================

let _socket = null
let _isConnected = false
let _isConnecting = false
let _reconnectTimer = null
let _reconnectCount = 0
let _maxReconnect = 5
let _reconnectInterval = 2000
let _onMessageCallback = null
let _onStatusChangeCallback = null
let _onErrorCallback = null
let _onIpSentCallback = null
let _currentIP = ''
let _currentPort = 8888
let _heartbeatTimer = null
let _lastPongTime = 0
let _heartbeatInterval = 5000
let _timeoutTimer = null
let _connectionTimeout = 10000
let _ipReceived = false
let _waitingForWifi = false
let _wifiRecoveryTimer = null
let _heartbeatEnabled = false
let _messageListeners = []
let _connectResolve = null
let _connectReject = null

// 状态变化回调
function _updateStatus(status, message) {
  _isConnected = status === 'connected'
  _isConnecting = status === 'connecting'
  if (_onStatusChangeCallback && typeof _onStatusChangeCallback === 'function') {
    wx.nextTick(() => { _onStatusChangeCallback(status, message) })
  }
}

// 清理连接资源
function _cleanup() {
  if (_socket) {
    try { _socket.close() } catch (e) {}
    _socket = null
  }
  if (_reconnectTimer) { clearTimeout(_reconnectTimer); _reconnectTimer = null }
  if (_heartbeatTimer) { clearInterval(_heartbeatTimer); _heartbeatTimer = null }
  if (_timeoutTimer) { clearTimeout(_timeoutTimer); _timeoutTimer = null }
  if (_wifiRecoveryTimer) { clearTimeout(_wifiRecoveryTimer); _wifiRecoveryTimer = null }
  _lastPongTime = 0; _reconnectCount = 0; _isConnecting = false; _ipReceived = false; _waitingForWifi = false
  // 清理 Promise 引用
  _connectResolve = null; _connectReject = null
}

// 心跳保活
function _setupHeartbeat() {
  console.log('[TCP] _setupHeartbeat — 启动心跳，间隔', _heartbeatInterval, 'ms')
  if (_heartbeatTimer) { clearInterval(_heartbeatTimer) }
  _heartbeatTimer = setInterval(() => {
    if (_socket && _isConnected && _heartbeatEnabled) {
      const now = Date.now()
      if (_lastPongTime > 0 && now - _lastPongTime > _heartbeatInterval * 2) {
        console.warn('[TCP] 心跳超时，断开连接')
        _handleDisconnect(); return
      }
      try {
        _socket.send({ data: JSON.stringify({ cmd: 'ping' }), fail: () => { _handleDisconnect() } })
      } catch (e) { _handleDisconnect() }
    }
  }, _heartbeatInterval)
}

// 分发消息到所有监听器（v1.1：先路由类型化监听，再走通用监听）
let _notifyListeners = function(rawData) {
  _messageListeners.forEach(fn => {
    try { fn(rawData) } catch (e) { console.error('[TCP] listener error:', e) }
  })
  if (_onMessageCallback) {
    try { _onMessageCallback(rawData) } catch (e) {}
  }
}

// 绑定WebSocket事件
function _bindSocketEvents() {
  _socket.onOpen(() => {
    console.log('[TCP] onOpen — WebSocket 连接成功')
    clearTimeout(_timeoutTimer); _timeoutTimer = null; _isConnecting = false; _isConnected = true; _reconnectCount = 0
    _updateStatus('connected', '连接成功'); _lastPongTime = Date.now()
    // 成功时 resolve Promise
    if (_connectResolve) { _connectResolve(true); _connectResolve = null; _connectReject = null }
    console.log('[TCP] onOpen — 发送 handshake')
    setTimeout(() => {
      if (_socket && _isConnected) {
        _socket.send({ data: JSON.stringify({ cmd: 'handshake', type: 'client', version: 'V1.0' }) })
      }
    }, 500)
  })

  _socket.onMessage((res) => {
    try {
      const rawData = typeof res.data === 'string' ? res.data : ''
      if (!rawData) return
      if (rawData.startsWith('{')) {
        // console.log('[TCP] onMessage — JSON 数据:', rawData.substring(0, 100)) // 按需开启
        _notifyListeners(rawData)
      } else if (rawData.includes('pong') || rawData.includes('ack')) {
        _lastPongTime = Date.now()
        // console.log('[TCP] onMessage — 收到 pong/ack')
      }
    } catch (e) { console.error('[TCP] onMessage 解析异常:', e) }
  })

  _socket.onClose(() => {
    console.log('[TCP] onClose — WebSocket 关闭')
    // 失败时 reject Promise（如果还在连接中）
    if (_isConnecting && _connectReject) { _connectReject(new Error('Connection closed')); _connectResolve = null; _connectReject = null }
    _handleDisconnect()
  })
  _socket.onError((err) => {
    console.error('[TCP] onError — WebSocket 错误:', err)
    clearTimeout(_timeoutTimer); _timeoutTimer = null
    // 失败时 reject Promise
    if (_connectReject) { _connectReject(err); _connectResolve = null; _connectReject = null }
    _handleDisconnect()
  })
}

// 处理断开连接
function _handleDisconnect() {
  console.log('[TCP] _handleDisconnect — 处理断开，当前 connected=' + _isConnected + ', connecting=' + _isConnecting)
  if (_isConnected || _isConnecting) _updateStatus('disconnected', '连接断开')
  _isConnected = false; _isConnecting = false
  if (_heartbeatTimer) { clearInterval(_heartbeatTimer); _heartbeatTimer = null }
  if (_timeoutTimer) { clearTimeout(_timeoutTimer); _timeoutTimer = null }
  if (_wifiRecoveryTimer) { clearTimeout(_wifiRecoveryTimer); _wifiRecoveryTimer = null }
  if (_socket) { try { _socket.close() } catch (e) {} _socket = null }
  _ipReceived = false
  if (_waitingForWifi) {
    _wifiRecoveryTimer = setTimeout(() => { _waitingForWifi = false; _tryReconnect() }, 5000)
  } else { _tryReconnect() }
}

// 自动重连
function _tryReconnect() {
  console.log('[TCP] _tryReconnect — 重连次数:', _reconnectCount, '/', _maxReconnect)
  if (_reconnectCount >= _maxReconnect || !_currentIP || _isConnecting) {
    console.log('[TCP] _tryReconnect — 跳过重连（达到上限/无IP/正在连接）')
    return
  }
  _reconnectCount++
  const delay = _reconnectInterval * Math.min(_reconnectCount, 3)
  console.log('[TCP] _tryReconnect — 将在', delay, 'ms 后重连')
  _reconnectTimer = setTimeout(() => { connect(_currentIP, _currentPort) }, delay)
}

// 连接WebSocket
function connect(ip, port) {
  console.log('[TCP] connect() — 尝试连接', ip, port || 8888)
  if (_isConnecting || _isConnected) {
    console.log('[TCP] connect() — 跳过，当前状态: connecting=' + _isConnecting + ', connected=' + _isConnected)
    return Promise.resolve(_isConnected)
  }
  _cleanup()
  _currentIP = ip
  _currentPort = port || 8888
  _isConnecting = true; _reconnectCount = 0; _heartbeatEnabled = false

  let safeIp = _currentIP.replace(/^ws:\/\/\/?/i, '').replace(/:\d+$/i, '')
  let url = `ws://${safeIp}:${_currentPort}`
  console.log('[TCP] connect() — WebSocket URL:', url)

  _updateStatus('connecting', `连接到 ${safeIp}:${_currentPort}`)

  _timeoutTimer = setTimeout(() => { if (_isConnecting) _handleDisconnect() }, _connectionTimeout)

  _socket = wx.connectSocket({ url: url })
  console.log('[TCP] connect() — wx.connectSocket 已调用')
  _bindSocketEvents()

  // 返回 Promise，等待连接结果
  return new Promise((resolve, reject) => {
    _connectResolve = resolve
    _connectReject = reject
  })
}

// 断开连接
function disconnect() {
  console.log('[TCP] disconnect() — 手动断开连接')
  _cleanup(); _updateStatus('disconnected', '手动断开'); _currentIP = ''; _currentPort = 8888
}

// 发送数据
function send(data) {
  if (!_socket || !_isConnected) {
    console.warn('[TCP] send() — 发送失败，未连接')
    return Promise.resolve(false)
  }
  const dataStr = typeof data === 'object' ? JSON.stringify(data) : String(data)
  console.log('[TCP] send() — 发送:', dataStr.substring(0, 80))
  return new Promise((resolve) => {
    try {
      _socket.send({ data: dataStr, success: () => resolve(true), fail: () => resolve(false) })
    } catch (e) { console.error('[TCP] send() 异常:', e); resolve(false) }
  })
}

// 连接状态查询
function isConnected() { return _isConnected }

// 设置回调函数
function setCallbacks(onMessage, onStatusChange, onError, onIpSent) {
  _onMessageCallback = onMessage
  _onStatusChangeCallback = onStatusChange
  _onErrorCallback = onError
  _onIpSentCallback = onIpSent
}

// 心跳开关
function enableHeartbeat(enabled) {
  console.log('[TCP] enableHeartbeat —', enabled)
  _heartbeatEnabled = enabled
  if (enabled && _isConnected) _setupHeartbeat()
  else if (_heartbeatTimer) { clearInterval(_heartbeatTimer); _heartbeatTimer = null }
}

// ============================================================
// 事件监听接口（多监听器模式，支持多个页面同时订阅消息）
// ============================================================

function onMessage(fn) {
  if (typeof fn === 'function' && !_messageListeners.includes(fn)) {
    _messageListeners.push(fn)
  }
}

function offMessage(fn) {
  if (!fn) { _messageListeners = [] }
  else {
    const idx = _messageListeners.indexOf(fn)
    if (idx !== -1) _messageListeners.splice(idx, 1)
  }
}

// ============================================================
// v1.1 扩展：命令-响应 + 类型化监听器
// ============================================================

let _realtimeDataListeners = []
let _calibDataListeners = []
let _cmdSeq = 0
let _pendingCmds = {}  // seq → { resolve, reject, timer }
const _CMD_TIMEOUT_MS = 5000

// 按 data.type 分发到对应监听器
function _routeTypedMessage(rawData) {
  let data
  try { data = JSON.parse(rawData) } catch (e) { return false }

  // 实时数据: { type: "data", rms, mdf, fatigue }
  if (data.type === 'data' && _realtimeDataListeners.length > 0) {
    _realtimeDataListeners.forEach(fn => { try { fn(data) } catch (e) {} })
    return true
  }

  // 校准数据: { type: "calib_data", phase, rms, mdf }
  if (data.type === 'calib_data' && _calibDataListeners.length > 0) {
    _calibDataListeners.forEach(fn => { try { fn(data) } catch (e) {} })
    return true
  }

  // 命令响应 ack: { cmd:"...", status, ... } — 匹配 pending seq
  if (data.cmd && data.seq !== undefined) {
    const seq = String(data.seq)
    if (_pendingCmds[seq]) {
      clearTimeout(_pendingCmds[seq].timer)
      const { resolve, reject } = _pendingCmds[seq]
      delete _pendingCmds[seq]
      if (data.status === 'ok' || data.status === 0 || data.ok === true) resolve(data)
      else reject(new Error((data.error || 'CMD_ERROR') + ': ' + (data.msg || '')))
      return true
    }
  }

  return false
}

// 拦截原始消息，先路由类型化监听，再走通用监听
const _origNotifyListeners = _notifyListeners
_notifyListeners = function(rawData) {
  if (!_routeTypedMessage(rawData)) {
    _origNotifyListeners(rawData)
  }
}

// 命令-响应发送（等待固件 ack）
function sendCmd(cmd, payload = {}) {
  return new Promise((resolve, reject) => {
    if (!_socket || !_isConnected) { reject(new Error('未连接')); return }
    const seq = String(++_cmdSeq)
    const pkg = { cmd, seq, ...payload }
    const dataStr = JSON.stringify(pkg)
    // 超时
    const timer = setTimeout(() => {
      if (_pendingCmds[seq]) {
        delete _pendingCmds[seq]
        reject(new Error('CMD_TIMEOUT'))
      }
    }, _CMD_TIMEOUT_MS)
    _pendingCmds[seq] = { resolve, reject, timer }
    _socket.send({ data: dataStr, fail: (err) => {
      clearTimeout(timer); delete _pendingCmds[seq]
      reject(err || new Error('SEND_ERROR'))
    }})
  })
}

// 实时数据监听（type:"data"）
function onRealtimeData(fn) {
  if (typeof fn === 'function' && !_realtimeDataListeners.includes(fn)) {
    _realtimeDataListeners.push(fn)
  }
}
function offRealtimeData(fn) {
  if (!fn) { _realtimeDataListeners = [] }
  else { const i = _realtimeDataListeners.indexOf(fn); if (i !== -1) _realtimeDataListeners.splice(i, 1) }
}

// 校准过程数据监听（type:"calib_data"）
function onCalibData(fn) {
  if (typeof fn === 'function' && !_calibDataListeners.includes(fn)) {
    _calibDataListeners.push(fn)
  }
}
function offCalibData(fn) {
  if (!fn) { _calibDataListeners = [] }
  else { const i = _calibDataListeners.indexOf(fn); if (i !== -1) _calibDataListeners.splice(i, 1) }
}

// 清理扩展状态
function _cleanupExtended() {
  Object.keys(_pendingCmds).forEach(seq => {
    clearTimeout(_pendingCmds[seq].timer)
    _pendingCmds[seq].reject(new Error('连接断开'))
  })
  _pendingCmds = {}
  _realtimeDataListeners = []
  _calibDataListeners = []
}

// 改 close/disconnect/cleanup 也清理扩展状态
const _origCleanup = _cleanup
_cleanup = function() { _cleanupExtended(); _origCleanup() }

function close() { _cleanup(); _updateStatus('disconnected', 'close()'); _currentIP = ''; _currentPort = 8888 }

module.exports = { connect, disconnect, send, sendCmd, isConnected, setCallbacks, enableHeartbeat, onMessage, offMessage, onRealtimeData, offRealtimeData, onCalibData, offCalibData, close }
