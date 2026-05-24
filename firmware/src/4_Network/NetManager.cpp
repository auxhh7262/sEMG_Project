// NetManager.cpp
#include "NetManager.h"
#include "BleConfigServer.h"
#include "3_Storage/StorageManager.h"
#include "0_Base/Logger.h"

// NTP 服务器配置
const char* NTP_SERVER = "ntp.aliyun.com";
const int NTP_PORT = 123;
const int NTP_PACKET_SIZE = 48;

// BLE重连时推送WiFi IP的静态回调
static NetManager* _netMgrInstance = nullptr;
static void onBleDeviceConnected() {
    if (_netMgrInstance) _netMgrInstance->_pushWifiIp();
}

extern StorageManager gStorage;
static void (*_cmdCallback)(uint8_t, const char*) = nullptr;

NetManager::NetManager()
    : _bleServer(nullptr), _wifiConnected(false), _wifiRetryTimer(0),
      _isNtpSynced(false), _tcpServer(8888), _tcpStreaming(false),
      _currentClient(255), _ntpBaseMillis(0), _ntpBaseSeconds(0),
      _eepromCredsTried(false), _dhcpWaitDone(false), _dhcpGotIp(false),
      _bleIpPushPending(false), _ntpPending(false), _ntpRequestTime(0),
      _lastDisconnectLogMs(0), _lastZombieCheck(0), _autoSeq(-1),
      _streamingRequested(false), _dhcpStableStart(0) {
    memset(_ntpPacketBuffer, 0, sizeof(_ntpPacketBuffer));
    memset(_tcpJsonBuf, 0, sizeof(_tcpJsonBuf));
}

void NetManager::startStreaming() {
    if (!_streamingRequested) {
        _streamingRequested = true;
        LOG("[NET] Streaming requested, will send data\n");
    }
}

void NetManager::stopStreaming() {
    if (_streamingRequested) {
        _streamingRequested = false;
        LOG("[NET] Streaming stopped\n");
    }
}

void NetManager::init(BleConfigServer* bleServer) {
    _bleServer = bleServer;
    _netMgrInstance = this;
    BleConfigServer::setWifiInfoCallback(onBleDeviceConnected);

    _tcpServer.begin();
    _ntpUdp.begin(8889);

    _tcpServer.onEvent([this](uint8_t num, WStype_t type, uint8_t *payload, size_t length) {
        switch (type) {
            case WStype_CONNECTED:
                LOG("[TCP] CONNECT event: client %d, _currentClient=%d\n", (int)num, (int)_currentClient);
                if (_currentClient != 255 && _currentClient != num) {
                    LOG("[TCP] Disconnecting old client %d for new client %d\n", (int)_currentClient, (int)num);
                    _tcpServer.disconnect(_currentClient);
                }
                _currentClient = num;
                _tcpStreaming = true;
                LOG("[TCP] Client %d connected (streaming enabled)\n", (int)num);
                break;
            case WStype_DISCONNECTED: {
                if (num != _currentClient) break;
                LOG("[TCP] DISCONNECT event: ACTIVE client %d, stopping streaming\n", (int)num);
                _tcpStreaming = false;
                stopStreaming();
                _currentClient = 255;
                break;
            }
            case WStype_TEXT:
                if (length > 0 && strstr((char*)payload, "\"cmd\":\"ping\"") != nullptr) {
                    static uint32_t _lastPingLogMs = 0;
                    if (millis() - _lastPingLogMs >= 2000) {
                        LOG("[TCP] RX: ping -> TX: pong\n");
                        _lastPingLogMs = millis();
                    }
                    sendJsonTo(num, "{\"cmd\":\"pong\",\"status\":\"ok\"}");
                    return;
                }
                if (_cmdCallback && length > 0) {
                    char* json = (char*)payload;
                    LOG("[NET] WS RX (client %d, len=%d): ", (int)num, (int)length);
                    for (size_t i = 0; i < length && i < 128; i++) { LOG("%c", json[i]); }
                    LOG("\n");
                    _cmdCallback(num, json);
                }
                break;
            default: break;
        }
    });

    LOG("[NET] NetManager initialized.\n");
    void* testMem = malloc(2048);
    if (testMem) { free(testMem); LOG("[NET] RAM Health OK (>2KB free)\n"); }
    else { LOG("[NET] WARNING: Low RAM (<2KB free), OOM crash risk!\n"); }
}

void NetManager::tick() {
    if (_bleServer && _bleServer->hasNewCredentials()) {
        WifiCredentials_t creds = _bleServer->consumeCredentials();
        LOG("[WIFI] New BLE credentials received, reconnecting to: %s\n", creds.ssid);
        WiFi.disconnect(); delay(200);
        gStorage.SaveWifiCredentials(&creds);
        WiFi.begin(creds.ssid, creds.pass);
        _wifiConnected = false; _dhcpWaitDone = false; _dhcpGotIp = false;
        _eepromCredsTried = true; _wifiRetryTimer = millis(); _dhcpStableStart = 0;
        return;
    }

    if (!_wifiConnected) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!_dhcpWaitDone) {
                _dhcpWaitStart = millis(); _dhcpWaitDone = true; _dhcpGotIp = false; _dhcpStableStart = 0;
                LOG("[WIFI] WL_CONNECTED, waiting for DHCP...\n");
            }
            String ip = WiFi.localIP().toString();
            if (ip != "0.0.0.0") _dhcpGotIp = true;
            if (_dhcpGotIp) {
                if (_dhcpStableStart == 0) _dhcpStableStart = millis();
                if (millis() - _dhcpStableStart >= 500) {
                    _wifiConnected = true; _eepromCredsTried = true;
                    LOG("[WIFI] Connected & Stable! IP: %s\n", WiFi.localIP().toString().c_str());
                    if (_bleServer) {
                        char ipJson[64]; snprintf(ipJson, sizeof(ipJson), "{\"ip\":\"%s\"}", WiFi.localIP().toString().c_str());
                        _bleServer->updateIpAddress(ipJson); _bleServer->resumeAdvertising(); _bleIpPushPending = false;
                        LOG("[WIFI] 精简IP已通知手机，BLE广播已恢复\n");
                    }
                    syncTime(); _dhcpStableStart = 0;
                }
            } else if (millis() - _dhcpWaitStart >= 5000) {
                LOG("[WIFI] DHCP timeout, IP still 0.0.0.0, disconnect and retry\n"); WiFi.disconnect();
                _wifiConnected = false; _dhcpWaitDone = false; _dhcpGotIp = false; _wifiRetryTimer = millis(); _dhcpStableStart = 0;
                if (_bleServer) { _bleServer->startAdvertising(); LOG("[WIFI] BLE广播已恢复（DHCP超时）\n"); }
            }
        } else if (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL) {
            if (millis() - _wifiRetryTimer > WIFI_RETRY_INTERVAL) {
                _wifiRetryTimer = millis(); _tryConnectWifi();
                static bool bleAdvertisedOnFail = false;
                if (!bleAdvertisedOnFail && _bleServer) { bleAdvertisedOnFail = true; _bleServer->startAdvertising(); LOG("[WIFI] BLE广播已恢复（连接失败）\n"); delay(0); }
            }
        } else if (WiFi.status() == WL_IDLE_STATUS) {
            if (millis() - _wifiRetryTimer > WIFI_RETRY_INTERVAL) { _wifiRetryTimer = millis(); _tryConnectWifi(); }
        }
    } else {
        if (WiFi.status() != WL_CONNECTED) {
            LOG("[WIFI] Connection lost!\n"); _wifiConnected = false; _eepromCredsTried = false; _wifiRetryTimer = millis();
            if (_bleServer) { _bleServer->startAdvertising(); LOG("[WIFI] BLE广播已恢复（连接断开）\n"); }
        } else {
            _handleNtp();
            if (_ntpPending && (millis() - _ntpRequestTime > 5000)) { _ntpPending = false; LOG("[NTP] Sync timeout, will retry in 60s\n"); _ntpRequestTime = millis(); }
            if (!_isNtpSynced && !_ntpPending && (millis() - _ntpRequestTime > 60000)) { syncTime(); }
        }
    }
    _tcpServer.loop();
}

bool NetManager::_tryConnectWifi() {
    if (!_eepromCredsTried) {
        _eepromCredsTried = true; WifiCredentials_t savedCreds;
        if (gStorage.LoadWifiCredentials(&savedCreds) && savedCreds.isValid) {
            LOG("[WIFI] Auto-connecting from EEPROM: %s\n", savedCreds.ssid); WiFi.begin(savedCreds.ssid, savedCreds.pass);
            _dhcpWaitDone = false; _dhcpGotIp = false; return true;
        }
        LOG("[WIFI] No saved credentials in EEPROM\n");
    }
    if (_bleServer && _bleServer->hasNewCredentials()) {
        WifiCredentials_t creds = _bleServer->consumeCredentials(); LOG("[WIFI] Got new credentials: %s\n", creds.ssid);
        WiFi.disconnect(); delay(200); gStorage.SaveWifiCredentials(&creds); LOG("[WIFI] Connecting via BLE: %s\n", creds.ssid);
        WiFi.begin(creds.ssid, creds.pass); _wifiRetryTimer = millis(); _wifiConnected = false; _dhcpWaitDone = false; _dhcpGotIp = false; return true;
    }
    return false;
}

void NetManager::connectWifi(const char* ssid, const char* pass) {
    LOG("[WIFI] Manual connect request: %s\n", ssid); WiFi.begin(ssid, pass);
    _wifiRetryTimer = millis(); _wifiConnected = false; _dhcpWaitDone = false; _dhcpGotIp = false;
}

void NetManager::disconnectWifi() {
    LOG("[WIFI] Disconnecting...\n"); WiFi.disconnect(); _wifiConnected = false; _eepromCredsTried = false;
    if (_bleServer) _bleServer->startAdvertising();
}

void NetManager::syncTime() {
    if (!_wifiConnected) return; IPAddress ntpIp;
    if (!WiFi.hostByName(NTP_SERVER, ntpIp)) { LOG("[NTP] DNS failed for %s, will retry\n", NTP_SERVER); _ntpPending = false; _ntpRequestTime = millis(); return; }
    LOG("[NTP] Requesting time sync from %s (%s)...\n", NTP_SERVER, ntpIp.toString().c_str());
    _ntpPending = true; _ntpRequestTime = millis(); memset(_ntpPacketBuffer, 0, NTP_PACKET_SIZE);
    _ntpPacketBuffer[0] = 0b11100011; _ntpUdp.beginPacket(ntpIp, NTP_PORT); _ntpUdp.write(_ntpPacketBuffer, NTP_PACKET_SIZE); _ntpUdp.endPacket();
}

void NetManager::_handleNtp() {
    int packetSize = _ntpUdp.parsePacket();
    if (packetSize >= NTP_PACKET_SIZE) {
        _ntpUdp.read(_ntpPacketBuffer, NTP_PACKET_SIZE);
        unsigned long highWord = word(_ntpPacketBuffer[40], _ntpPacketBuffer[41]);
        unsigned long lowWord = word(_ntpPacketBuffer[42], _ntpPacketBuffer[43]);
        unsigned long secsSince1900 = highWord << 16 | lowWord;
        const unsigned long seventyYears = 2208988800UL;
        _ntpBaseSeconds = secsSince1900 - seventyYears; _ntpBaseMillis = millis(); _isNtpSynced = true; _ntpPending = false;
        LOG("[NTP] Sync successful. Base time: %lu\n", _ntpBaseSeconds);
    }
}

void NetManager::_getTimestamp(uint32_t &sec, uint16_t &ms) {
    if (_isNtpSynced) { uint32_t elapsed = millis() - _ntpBaseMillis; sec = _ntpBaseSeconds + (elapsed / 1000); ms = elapsed % 1000; }
    else { sec = millis() / 1000; ms = millis() % 1000; }
}

void NetManager::sendData(float rms, float mdf, float fatigue, uint8_t quality, float activation, bool isCalibMode, const char* calibPhase) {
    if (!_tcpStreaming || !_streamingRequested) {
        static uint32_t _lastBlockLogMs = 0;
        if (millis() - _lastBlockLogMs >= 2000) { _lastBlockLogMs = millis(); LOG("[NET] DATA BLOCKED! tcpStream=%d, streamReq=%d\n", _tcpStreaming, _streamingRequested); }
        return;
    }
    uint32_t sec; uint16_t ms; _getTimestamp(sec, ms); int written;
    if (isCalibMode) {
        written = snprintf(_tcpJsonBuf, sizeof(_tcpJsonBuf), "{\"type\":\"calib_data\",\"phase\":\"%s\",\"ts\":%lu%03u,\"rms\":%.2f,\"mdf\":%.1f}", calibPhase ? calibPhase : "REST", sec, ms, rms, mdf);
    } else {
        written = snprintf(_tcpJsonBuf, sizeof(_tcpJsonBuf), "{\"type\":\"data\",\"ts\":%lu%03u,\"r\":%.2f,\"m\":%.1f,\"f\":%.2f,\"q\":%u,\"a\":%.2f}", sec, ms, rms, mdf, (double)fatigue, quality, (double)activation);
    }
    if (written < 0 || (uint32_t)written >= sizeof(_tcpJsonBuf)) { LOG("[TCP] JSON buffer overflow! Need %d bytes\n", written); return; }
    static uint32_t _lastDataLogMs = 0;
    if (millis() - _lastDataLogMs >= 2000) { _lastDataLogMs = millis(); if (isCalibMode) LOG("[TCP] TX(calib): rms=%.2f mdf=%.1f\n", rms, mdf); else LOG("[TCP] TX: rms=%.2f mdf=%.1f fat=%.2f q=%u act=%.2f\n", rms, mdf, (double)fatigue, quality, (double)activation); }
    _tcpServer.broadcastTXT(_tcpJsonBuf);
}

bool NetManager::isWifiConnected() const { return _wifiConnected; }
bool NetManager::isTcpClientConnected() const { return _tcpStreaming; }
void NetManager::setAutoSeq(int seq) { _autoSeq = seq; }

// 🌟 终极修复版：直接使用全局 _tcpServer，废弃 _clients，加入流控绿通道
void NetManager::sendJsonTo(uint8_t clientNum, const char* json) {
    if (_currentClient == 255) return; // 修复：移除未定义的 TCP_MAX_CLIENTS，仅用 255 判断无连接

    bool wasStreaming = _streamingRequested;
    if (wasStreaming) {
        _streamingRequested = false; // 暂停流，给 ACK 让路
    }

    _tcpServer.sendTXT(clientNum, json); // 使用正确的全局 server 发送

    if (wasStreaming) {
        _streamingRequested = true; // 恢复流
    }
}


void NetManager::setCommandCallback(void (*callback)(uint8_t, const char*)) { _cmdCallback = callback; }

void NetManager::_pushWifiIp() {
    if (!_bleServer) return;
    if (!_wifiConnected) { LOG("[NET] BLE请求IP，但WiFi未连接，标记pending\n"); _bleIpPushPending = true; return; }
    char ipJson[64]; snprintf(ipJson, sizeof(ipJson), "{\"ip\":\"%s\"}", WiFi.localIP().toString().c_str());
    bool ok = _bleServer->updateIpAddress(ipJson);
    if (ok) { LOG("[NET] BLE重连 → 已推送精简IP: %s\n", WiFi.localIP().toString().c_str()); }
    else { LOG("[NET] BLE重连 → IP推送失败\n"); }
}
