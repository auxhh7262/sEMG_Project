// BleConfigServer.cpp
#include "BleConfigServer.h"
#include "0_Base/Logger.h"
#include <WiFiS3.h>

// 静态成员初始化
BleConfigServer* BleConfigServer::_instance = nullptr;
BleConfigServer::WiFiInfoCallback_t BleConfigServer::_wifiInfoCb = nullptr;

BleConfigServer::BleConfigServer() 
    : _state(BLE_STATE_DISCONNECTED)
    , _hasNewCredentials(false)
    , _shouldAdvertise(true)
    , _deviceConnected(false) {
    strcpy(_currentSSID, "");
    strcpy(_currentPASS, "");
    _instance = this;
}

void BleConfigServer::init() {
    LOG("[BLE] 初始化BLE服务器...\n");
    if (!BLE.begin()) {
        LOG("[BLE] ❌ 初始化失败\n");
        return;
    }

    BLE.setDeviceName("sEMG_Monitor");
    BLE.setLocalName("sEMG_0000"); 
    BLE.setAdvertisedServiceUuid("19b10000-e8f2-537e-4f6c-d104768a1214");
    BLE.setAdvertisedService(_bleService);

    _wifiSsidChar.addDescriptor(_wifiSsidDesc);
    _wifiSsidChar.setValue("");
    _bleService.addCharacteristic(_wifiSsidChar);

    _wifiPassChar.addDescriptor(_wifiPassDesc);
    _wifiPassChar.setValue("");
    _bleService.addCharacteristic(_wifiPassChar);

    _ipAddressChar.addDescriptor(_ipAddressDesc);
    _ipAddressChar.setValue("0.0.0.0");
    _bleService.addCharacteristic(_ipAddressChar);

    BLE.addService(_bleService);

    BLE.setEventHandler(BLEConnected, [](BLEDevice device) {
        LOG("[BLE] 设备已连接: %s\n", device.address().c_str());
        // BLE重连后主动推送当前WiFi IP（延迟500ms确保BLE连接稳定）
        delay(500);
        if (_instance) {
            _instance->_onDeviceConnected();
        }
    });
    BLE.setEventHandler(BLEDisconnected, [](BLEDevice device) {
        LOG("[BLE] 设备已断开\n");
    });

    _wifiSsidChar.setEventHandler(BLEWritten, onSsidWritten);
    _wifiPassChar.setEventHandler(BLEWritten, onPassWritten);

    BLE.advertise();
    _shouldAdvertise = true;
    LOG("[BLE] ✅ BLE服务器初始化完成\n");
}

void BleConfigServer::onSsidWritten(BLEDevice device, BLECharacteristic characteristic) {
    BleConfigServer* instance = getInstance();
    if (!instance) return;
    
    String value = instance->_wifiSsidChar.value();
    LOG("[BLE] 收到SSID: %s\n", value.c_str());
    strncpy(instance->_currentSSID, value.c_str(), sizeof(instance->_currentSSID) - 1);
    instance->_currentSSID[sizeof(instance->_currentSSID) - 1] = '\0';
}

void BleConfigServer::onPassWritten(BLEDevice device, BLECharacteristic characteristic) {
    BleConfigServer* instance = getInstance();
    if (!instance) return;
    
    String value = instance->_wifiPassChar.value();
    LOG("[BLE] 收到PASS: %s\n", value.length() > 0 ? "[隐藏]" : "[空]");
    strncpy(instance->_currentPASS, value.c_str(), sizeof(instance->_currentPASS) - 1);
    instance->_currentPASS[sizeof(instance->_currentPASS) - 1] = '\0';
    
    if (strlen(instance->_currentSSID) > 0 && strlen(instance->_currentPASS) > 0) {
        LOG("[BLE] ✅ 收到完整WiFi凭证\n");
        instance->_hasNewCredentials = true;
        instance->_shouldAdvertise = false;
        BLE.stopAdvertise();
    }
}

void BleConfigServer::tick() {
    BLE.poll();
    if (_deviceConnected != BLE.connected()) {
        _deviceConnected = BLE.connected();
        _state = _deviceConnected ? BLE_STATE_CONNECTED : BLE_STATE_DISCONNECTED;
        LOG("[BLE] 状态变化: %s\n", _deviceConnected ? "已连接" : "已断开");
        // [B0-1-fix] 设备断开后，如果需要广播则重启广播
        // BLE.advertise() 是动作函数(开始广播)，不是查询函数
        if (!_deviceConnected && _shouldAdvertise) {
            BLE.advertise();
            LOG("[BLE] 设备断开，重新开始广告\n");
        }
    }

    // 检测 central 刚订阅 IP 通知（用 static 跟踪状态变化）
    static bool wasSubscribed = false;
    bool isSubscribed = _ipAddressChar.subscribed();
    if (_deviceConnected && isSubscribed && !wasSubscribed) {
        LOG("[BLE] 检测到IP通知订阅，主动推送IP...\r\n");
        triggerWifiInfoCb();
    }
    wasSubscribed = isSubscribed;
}

bool BleConfigServer::hasNewCredentials() {
    bool result = _hasNewCredentials;
    if (result) {
        LOG("[BLE] 有新WiFi凭证可用: SSID='%s'\n", _currentSSID);
    }
    return result;
}

WifiCredentials_t BleConfigServer::consumeCredentials() {
    WifiCredentials_t creds;
    creds.isValid = false;
    if (_hasNewCredentials) {
        strncpy(creds.ssid, _currentSSID, sizeof(creds.ssid) - 1);
        strncpy(creds.pass, _currentPASS, sizeof(creds.pass) - 1);
        creds.ssid[sizeof(creds.ssid) - 1] = '\0';
        creds.pass[sizeof(creds.pass) - 1] = '\0';
        creds.isValid = true;
        LOG("[BLE] 消费凭证: SSID='%s'\n", creds.ssid);
        _hasNewCredentials = false;
        memset(_currentSSID, 0, sizeof(_currentSSID));
        memset(_currentPASS, 0, sizeof(_currentPASS));
    }
    return creds;
}

bool BleConfigServer::updateIpAddress(const char* ipJson) {
    if (!_deviceConnected) {
        return false;
    }
    _ipAddressChar.setValue(ipJson);
    // BLE.poll() 在 tick() 中调用，会自动发送通知（如果 central 已订阅）
    if (_ipAddressChar.subscribed()) {
        LOG("[BLE] ✅ IP已更新，通知将发出: %s\n", ipJson);
    } else {
        LOG("[BLE] ℹ️ IP已更新（central未订阅，等订阅后可READ）: %s\n", ipJson);
    }
    return true;
}

// [B0-1-fix] pauseAdvertising: BLE.advertise() 是"开始广播"的动作函数，不是查询
// ArduinoBLE 没有 isAdvertising() API，需自行维护 _shouldAdvertise 标志
void BleConfigServer::pauseAdvertising() {
    BLE.stopAdvertise();
    _shouldAdvertise = false;
}

void BleConfigServer::resumeAdvertising() {
    if (_shouldAdvertise) {
        BLE.advertise();
    }
}

void BleConfigServer::startAdvertising() {
    BLE.advertise();
    _shouldAdvertise = true;
}

void BleConfigServer::_onDeviceConnected() {
    LOG("[BLE] 设备连接，触发WiFi信息推送\n");
    triggerWifiInfoCb();
}

// [B0-2-fix] getState: 不再误用 BLE.advertise()
// ArduinoBLE 无法查询是否正在广播，只能通过 _shouldAdvertise 标志推断
uint8_t BleConfigServer::getState() {
    if (BLE.connected()) {
        return BLE_STATE_CONNECTED;
    }
    if (_shouldAdvertise) {
        return BLE_STATE_ADVERTISING;
    }
    return BLE_STATE_DISCONNECTED;
}

bool BleConfigServer::canWriteIp() {
    return BLE.connected();
}
