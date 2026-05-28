#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <WebSocketsServer.h>
#include "0_Base/Globals.h"

class BleConfigServer;

class NetManager {
public:
    NetManager();
    void init(BleConfigServer* bleServer);
    void tick();
    void sendData(float rms, float mdf, float fatigue, uint8_t quality, float activation = 0.0f, bool isCalibMode = false, const char* calibPhase = nullptr);

    void sendJsonTo(uint8_t clientNum, const char* json);
    
    void setAutoSeq(int seq);
    void clearAutoSeq() { _autoSeq = -1; }
    void setCommandCallback(void (*callback)(uint8_t, const char*));

    bool isWifiConnected() const;
    bool isTcpClientConnected() const;

    void connectWifi(const char* ssid, const char* pass);
    void disconnectWifi();
    void _pushWifiIp();
    void syncTime();

private:
    bool _tryConnectWifi();
    void _handleNtp();

    BleConfigServer* _bleServer;
    bool _wifiConnected;
    uint32_t _wifiRetryTimer;
    const uint32_t WIFI_RETRY_INTERVAL = 10000;
    bool _eepromCredsTried;
    bool _dhcpWaitDone;
    uint32_t _dhcpWaitStart;
    bool _dhcpGotIp;
    uint32_t _dhcpStableStart;
    bool _bleIpPushPending;
    bool _ntpPending;
    uint32_t _ntpRequestTime;
    uint32_t _lastDisconnectLogMs;
    int _autoSeq;

    unsigned long _ntpBaseMillis;
    unsigned long _ntpBaseSeconds;
    bool _isNtpSynced;
    WiFiUDP _ntpUdp;
    byte _ntpPacketBuffer[48];

    WebSocketsServer _tcpServer;
    bool _tcpStreaming;
    uint8_t _currentClient;
    uint32_t _lastTcpRxMs;
    uint32_t _lastPingMs;
    char _tcpJsonBuf[224];

    void _getTimestamp(uint32_t &sec, uint16_t &ms);
};

#endif
