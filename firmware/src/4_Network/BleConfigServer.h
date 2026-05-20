// BleConfigServer.h
#ifndef BLECONFIGSERVER_H
#define BLECONFIGSERVER_H

#include <ArduinoBLE.h>
#include "0_Base/Globals.h" // 彻底解决依赖溯源问题

#define BLE_IP_CHAR_MAX_LEN 128
#define BLE_DEVICE_ID_CHAR_MAX_LEN 32
#define BLE_WIFI_SSID_MAX_LEN 32
#define BLE_WIFI_PASS_MAX_LEN 64

#define BLE_STATE_DISCONNECTED 0
#define BLE_STATE_CONNECTED 1
#define BLE_STATE_ADVERTISING 2

class BleConfigServer {
public:
    BleConfigServer();
    void init();
    void tick();
    bool hasNewCredentials();
    WifiCredentials_t consumeCredentials();
    
    // 【关键修复】使用与微信小程序完全一致的 19B10000 UUID 系列
    bool updateIpAddress(const char* ipJson);
    void pauseAdvertising();
    void resumeAdvertising();
    void startAdvertising();
    uint8_t getState();
    bool canWriteIp();
    
    // 【新增】动态设置 BLE 广播名称，方便多设备区分
    void setDeviceName(const char* name) {
        BLE.setDeviceName(name);
        BLE.setLocalName(name);
    }

    // 【新增】用于解决 ArduinoBLE 不支持 Lambda 回调的静态函数与实例指针
    static void onSsidWritten(BLEDevice device, BLECharacteristic characteristic);
    static void onPassWritten(BLEDevice device, BLECharacteristic characteristic);
    static BleConfigServer* getInstance() { return _instance; }

private:
    // 【关键修复】恢复 19B10000 UUID，确保小程序能扫描并连接
    BLEService _bleService = BLEService("19b10000-e8f2-537e-4f6c-d104768a1214");
    
    BLEStringCharacteristic _deviceIdChar{"19b10002-e8f2-537e-4f6c-d104768a1214", BLERead | BLENotify, BLE_DEVICE_ID_CHAR_MAX_LEN};
    BLEDescriptor _deviceIdDesc{"2901", "Device ID"};
    
    BLEStringCharacteristic _wifiSsidChar{"19b10001-e8f2-537e-4f6c-d104768a1214", BLEWrite | BLENotify, BLE_WIFI_SSID_MAX_LEN};
    BLEDescriptor _wifiSsidDesc{"2901", "WiFi SSID"};
    
    BLEStringCharacteristic _wifiPassChar{"19b10004-e8f2-537e-4f6c-d104768a1214", BLEWrite | BLENotify, BLE_WIFI_PASS_MAX_LEN};
    BLEDescriptor _wifiPassDesc{"2901", "WiFi Password"};
    
    BLEStringCharacteristic _ipAddressChar{"19b10003-e8f2-537e-4f6c-d104768a1214", BLERead | BLENotify, BLE_IP_CHAR_MAX_LEN};
    BLEDescriptor _ipAddressDesc{"2901", "Device IP Address"};

    char _currentSSID[BLE_WIFI_SSID_MAX_LEN];
    char _currentPASS[BLE_WIFI_PASS_MAX_LEN];
    
    uint8_t _state;
    bool _hasNewCredentials;
    bool _shouldAdvertise;
    bool _deviceConnected;

    // 【新增】静态实例指针，用于在静态回调中访问成员变量
    static BleConfigServer* _instance;
};

#endif
