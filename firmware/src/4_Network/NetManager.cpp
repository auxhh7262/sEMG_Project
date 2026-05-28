// NetManager.cpp — Simplified: no heartbeat, no pauseStreaming, CONNECT=send, DISCONNECT=stop
#include "NetManager.h"
#include "BleConfigServer.h"
#include "3_Storage/StorageManager.h"
#include "0_Base/Logger.h"

const char* NTP_SERVER = "ntp.aliyun.com";
const int NTP_PORT = 123;
const int NTP_PACKET_SIZE = 48;

static NetManager* _netMgrInstance = nullptr;

static void onBleDeviceConnected() {
    if (_netMgrInstance) _netMgrInstance->_pushWifiIp();
}

extern StorageManager gStorage;
static void (*_cmdCallback)(uint8_t, const char*) = nullptr;

NetManager::NetManager()
    : _bleServer(nullptr), _wifiConnected(false), _wifiRetryTimer(0),
      _isNtpSynced(false), _tcpServer(8888), _tcpStreaming(false),
      _currentClient(255), _lastTcpRxMs(0), _lastPingMs(0), _ntpBaseMillis(0), _ntpBaseSeconds(0),
      _eepromCredsTried(false), _dhcpWaitDone(false), _dhcpGotIp(false),
      _bleIpPushPending(false), _ntpPending(false), _ntpRequestTime(0),
      _lastDisconnectLogMs(0), _autoSeq(-1), _dhcpStableStart(0) {
    memset(_ntpPacketBuffer, 0, sizeof(_ntpPacketBuffer));
    memset(_tcpJsonBuf, 0, sizeof(_tcpJsonBuf));
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
                LOG("[TCP] CONNECT: client %d (prev=%d)\n", (int)num, (int)_currentClient);
                if (_currentClient != 255 && _currentClient != num) {
                    LOG("[TCP] Forcing disconnect of old client %d\n", (int)_currentClient);
                    _tcpServer.disconnect(_currentClient);
                }
                _currentClient = num;
                _tcpStreaming = true;
                break;

            case WStype_DISCONNECTED: {
                // 旧客户端断开：忽略（固件重启后 WebSocket 服务器里残留的旧连接）
                if (num != _currentClient) break;
                // ✅ 相同客户端 1 秒内只记录一次，避免残留连接刷屏
                static uint32_t _lastDiscMs[8] = {0};
                uint32_t nowMs = millis();
                if (nowMs - _lastDiscMs[num & 0x07] >= 1000) {
                    _lastDiscMs[num & 0x07] = nowMs;
                    LOG("[TCP] DISCONNECT: client %d\n", (int)num);
                }
                _tcpStreaming = false;
                _currentClient = 255;
                break;
            }

            case WStype_PING:
                LOG("[TCP] PING from client %d\n", (int)num);
                break;
            case WStype_PONG:
                LOG("[TCP] PONG from client %d\n", (int)num);
                break;

            default:
                LOG("[TCP] EVENT type=%d from client %d\n", (int)type, (int)num);
                break;
            case WStype_TEXT:
                if (length > 0) {
                    char* json = (char*)payload;
                    _lastTcpRxMs = millis();
                    LOG("[NET] RX (client %d, len=%d): %.128s\n", (int)num, (int)length, json);

                    // Auto-ACK for commands with seq
                    if (strstr(json, "\"cmd\":\"start_stream") != nullptr) {
                        char* seqStr = strstr(json, "\"seq\":");
                        if (seqStr) {
                            char seqPart[20] = {0};
                            for(int i=0; i<19 && seqStr[i] != '\0' && seqStr[i] != '}'; i++)
                                seqPart[i] = seqStr[i];
                            char ackBuf[128];
                            snprintf(ackBuf, sizeof(ackBuf), "{\"cmd\":\"start_stream\",\"status\":\"ok\",%s}", seqPart);
                            sendJsonTo(num, ackBuf);
                        }
                    } else if (strstr(json, "\"cmd\":\"query_cz") != nullptr) {
                        char* seqStr = strstr(json, "\"seq\":");
                        if (seqStr) {
                            char seqPart[20] = {0};
                            for(int i=0; i<19 && seqStr[i] != '\0' && seqStr[i] != '}'; i++)
                                seqPart[i] = seqStr[i];
                            char ackBuf[128];
                            snprintf(ackBuf, sizeof(ackBuf), "{\"cmd\":\"query_cz\",\"status\":\"ok\",%s}", seqPart);
                            sendJsonTo(num, ackBuf);
                        }
                    }

                    if (_cmdCallback) {
                        _cmdCallback(num, json);
                    }
                }
                break;
        }
    });

    LOG("[NET] NetManager initialized.\n");
}

void NetManager::tick() {
    if (_bleServer && _bleServer->hasNewCredentials()) {
        WifiCredentials_t creds = _bleServer->consumeCredentials();
        WiFi.disconnect();
        delay(200);
        gStorage.SaveWifiCredentials(&creds);
        WiFi.begin(creds.ssid, creds.pass);
        _wifiConnected = false;
        _dhcpWaitDone = false;
        _dhcpGotIp = false;
        _eepromCredsTried = true;
        _wifiRetryTimer = millis();
        _dhcpStableStart = 0;
        return;
    }

    if (!_wifiConnected) {
        if (WiFi.status() == WL_CONNECTED) {
            if (!_dhcpWaitDone) {
                _dhcpWaitStart = millis();
                _dhcpWaitDone = true;
                _dhcpGotIp = false;
                _dhcpStableStart = 0;
                LOG("[WIFI] WL_CONNECTED, waiting for DHCP...\n");
            }
            String ip = WiFi.localIP().toString();
            if (ip != "0.0.0.0") _dhcpGotIp = true;
            if (_dhcpGotIp) {
                if (_dhcpStableStart == 0) _dhcpStableStart = millis();
                if (millis() - _dhcpStableStart >= 500) {
                    _wifiConnected = true;
                    _eepromCredsTried = true;
                    LOG("[WIFI] Connected & Stable! IP: %s\n", WiFi.localIP().toString().c_str());
                    if (_bleServer) {
                        char ipJson[64];
                        snprintf(ipJson, sizeof(ipJson), "{\"ip\":\"%s\"}", WiFi.localIP().toString().c_str());
                        _bleServer->updateIpAddress(ipJson);
                        _bleServer->resumeAdvertising();
                        _bleIpPushPending = false;
                        LOG("[WIFI] IP已通知手机，BLE广播已恢复\n");
                    }
                    syncTime();
                    _dhcpStableStart = 0;
                }
            } else if (millis() - _dhcpWaitStart >= 5000) {
                WiFi.disconnect();
                _wifiConnected = false;
                _dhcpWaitDone = false;
                _dhcpGotIp = false;
                _wifiRetryTimer = millis();
                _dhcpStableStart = 0;
                if (_bleServer) {
                    _bleServer->startAdvertising();
                    LOG("[WIFI] BLE广播已恢复（DHCP超时）\n");
                }
            }
        } else if (WiFi.status() == WL_CONNECT_FAILED || WiFi.status() == WL_NO_SSID_AVAIL) {
            if (millis() - _wifiRetryTimer > WIFI_RETRY_INTERVAL) {
                _wifiRetryTimer = millis();
                _tryConnectWifi();
            }
        } else if (WiFi.status() == WL_IDLE_STATUS) {
            if (millis() - _wifiRetryTimer > WIFI_RETRY_INTERVAL) {
                _wifiRetryTimer = millis();
                _tryConnectWifi();
            }
        }
    } else {
        if (WiFi.status() != WL_CONNECTED) {
            LOG("[WIFI] Connection lost!\n");
            _wifiConnected = false;
            _eepromCredsTried = false;
            _wifiRetryTimer = millis();
            if (_bleServer) {
                _bleServer->startAdvertising();
                LOG("[WIFI] BLE广播已恢复（连接断开）\n");
            }
        } else {
            _handleNtp();
            if (_ntpPending && (millis() - _ntpRequestTime > 5000)) {
                _ntpPending = false;
                LOG("[NTP] Sync timeout, will retry in 60s\n");
                _ntpRequestTime = millis();
            }
            if (!_isNtpSynced && !_ntpPending && (millis() - _ntpRequestTime > 60000)) {
                syncTime();
            }
        }
    }
    // ✅ TCP 存活检测：仅在空闲时检测（流式传输中连接一定是活的）
    if (!_tcpStreaming && _currentClient != 255) {
        uint32_t now = millis();
        if (now - _lastTcpRxMs >= 30000 && _lastTcpRxMs > 0) {
            LOG("[TCP] Idle %ds, disconnecting stale client %d\n", (now - _lastTcpRxMs) / 1000, _currentClient);
            _tcpServer.disconnect(_currentClient);
            _currentClient = 255;
        }
    }
    _tcpServer.loop();
}

bool NetManager::_tryConnectWifi() {
    static uint8_t _retryCnt = 0;
    if (WiFi.status() == WL_CONNECTED) {
        _retryCnt = 0;
        return true;
    }

    if (!_eepromCredsTried) {
        _eepromCredsTried = true;
        WifiCredentials_t savedCreds;
        if (gStorage.LoadWifiCredentials(&savedCreds) && savedCreds.isValid) {
            LOG("[WIFI] Auto-connecting from EEPROM: %s\n", savedCreds.ssid);
            WiFi.begin(savedCreds.ssid, savedCreds.pass);
            _dhcpWaitDone = false;
            _dhcpGotIp = false;
            _retryCnt = 0;
            return true;
        }
        LOG("[WIFI] No saved credentials in EEPROM\n");
    }

    if (_bleServer && _bleServer->hasNewCredentials()) {
        WifiCredentials_t creds = _bleServer->consumeCredentials();
        WiFi.disconnect();
        delay(200);
        gStorage.SaveWifiCredentials(&creds);
        WiFi.begin(creds.ssid, creds.pass);
        _wifiRetryTimer = millis();
        _wifiConnected = false;
        _dhcpWaitDone = false;
        _dhcpGotIp = false;
        _retryCnt = 0;
        return true;
    }

    if (++_retryCnt > 10) {
        _retryCnt = 0;
        LOG("[WIFI] Too many retries, stopping.\n");
        if (_bleServer) _bleServer->startAdvertising();
        return false;
    }
    return false;
}

void NetManager::connectWifi(const char* ssid, const char* pass) {
    WiFi.begin(ssid, pass);
    _wifiRetryTimer = millis();
    _wifiConnected = false;
    _dhcpWaitDone = false;
    _dhcpGotIp = false;
}

void NetManager::disconnectWifi() {
    WiFi.disconnect();
    _wifiConnected = false;
    _eepromCredsTried = false;
    if (_bleServer) _bleServer->startAdvertising();
}

void NetManager::syncTime() {
    if (!_wifiConnected) return;
    IPAddress ntpIp;
    if (!WiFi.hostByName(NTP_SERVER, ntpIp)) {
        _ntpPending = false;
        _ntpRequestTime = millis();
        return;
    }
    _ntpPending = true;
    _ntpRequestTime = millis();
    memset(_ntpPacketBuffer, 0, NTP_PACKET_SIZE);
    _ntpPacketBuffer[0] = 0b11100011;
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

void NetManager::sendData(float rms, float mdf, float fatigue, uint8_t quality, float activation, bool isCalibMode, const char* calibPhase) {
    if (!_tcpStreaming) return;

    uint32_t sec;
    uint16_t ms;
    _getTimestamp(sec, ms);

    int written;
    if (isCalibMode)
        written = snprintf(_tcpJsonBuf, sizeof(_tcpJsonBuf), "{\"type\":\"calib_data\",\"phase\":\"%s\",\"ts\":%lu%03u,\"rms\":%.2f,\"mdf\":%.1f}", calibPhase ? calibPhase : "REST", sec, ms, rms, mdf);
    else
        written = snprintf(_tcpJsonBuf, sizeof(_tcpJsonBuf), "{\"type\":\"data\",\"ts\":%lu%03u,\"r\":%.2f,\"m\":%.1f,\"f\":%.2f,\"q\":%u,\"a\":%.2f}", sec, ms, rms, mdf, (double)fatigue, quality, (double)activation);

    if (written < 0 || (size_t)written >= sizeof(_tcpJsonBuf)) {
        LOG("[TCP] JSON buffer overflow!\n");
        return;
    }

    size_t bufLen = strlen(_tcpJsonBuf);
    if (bufLen + 1 < sizeof(_tcpJsonBuf)) {
        _tcpJsonBuf[bufLen] = '\n';
        _tcpJsonBuf[bufLen + 1] = '\0';
        bufLen++;
    }

    static uint32_t _lastDataLogMs = 0;
    if (millis() - _lastDataLogMs >= 2000) {
        _lastDataLogMs = millis();
        LOG("[TCP] TX: rms=%.2f mdf=%.1f fat=%.2f q=%u act=%.2f\n", rms, mdf, (double)fatigue, quality, (double)activation);
    }

    _tcpServer.broadcastTXT(_tcpJsonBuf, bufLen);
}

bool NetManager::isWifiConnected() const { return _wifiConnected; }
bool NetManager::isTcpClientConnected() const { return _tcpStreaming; }
void NetManager::setAutoSeq(int seq) { _autoSeq = seq; }

void NetManager::sendJsonTo(uint8_t clientNum, const char* json) {
    if (!json) return;
    if (strlen(json) > 512) return;
    if (_currentClient == 255) {
        LOG("[NET] sendJsonTo blocked: no client connected\n");
        return;
    }
    if (clientNum != _currentClient) {
        LOG("[NET] sendJsonTo clientNum mismatch: cmd=%d current=%d (force sending to cmd=%d)\n", clientNum, _currentClient, clientNum);
    }
    char sendBuf[600];
    int len = snprintf(sendBuf, sizeof(sendBuf), "%s\n", json);
    _tcpServer.sendTXT(clientNum, sendBuf, (size_t)len);
    _lastTcpRxMs = millis(); // TX 也算活跃，防止超时断连
}

void NetManager::setCommandCallback(void (*callback)(uint8_t, const char*)) {
    _cmdCallback = callback;
}

void NetManager::_pushWifiIp() {
    if (!_bleServer) return;
    if (!_wifiConnected) {
        _bleIpPushPending = true;
        return;
    }
    char ipJson[64];
    snprintf(ipJson, sizeof(ipJson), "{\"ip\":\"%s\"}", WiFi.localIP().toString().c_str());
    bool ok = _bleServer->updateIpAddress(ipJson);
    if (ok) LOG("[NET] BLE重连 → 已推送IP: %s\n", WiFi.localIP().toString().c_str());
    else LOG("[NET] BLE重连 → IP推送失败\n");
}
