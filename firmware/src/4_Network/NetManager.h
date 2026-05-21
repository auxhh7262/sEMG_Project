#ifndef NET_MANAGER_H
#define NET_MANAGER_H

#include <Arduino.h>
#include <WiFiS3.h>
#include <WiFiUdp.h>
#include <WebSocketsServer.h>
#include "0_Base/Globals.h"

// 前向声明，减少头文件依赖
class BleConfigServer;

class NetManager {
public:
    NetManager();

    void init(BleConfigServer* bleServer);
    void tick();
    void sendData(float rms, float mdf, float fatigue, uint8_t quality, float activation = 0.0f, bool isCalibMode = false, const char* calibPhase = nullptr);

    // ---- JSON 命令接口 ----
    void sendJsonTo(uint8_t clientNum, const char* json);
    void sendJsonTo(uint8_t clientNum, const char* json, int seq);  // [v3.9.14] 带seq重载
    void setAutoSeq(int seq);       // [v3.9.14] 设置自动seq（命令响应自动回传）
    void clearAutoSeq() { _autoSeq = -1; }  // 清除自动seq（push消息用）
    void setCommandCallback(void (*callback)(uint8_t, const char*));

    bool isWifiConnected() const;
    bool isTcpClientConnected() const;

    void connectWifi(const char* ssid, const char* pass);
    void disconnectWifi();
    void syncTime();

private:
    bool _tryConnectWifi();
    void _handleNtp();

    BleConfigServer* _bleServer;

    bool _wifiConnected;
    uint32_t _wifiRetryTimer;
    const uint32_t WIFI_RETRY_INTERVAL = 10000;
    bool _eepromCredsTried;  // [B1-1-fix] 标记是否已尝试EEPROM凭证
    bool _dhcpWaitDone;          // DHCP等待是否完成
    uint32_t _dhcpWaitStart;     // DHCP等待开始时间
    bool _dhcpGotIp;             // DHCP是否成功获取IP
    bool _ntpPending;              // NTP请求已发送，等待响应
    uint32_t _ntpRequestTime;      // NTP请求发送时间（超时检测+重试间隔）
    uint32_t _lastDisconnectLogMs; // 上次TCP断连日志时间（限频）

    int _autoSeq;  // [v3.9.14] 自动seq回传（-1=禁用）

    unsigned long _ntpBaseMillis;
    unsigned long _ntpBaseSeconds;
    bool _isNtpSynced;
    WiFiUDP _ntpUdp;
    byte _ntpPacketBuffer[48];

    WebSocketsServer _tcpServer;
    bool _tcpStreaming;
    uint8_t _currentClient;  // 当前连接的客户端编号
    char _tcpJsonBuf[192];  // [B1-4-fix] 128→192，防止JSON截断

    void _getTimestamp(uint32_t &sec, uint16_t &ms);
};

#endif // NET_MANAGER_H
