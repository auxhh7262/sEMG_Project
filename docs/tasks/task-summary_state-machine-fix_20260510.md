# sEMG 协议调试 - 根因定位与修复

## 问题

设备收到 `{"cmd":"start"}` 命令但无响应。

## 根因

**系统状态停留在 `ST_BOOT`，从未转换到 `ST_IDLE`！**

### 代码分析

1. **StateManager::init()** 设置 `_state = ST_BOOT`
2. **main.cpp setup()** 调用 `gState.init()` 但没有调用 `gState.transitionTo(ST_IDLE)`
3. **AppController::onCommandReceived(CMD_START_CALIB)** 只在 `ST_IDLE` 状态下执行：
   ```cpp
   if (st == ST_IDLE) {
       _calibMgr->reset();
       _calibMgr->beginPhase();
       _stateMgr->startCalibPhase(CALIB_REST_SEC);
       _stateMgr->transitionTo(ST_CALIB_REST);
       LOG("[CTRL] Calibration started: REST phase %ds\n", CALIB_REST_SEC);
   }
   ```

4. **状态转换规则**（SystemStateMachine.cpp）：
   ```cpp
   if (from == ST_BOOT) return (to == ST_IDLE || to == ST_MONITORING);
   ```
   - `ST_BOOT` 只能转换到 `ST_IDLE` 或 `ST_MONITORING`
   - 但 `CMD_START_CALIB` 只在 `ST_IDLE` 状态下有效

### 为什么没有错误日志？

`CMD_START_CALIB` 的处理代码：
```cpp
case CMD_START_CALIB: {
    SystemState_t st = _stateMgr->getState();
    if (st == ST_MONITORING) {
        LOG("[CTRL] Already in monitoring, ignore CMD_START_CALIB\n");
        break;
    }
    if (st == ST_ERROR) {
        LOG("[CTRL] Error state, must STOP first\n");
        break;
    }
    if (st == ST_IDLE) {
        // 启动校准...
    }
    break;  // ❌ 如果 st == ST_BOOT，静默跳过！
}
```

**没有处理 `ST_BOOT` 状态的分支，所以静默跳过！**

## 已修复

### 1. main.cpp — 初始化后转换到 IDLE

```cpp
// 注册 JSON 命令回调
gNetManager.setCommandCallback([](uint8_t clientNum, const char* json) {
    gProtocol.handleJsonCommand(clientNum, json);
});

// 初始化完成，进入 IDLE 状态
gState.transitionTo(ST_IDLE);

LOG("[MAIN] V1.0 系统初始化完成\n");
```

### 2. ProtocolHandler.cpp — 添加入口日志

```cpp
void ProtocolHandler::handleJsonCommand(uint8_t clientNum, const char* json) {
    LOG("[PROTO] RX JSON: %s\n", json);
    // ...
    LOG("[PROTO] cmd='%s'\n", cmd);
    // ...
}
```

## 预期日志

设备启动：
```
[MAIN] V1.0 系统初始化完成
[STATE] BOOT -> IDLE
```

发送 `{"cmd":"start"}`：
```
[PROTO] RX JSON: {"cmd":"start"}
[PROTO] cmd='start'
[CTRL] Calibration started: REST phase 10s
[STATE] IDLE -> CALIB_REST
```

## 文件修改

| 文件 | 修改 |
|------|------|
| `src/main.cpp` | 初始化后调用 `gState.transitionTo(ST_IDLE)` |
| `src/0_Base/ProtocolHandler.cpp` | 添加入口日志 |

## 教训

1. **状态机必须有明确的初始状态转换**
2. **命令处理应该覆盖所有状态，或在 default 分支打印警告**
3. **入口日志是调试的关键**
