// pages/index/index.js - 设置页：WiFi配网内联（合并wifi-config）
// V2.4 - 修复：有缓存IP时不再触发BLE扫描，直接用TCP

const ble = require('../../utils/bleClient.js')
const app = getApp()
const { log, warn, error } = require('../../utils/logger')

// 配网状态
const STEP = {
  IDLE: 0,
  SCANNING: 1,
  CONNECTED: 2,
  SENDING: 3,
  WAITING_IP: 4,
  SUCCESS: 5,
  FAILED: 6
}

const STATUS_TEXT = {
  [STEP.IDLE]: '未连接',
  [STEP.SCANNING]: '扫描中',
  [STEP.CONNECTED]: '已连接',
  [STEP.SENDING]: '发送中',
  [STEP.WAITING_IP]: '等待响应',
  [STEP.SUCCESS]: '配网成功',
  [STEP.FAILED]: '配网失败'
}

const STATUS_CLASS = {
  [STEP.IDLE]: 'idle',
  [STEP.SCANNING]: 'scanning',
  [STEP.CONNECTED]: 'connected',
  [STEP.SENDING]: 'sending',
  [STEP.WAITING_IP]: 'waiting',
  [STEP.SUCCESS]: 'success',
  [STEP.FAILED]: 'error'
}

Page({
  data: {
    // 状态
    currentStep: STEP.IDLE,
    statusText: STATUS_TEXT[STEP.IDLE],
    statusClass: STATUS_CLASS[STEP.IDLE],

    // 设备
    deviceName: '',
    deviceRssi: null,
    bleConnected: false,
    deviceIp: '',

    // 表单
    ssid: '',
    password: '',
    showPassword: false,

    // 操作
    isScanning: false,
    canSend: false,

    // 设备WiFi名称
    deviceSsid: '',
    // 手机网络信息
    phoneWifiSsid: '',
    // 结果
    resultType: '',
    resultMessage: ''
  },

  // ──── 页面生命周期 ────

  onLoad() {
    const cachedIp = wx.getStorageSync('device_ip')

    if (cachedIp) {
      this.setData({ deviceIp: cachedIp })
      this._setStep(STEP.SUCCESS)
    }
    // 加载缓存的设备WiFi名称
    const cachedSsid = wx.getStorageSync('device_ssid')
    if (cachedSsid) {
      this.setData({ deviceSsid: cachedSsid })
    }
    // 获取手机网络信息
    this._getPhoneNetworkInfo()

    ble.onIpReceived((ip, port, deviceData) => {
      log('[设置] 收到设备IP:', ip, 'SSID:', deviceData?.ssid)
      if (!ip || ip === '0.0.0.0') {
        warn('[设置] 收到无效IP:', ip, '，不缓存，等待设备获取真实IP')
        return
      }
      // 保存IP和WiFi名称
      const ssid = deviceData?.ssid || ''
      this.setData({ deviceIp: ip, deviceSsid: ssid })
      wx.setStorageSync('device_ip', ip)
      wx.setStorageSync('device_port', port || 8888)
      if (ssid) {
        wx.setStorageSync('device_ssid', ssid)
      }
      // 【修复】所有状态都更新IP：设备断电重连后IP可能变化
      // SUCCESS时仅更新缓存，不弹"配网成功"提示（避免重复弹窗）
      if (this.data.currentStep === STEP.SUCCESS) {
        log('[设置] 设备IP已更新（BLE重连）:', ip)
      } else {
        this._setStep(STEP.SUCCESS)
        this._showResult('success', '配网成功！设备 IP: ' + ip)
      }
    })
  },

  onShow() {
    // 切换到 idle 模式（网络配置页不主动收数据）
    const prevMode = app.setDataMode('idle');
    log('[设置] 数据模式切换:', prevMode, '→ idle');
    
    const cachedIp = wx.getStorageSync('device_ip')
    const isValidIp = cachedIp && cachedIp !== '0.0.0.0' && cachedIp !== ''

    // 清除无效缓存
    if (cachedIp && !isValidIp) {
      warn('[设置] 清除无效缓存IP:', cachedIp)
      wx.removeStorageSync('device_ip')
    }

    if (isValidIp) {
      // 有有效缓存IP → 显示成功状态，不再触发BLE扫描
      // BLE只用于首次配网；设备已有WiFi时直接用TCP
      log('[设置] 有缓存IP:', cachedIp, '，设备已连WiFi，无需BLE扫描')
      const cachedSsid = wx.getStorageSync('device_ssid') || ''
      this.setData({
        deviceIp: cachedIp,
        deviceSsid: cachedSsid,
        currentStep: STEP.SUCCESS,
        statusText: STATUS_TEXT[STEP.SUCCESS],
        statusClass: STATUS_CLASS[STEP.SUCCESS],
        bleConnected: false  // BLE连接状态不影响显示
      })
      // 刷新手机网络信息
      this._getPhoneNetworkInfo()

      // 同时尝试WiFi连接（静默，不影响UI）
      const wifiClient = require('../../utils/wifiClient')
      if (wifiClient && !wifiClient.isConnected()) {
        const cachedPort = wx.getStorageSync('device_port') || 8888
        log('[设置] 尝试静默WiFi连接:', cachedIp, cachedPort)
        wifiClient.connect(cachedIp, cachedPort).catch(err => {
          warn('[设置] WiFi连接失败，触发BLE重连获取新IP:', err)
          // 旧IP连不上 → 触发BLE扫描获取设备当前IP
          this._autoReconnect()
        })
      }
    } else {
      // 无缓存IP时，才显示未连接状态
      // [FIX#5] 不中断正在进行的配网流程（扫描/连接/发送/等待IP）
      const activeSteps = [STEP.SCANNING, STEP.CONNECTED, STEP.SENDING, STEP.WAITING_IP]
      if (activeSteps.includes(this.data.currentStep)) {
        log('[设置] 配网流程进行中，不重置状态，当前:', this.data.currentStep)
      } else {
        log('[设置] 无缓存IP，等待用户手动配网')
        this._setStep(STEP.IDLE)
      }
      // 刷新手机网络信息
      this._getPhoneNetworkInfo()
    }
  },

  onUnload() {
    this._cleanup()
  },

  _autoReconnect() {
    log('[设置] 尝试自动重连BLE...')
    ble.scanAndConnect('sEMG_')
      .then(device => {
        log('[设置] BLE重连成功:', device.name)
        this._setStep(STEP.CONNECTED)
        this.setData({ bleConnected: true, deviceName: device.name || '', deviceRssi: device.RSSI || null })
        this._updateCanSend()
        this._startConnectionMonitor()
      })
      .catch(err => {
        error('[设置] BLE重连失败:', err)
        try { ble.close() } catch (e) {}  // [FIX#3] 清理BLE资源
      })
  },

  _startConnectionMonitor() {
    if (this._connectionTimer) return
    this._connectionTimer = setInterval(() => {
      if (!ble.isConnected()) {
        this.setData({ bleConnected: false })
        clearInterval(this._connectionTimer)
        this._connectionTimer = null
        log('[设置] BLE连接已断开')
      }
    }, 2000)
  },

  _cleanup() {
    if (this._connectionTimer) {
      clearInterval(this._connectionTimer)
      this._connectionTimer = null
    }
  },

  // ──── 事件处理 ────

  onStartScan() {
    if (this.data.isScanning || this.data.bleConnected) return

    this._setStep(STEP.SCANNING)
    this.setData({ isScanning: true })

    ble.scanAndConnect('sEMG_')
      .then(device => {
        log('[设置] BLE连接成功:', device.name, 'RSSI:', device.RSSI)
        this._setStep(STEP.CONNECTED)
        this.setData({
          bleConnected: true,
          deviceName: device.name || '',
          deviceRssi: device.RSSI || null,
          isScanning: false
        })
        this._updateCanSend()

        // 定期检查BLE连接状态
        this._startConnectionMonitor()
      })
      .catch(err => {
        error('[设置] BLE连接失败:', err)
        try { ble.close() } catch (e) {}  // [FIX#3] 清理BLE资源，避免后续连接失败
        this._setStep(STEP.FAILED)
        this.setData({ isScanning: false })
        this._showResult('error', '扫描失败: ' + this._getErrorMsg(err))
      })
  },

  onSsidInput(e) {
    this.setData({ ssid: e.detail.value })
    this._updateCanSend()
  },

  onPasswordInput(e) {
    this.setData({ password: e.detail.value })
    this._updateCanSend()
  },

  onTogglePassword() {
    this.setData({ showPassword: !this.data.showPassword })
  },

  async onSendCredentials() {
    if (!this.data.canSend) return

    const { ssid, password } = this.data
    if (!ssid) {
      this._showResult('error', '请输入 WiFi 名称')
      return
    }

    this._setStep(STEP.SENDING)
    log('[设置] 发送WiFi凭证: SSID=' + ssid)

    try {
      await ble.sendWifiConfig(ssid, password)
      log('[设置] 凭证已发送，等待设备响应...')
      // 缓存设备将要连接的WiFi名称
      wx.setStorageSync('device_ssid', ssid)
      this.setData({ deviceSsid: ssid })
      this._setStep(STEP.WAITING_IP)
      this._showResult('success', '凭证已发送，等待设备连接WiFi...')
    } catch (err) {
      error('[设置] BLE发送失败:', err)
      this._setStep(STEP.FAILED)
      this._showResult('error', '发送失败: ' + this._getErrorMsg(err))
    }
  },

  onReset() {
    this._cleanup()
    try { ble.close() } catch (e) {}

    this.setData({
      currentStep: STEP.IDLE,
      statusText: STATUS_TEXT[STEP.IDLE],
      statusClass: STATUS_CLASS[STEP.IDLE],
      deviceName: '',
      deviceRssi: null,
      bleConnected: false,
      deviceIp: '',
      ssid: '',
      password: '',
      isScanning: false,
      canSend: false,
      resultType: '',
      resultMessage: ''
    })
    // 清除缓存
    wx.removeStorageSync('device_ip')
    wx.removeStorageSync('device_port')
    wx.removeStorageSync('device_ssid')
    wx.removeStorageSync('config_step')
  },

  // ──── 内部方法 ────

  _setStep(step) {
    this.setData({
      currentStep: step,
      statusText: STATUS_TEXT[step] || '',
      statusClass: STATUS_CLASS[step] || 'idle'
    })
    wx.setStorageSync('config_step', step)
  },

  _updateCanSend() {
    const canSend = this.data.bleConnected && !!this.data.ssid
    this.setData({ canSend })
  },

  _showResult(type, message) {
    this.setData({ resultType: type, resultMessage: message })
    if (type === 'success' || type === 'error') {
      setTimeout(() => {
        // 只清除非关键提示
        if (this.data.resultMessage === message) {
          this.setData({ resultMessage: '' })
        }
      }, 4000)
    }
  },

  // ──── 获取手机网络信息 ────

  _getPhoneNetworkInfo() {
    // 初始化WiFi模块（必须先调用才能用getConnectedWifi）
    wx.startWifi({
      success: () => { this._doGetPhoneWifi() },
      fail: () => { this._doGetPhoneWifi() }
    })
  },

  // 获取手机WiFi信息（startWifi成功后调用）
  _doGetPhoneWifi() {
    wx.getConnectedWifi({
      success: (res) => {
        log('[设置] 手机WiFi:', res.wifi.SSID)
        this.setData({ phoneWifiSsid: res.wifi.SSID || '' })
      },
      fail: (err) => {
        const msg = err.errMsg || ''
        warn('[设置] 获取手机WiFi失败:', msg)
        if (msg.includes('GPS') || msg.includes('location') || msg.includes('定位')) {
          wx.showToast({ title: '请开启手机定位', icon: 'none', duration: 3000 })
        }
        this.setData({ phoneWifiSsid: '' })
      }
    })
  },

  _getErrorMsg(err) {
    if (!err) return '未知错误'
    if (typeof err === 'string') return err
    if (err.message) return err.message
    if (err.errMsg) return err.errMsg
    return JSON.stringify(err)
  }
})

