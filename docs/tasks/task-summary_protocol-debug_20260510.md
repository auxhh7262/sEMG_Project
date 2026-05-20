# sEMG 协议调试 - 设备未收到 WebSocket 消息

## 问题

小程序发送 `{"cmd":"start"}` 到设备，日志显示发送成功，但设备串口没有任何响应。

## 已排查

### 1. 协议确认 ✅

固件支持 `{"cmd":"start"}` 命令（`ProtocolHandler.cpp:213`）：
```cpp
else if (strcmp(cmd, "start") == 0) {
    gAppController.onCommandReceived(CMD_START_CALIB);
}
```

### 2. WebSocket 连接确认 ✅

- 小程序日志：`[TCP] onOpen — WebSocket 连接成功`
- 固件日志：`[TCP] Client X connected`

### 3. 发送确认 ✅

小程序日志：
```
[TCP] send() — 发送: {"cmd":"start"}
[calibrate] start 命令发送结果: 成功
```

### 4. 固件端问题定位

**`ProtocolHandler::handleJsonCommand()` 没有入口日志！**

原代码：
```cpp
void ProtocolHandler::handleJsonCommand(uint8_t clientNum, const char* json) {
    StaticJsonDocument<512> doc;
    DeserializationError err = deserializeJson(doc, json);
    
    if (err) {
        LOG("[PROTO] JSON parse error: %s\n", err.c_str());
        return;
    }
    
    const char* cmd = doc["cmd"];
    if (!cmd) return;  // ❌ 静默返回，没有日志！
    ...
}
```

## 已修复

在 `ProtocolHandler.cpp` 添加入口日志：
```cpp
void ProtocolHandler::handleJsonCommand(uint8_t clientNum, const char* json) {
    LOG("[PROTO] RX JSON: %s\n", json);  // ✅ 新增
    ...
    const char* cmd = doc["cmd"];
    if (!cmd) {
        LOG("[PROTO] No cmd field\n");  // ✅ 新增
        return;
    }
    LOG("[PROTO] cmd='%s'\n", cmd);  // ✅ 新增
    ...
}
```

## 下一步

1. 重新编译并上传固件
2. 测试校准流程
3. 观察设备串口日志：
   - 应显示 `[PROTO] RX JSON: {"cmd":"start"}`
   - 应显示 `[PROTO] cmd='start'`
   - 应显示 `[CTRL] Calibration started: REST phase 10s`
4. 如果还是没有日志，检查 WebSocket 库是否正确处理消息

## 可能的根因

1. **WebSocket 库问题**：设备端的 WebSockets 库可能没有正确解析消息
2. **消息格式问题**：小程序发送的格式可能与库期望的不一致
3. **缓冲区问题**：消息可能被截断或缓冲区溢出

## 文件修改

- `E:\Personal\sEMG\sEMG_Firmware_V1.4\src\0_Base\ProtocolHandler.cpp` — 添加入口日志
- `E:\Personal\sEMG\code\0421\utils\tcpClient.js` — 开启 send 日志
- `E:\Personal\sEMG\code\0421\pages\calibrate\index.js` — 添加发送确认日志
- `E:\Personal\sEMG\code\0421\app.json` — 启动页改为网络配置
