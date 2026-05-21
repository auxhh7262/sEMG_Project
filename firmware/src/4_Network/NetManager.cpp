// NetManager.cpp
#include "NetManager.h"
#include "BleConfigServer.h"
#include "3_Storage/StorageManager.h"
#include "0_Base/Logger.h"

// NTP 服务器配置
const char* NTP_SERVER = "ntp.aliyun.com";
const int NTP_PORT = 123;
const int NTP_PACKET_SIZE = 48;

// [B1-1-fix] 需要访问 StorageManager 加载 EEPROM 中的 WiFi 凭证
extern StorageManager gStorage;

// JSON 命令回调
static void (*_cmdCallback)(uint8_t, const char*) = nullptr;

NetManager::NetManager() 
    : _bleServer(nullptr),
      _wifiConnected(false),
      _wifiRetryTimer(0),
      _isNtpSynced(false),
      _tcpServer(8888),
      _tcpStreaming(false),
      _currentClient(255),
      _ntpBaseMillis(0),
      _ntpBaseSeconds(0),
      _eepromCredsTried(false),
      _dhcpWaitDone(false),
      _dhcpGotIp(false),
      _ntpPending(false),
      _ntpRequestTime(0),
      _lastDisconnectLogMs(0),
      _autoSeq(-1) {
    memset(_ntpPacketBuffer, 0, sizeof(_ntpPacketBuffer));
    memset(_tcpJsonBuf, 0, sizeof(_tcpJsonBuf));
}

void NetManager::init(BleConfigServer* bleServer) {
    _bleServer = bleServer;
    _tcpServer.begin();

    // 初始化 NTP UDP 端口（必须调用 begin 才能收发 UDP 包）
    _ntpUdp.begin(8889);  // 本地端口，避免与TCP 8888冲突

    _tcpServer.onEvent([this](uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
        switch (type) {
            case WStype_CONNECTED:
                _tcpStreaming = true;
                _currentClient = num;
                LOG("[TCP] Client %d connected\n", num);
                break;
            case WStype_DISCONNECTED:
                // [修复] 只有当前活跃客户端断开才关闭 streaming
                // 避免 Client 1（内部重连/GC）断开影响 Client 0 的数据流
                if (num == _currentClient) {
                    _tcpStreaming = false;
                    _currentClient = 255;
                    // 限频：至少30秒间隔才打印断连日志
                    if (millis() - _lastDisconnectLogMs > 30000) {
                        LOG("[TCP] Client %d disconnected\n", num);
                        _lastDisconnectLogMs = millis();
                    }
                }
                // else: 非当前客户端断开，静默忽略（减少日志噪音）
                break;
            case WStype_TEXT:
                // 处理 JSON 命令
                if (_cmdCallback && length > 0) {
                    char* json = (char*)payload;
                    _cmdCallback(num, json);
                }
                break;
            default:
                break;
        }
    });
    LOG("[NET] NetManager initialized.\n");
}

void NetManager::tick() {
    // [BLE-Config-Fix] 最高优先级：检测 BLE 新凭证，即使 WiFi 已连接也要立即处理
    if (_bleServer && _bleServer->hasNewCredentials()) {
        WifiCredentials_t creds = _bleServer->consumeCredentials();
        LOG("[WIFI] New BLE credentials received, reconnecting to: %s\n", creds.ssid);
        
        // 断开当前 WiFi，确保干净切换
        WiFi.disconnect();
        delay(200);
        
        // 保存新凭证到 EEPROM
        gStorage.SaveWifiCredentials(&creds);
        
        // 连接新 WiFi
        WiFi.begin(creds.ssid, creds.pass);
        
        // 重置连接状态，等待下一轮 tick 检测连接结果
        _wifiConnected = false;
        _eepromCredsTried = true;  // 防止 EEPROM 凭证覆盖新凭证
        _wifiRetryTimer = millis();
        return;  // 跳过后续逻辑，等下一轮 tick
    }
    
    // 1. 管理 WiFi 连接逻辑
    if (!_wifiConnected) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!_dhcpWaitDone) {
                // [FIX] 非阻塞DHCP等待：首次检测到WL_CONNECTED时启动等待
                _dhcpWaitStart = millis();
                _dhcpWaitDone = true;
                _dhcpGotIp = false;
                LOG("[WIFI] WL_CONNECTED, waiting for DHCP...\n");
            }
            // 非阻塞检查DHCP IP
            String ip = WiFi.localIP().toString();
            if (ip != "0.0.0.0") {
                _dhcpGotIp = true;
            }
            if (_dhcpGotIp) {
                // DHCP成功获取IP
                _wifiConnected = true;
                _eepromCredsTried = true;

                LOG("[WIFI] Connected! IP: %s\n", WiFi.localIP().toString().c_str());
                // 通知手机IP地址和WiFi名称
                if (_bleServer) {
                    char ipJson[BLE_IP_CHAR_MAX_LEN];
                    snprintf(ipJson, sizeof(ipJson),
                        "{\"ip\":\"%s\",\"ssid\":\"%s\",\"deviceId\":\"sEMG\"}",
                        WiFi.localIP().toString().c_str(),
                        WiFi.SSID());
                    _bleServer->updateIpAddress(ipJson);
                    _bleServer->resumeAdvertising();
                    LOG("[WIFI] IP已通知手机，BLE广播已恢复\n");
                }
                syncTime();
            } else if (millis() - _dhcpWaitStart >= 5000) {
                // DHCP超时，IP仍为0.0.0.0 — 不设_wifiConnected=true，允许后续重试
                LOG("[WIFI] DHCP timeout, IP still 0.0.0.0, will retry\n");
                _wifiConnected = false;
                _dhcpWaitDone = false;
                _wifiRetryTimer = millis();
                // [FIX#8] WiFi连接失败后恢复BLE广播，允许重新配网
                if (_bleServer) {
                    _bleServer->startAdvertising();
                    LOG("[WIFI] BLE广播已恢复（DHCP超时）\n");
                }
            }
            // 否则：DHCP还没完成且未超时，下一tick继续检查
        } else if (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL) {
            // 连接失败，定期重试
            if (millis() - _wifiRetryTimer > WIFI_RETRY_INTERVAL) {
                _wifiRetryTimer = millis();
                _tryConnectWifi();
                // [FIX#8] WiFi连接失败后恢复BLE广播，允许重新配网
                static bool bleAdvertisedOnFail = false;
                if (!bleAdvertisedOnFail && _bleServer) {
                    bleAdvertisedOnFail = true;
                    _bleServer->startAdvertising();
                    LOG("[WIFI] BLE广播已恢复（连接失败）\n");
                    // 10秒后重置标记，允许再次通知
                    delay(0);  // yield
                    // 下次成功连接时会在tick中清除此标记
                }
            }
        } else if (WiFi.status() == WL_IDLE_STATUS) {
            // 未发起连接，定期尝试
            if (millis() - _wifiRetryTimer > WIFI_RETRY_INTERVAL) {
                _wifiRetryTimer = millis();
                _tryConnectWifi();
            }
        }
    } else {
        // WiFi 已连接，监测是否断开
        if (WiFi.status() != WL_CONNECTED) {
            LOG("[WIFI] Connection lost!\n");
            _wifiConnected = false;
            _eepromCredsTried = false;  // [B1-1-fix] 断开后重新尝试EEPROM凭证
            _wifiRetryTimer = millis();
            // [FIX#8] WiFi断线后恢复BLE广播
            if (_bleServer) {
                _bleServer->startAdvertising();
                LOG("[WIFI] BLE广播已恢复（连接断开）\n");
            }
        } else {
            _handleNtp();
            // NTP 超时检测（5秒无响应视为失败）
            if (_ntpPending && (millis() - _ntpRequestTime > 5000)) {
                _ntpPending = false;
                LOG("[NTP] Sync timeout, will retry in 60s\n");
                _ntpRequestTime = millis();
            }
            // NTP 定期重试（未同步且非等待中，60秒间隔）
            if (!_isNtpSynced && !_ntpPending && (millis() - _ntpRequestTime > 60000)) {
                syncTime();
            }
        }
    }

    // 2. 管理 WebSocket 连接
    _tcpServer.loop();
}

// [B1-1-fix] _tryConnectWifi: 优先从 EEPROM 加载已保存凭证，再从 BLE 获取新凭证
bool NetManager::_tryConnectWifi() {
    // 优先级1：从 EEPROM 加载已保存的 WiFi 凭证（开机自动重连）
    if (!_eepromCredsTried) {
        _eepromCredsTried = true;
        WifiCredentials_t savedCreds;
        if (gStorage.LoadWifiCredentials(&savedCreds) && savedCreds.isValid) {
            LOG("[WIFI] Auto-connecting from EEPROM: %s\n", savedCreds.ssid);
            WiFi.begin(savedCreds.ssid, savedCreds.pass);
            return true;
        }
        LOG("[WIFI] No saved credentials in EEPROM\n");
    }

    // 优先级2：从 BLE 获取新凭证
    if (_bleServer && _bleServer->hasNewCredentials()) {
        WifiCredentials_t creds = _bleServer->consumeCredentials();
        LOG("[WIFI] Got new credentials: %s\n", creds.ssid);

        // 强制断开旧WiFi连接，确保干净切换
        WiFi.disconnect();
        delay(200);

        // 保存新凭证到EEPROM
        gStorage.SaveWifiCredentials(&creds);

        // 连接新WiFi
        LOG("[WIFI] Connecting via BLE: %s\n", creds.ssid);
        WiFi.begin(creds.ssid, creds.pass);

        // 重置重试计时器，立即开始重连流程
        _wifiRetryTimer = millis();
        _wifiConnected = false;
        return true;
    }
    return false;
}

void NetManager::connectWifi(const char* ssid, const char* pass) {
    LOG("[WIFI] Manual connect request: %s\n", ssid);
    WiFi.begin(ssid, pass);
    _wifiRetryTimer = millis();
}

void NetManager::disconnectWifi() {
    LOG("[WIFI] Disconnecting...\n");
    WiFi.disconnect();
    _wifiConnected = false;
    _eepromCredsTried = false;  // [B1-1-fix] 断开后允许重新尝试
    if (_bleServer) {
        _bleServer->startAdvertising();
    }
}

void NetManager::syncTime() {
    if (!_wifiConnected) return;

    // DNS 解析 NTP 服务器地址
    IPAddress ntpIp;
    if (!WiFi.hostByName(NTP_SERVER, ntpIp)) {
        LOG("[NTP] DNS failed for %s, will retry\n", NTP_SERVER);
        _ntpPending = false;
        _ntpRequestTime = millis();  // 记录时间，60秒后重试
        return;
    }

    LOG("[NTP] Requesting time sync from %s (%s)...\n", NTP_SERVER, ntpIp.toString().c_str());
    _ntpPending = true;
    _ntpRequestTime = millis();
    memset(_ntpPacketBuffer, 0, NTP_PACKET_SIZE);
    _ntpPacketBuffer[0] = 0b11100011;  // LI=0, VN=4, Mode=3 (client)
    _ntpUdp.beginPacket(ntpIp, NTP_PORT);
    _ntpUdp.write(_ntpPacketBuffer, NTP_PACKET_SIZE);
    _ntpUdp.endPacket();
}

void NetManager::_handleNtp() {
    int packetSize = _ntpUdp.parsePacket();
    if (packetSize >= NTP_PACKET_SIZE) {
        _ntpUdp.read(_ntpPacketBuffer, NTP_PACKET_SIZE);
        unsigned long highWord = word(_ntpPacketBuffer[40], _ntpPacketBuffer[41]);
        unsigned long lowWord = word(_ntpPacketBuffer[42], _ntpPacketBuffer[43]);
        unsigned long secsSince1900 = highWord << 16 | lowWord;
        const unsigned long seventyYears = 2208988800UL;
        _ntpBaseSeconds = secsSince1900 - seventyYears;
        _ntpBaseMillis = millis();
        _isNtpSynced = true;
        _ntpPending = false;
        LOG("[NTP] Sync successful. Base time: %lu\n", _ntpBaseSeconds);
    }
}

void NetManager::_getTimestamp(uint32_t &sec, uint16_t &ms) {
    if (_isNtpSynced) {
        uint32_t elapsed = millis() - _ntpBaseMillis;
        sec = _ntpBaseSeconds + (elapsed / 1000);
        ms = elapsed % 1000;
    } else {
        sec = millis() / 1000;
        ms = millis() % 1000;
    }
}

// [B1-4-fix] JSON 缓冲区从128字节扩展到192字节，防止 snprintf 截断
void NetManager::sendData(float rms, float mdf, float fatigue, uint8_t quality, float activation, bool isCalibMode, const char* calibPhase) {
    if (!_tcpStreaming) return;
    
    uint32_t sec;
    uint16_t ms;
    _getTimestamp(sec, ms);

    int written;
    if (isCalibMode) {
        // 校准模式：加type+phase让小程序onCalibData匹配
        written = snprintf(_tcpJsonBuf, sizeof(_tcpJsonBuf),
                 "{\"type\":\"calib_data\",\"phase\":\"%s\",\"ts\":%lu%03u,\"r\":%.2f,\"m\":%.1f}",
                 calibPhase ? calibPhase : "REST", sec, ms, rms, mdf);
    } else {
        written = snprintf(_tcpJsonBuf, sizeof(_tcpJsonBuf),
                 "{\"ts\":%lu%03u,\"r\":%.2f,\"m\":%.1f,\"f\":%.2f,\"q\":%u,\"a\":%.2f}",
                 sec, ms, rms, mdf, (double)fatigue, quality, (double)activation);
    }
    if (written < 0 || (uint32_t)written >= sizeof(_tcpJsonBuf)) {
        LOG("[TCP] JSON buffer overflow! Need %d bytes\n", written);
        return;
    }
    // [FIX] 限频日志：每2秒打印一次，避免10Hz刷屏
    static uint32_t _lastDataLogMs = 0;
    if (millis() - _lastDataLogMs >= 2000) {
        _lastDataLogMs = millis();
        if (isCalibMode) LOG("[TCP] TX(calib): rms=%.2f mdf=%.1f\n", rms, mdf);
        else LOG("[TCP] TX: rms=%.2f mdf=%.1f fat=%.2f q=%u act=%.2f\n", rms, mdf, (double)fatigue, quality, (double)activation);
    }
    _tcpServer.broadcastTXT(_tcpJsonBuf);
}

bool NetManager::isWifiConnected() const {
    return _wifiConnected;
}

bool NetManager::isTcpClientConnected() const {
    return _tcpStreaming;
}

void NetManager::setAutoSeq(int seq) {
    _autoSeq = seq;
    LOG("[NET] setAutoSeq=%d\n", seq);
}

void NetManager::sendJsonTo(uint8_t clientNum, const char* json) {
    // [v3.9.14] 自动附加 seq（命令响应匹配）
    if (_autoSeq >= 0 && clientNum < 255) {
        // 构造带 seq 的 JSON：在 closing } 前插入 "seq":N
        char buf[512];
        int len = strlen(json);
        if (len > 2 && json[len-1] == '}') {
            // 检查是否已有 seq 字段（避免重复）
            if (strstr(json, "\"seq\"") == nullptr) {
                snprintf(buf, sizeof(buf), "%.*s,\"seq\":%d}", len-1, json, _autoSeq);
                _tcpServer.sendTXT(clientNum, buf);
                return;
            }
        }
        // 无法附加 seq，原样发送
        _tcpServer.sendTXT(clientNum, json);
        return;
    }
    
    if (clientNum < 255) {
        _tcpServer.sendTXT(clientNum, json);
    }
}

// [v3.9.14] 带 seq 重载（显式指定，优先级高于 _autoSeq）
void NetManager::sendJsonTo(uint8_t clientNum, const char* json, int seq) {
    if (seq >= 0 && clientNum < 255) {
        char buf[512];
        int len = strlen(json);
        if (len > 2 && json[len-1] == '}') {
            if (strstr(json, "\"seq\"") == nullptr) {
                snprintf(buf, sizeof(buf), "%.*s,\"seq\":%d}", len-1, json, seq);
                _tcpServer.sendTXT(clientNum, buf);
                return;
            }
        }
    }
    // 降级到自动模式或原样发送
    sendJsonTo(clientNum, json);
}

void NetManager::setCommandCallback(void (*callback)(uint8_t, const char*)) {
    _cmdCallback = callback;
}
