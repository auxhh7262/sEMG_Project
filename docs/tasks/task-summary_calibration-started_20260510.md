# sEMG 校准流程调试 - 状态机修复验证

## 时间

2026-05-10 14:30

## 测试结果

✅ **状态机修复成功**

### 日志验证

```
[STATE] BOOT -> IDLE           ✅ 初始化后正确转换到 IDLE
[CTRL] Calibration started: REST phase 10s   ✅ 校准命令正常处理
[STATE] IDLE -> CALIB_REST    ✅ 进入静息态采集阶段
[CALIB] Phase sampling started (RMS + MDF)   ✅ 数据采集开始
```

### 问题根因回顾

- **原因**：`main.cpp` 初始化后没有调用 `gState.transitionTo(ST_IDLE)`，系统停留在 `ST_BOOT`
- **修复**：在初始化完成后添加状态转换
- **结果**：校准命令现在可以正常执行

### 流程验证

1. 设备启动 → `ST_BOOT` → `ST_IDLE` ✅
2. TCP 连接建立 ✅
3. 握手命令处理 ✅
4. `{"cmd":"start"}` 命令触发校准 ✅
5. 进入 `CALIB_REST` 阶段 ✅

### 待确认

- 小程序是否收到实时数据推送？
- 10 秒后是否进入 `CALIB_MAX` 阶段？
- 完整校准流程是否正常完成？

## 文件修改

| 文件 | 修改内容 |
|------|----------|
| `src/main.cpp` | 添加 `gState.transitionTo(ST_IDLE)` |
| `src/0_Base/ProtocolHandler.cpp` | 添加入口日志 |

## 状态

**校准流程已启动，等待数据推送验证**
