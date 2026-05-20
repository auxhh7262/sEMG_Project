# sEMG 校准数据流 TCP 推送修复

## 日期
2026-05-10 14:45 ~ 15:00

## 问题
小程序发送 `{"cmd":"start"}` 后，校准阶段（CALIB_REST/CALIB_MAX）没有收到任何数据推送。

## 根因分析

### 根因 1：WebSocket 多客户端断开问题 ✅ 已修复
固件 `NetManager.cpp` WebSocket 事件处理：
```cpp
case WStype_DISCONNECTED:
    _tcpStreaming = false;  // ← 任何客户端断开都会关闭 streaming！
```

**修复：** 只有当前活跃客户端断开才关闭 streaming。

### 根因 2：采样率理解错误 ❌ 排除
最初以为 10Hz 采样，256 点需要 25.6 秒。
**实际是 1kHz 采样**，256 点只需 0.256 秒。

### 当前状态
代码已回退到使用 `update()`，但问题仍未解决。

## 待验证
重新编译上传后，观察串口日志：
- `[TCP] Client X connected` — 确认连接
- `[TCP] sendData skipped: not streaming` — streaming 被关闭
- `[TCP] TX: ...` — 数据推送成功

## 文件修改
- `NetManager.cpp` — DISCONNECTED 事件处理修复

## 状态
⏳ 待验证
