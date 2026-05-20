# sEMG 校准数据推送修复

## 问题

小程序发送 `{"cmd":"start"}` 后，校准流程启动（日志显示 `[CTRL] Calibration started`），但小程序没有收到任何数据。

## 根因

**`_handleCalibRestState()` 和 `_handleCalibMaxState()` 只采集数据，不推送数据到 TCP！**

### 代码对比

| 状态 | 采集数据 | 推送数据 |
|------|----------|----------|
| `ST_MONITORING` | ✅ `_signalProc->update()` | ✅ `_netMgr->sendData()` |
| `ST_DB_FEATURE` | ✅ `_signalProc->update()` | ✅ `_netMgr->sendData()` |
| `ST_CALIB_REST` | ✅ `_signalProc->update()` | ❌ **缺失** |
| `ST_CALIB_MAX` | ✅ `_signalProc->update()` | ❌ **缺失** |

## 已修复

在 `_handleCalibRestState()` 和 `_handleCalibMaxState()` 中添加：

```cpp
// 推送实时数据到小程序（让用户看到校准进度）
if (rms > 0.0f) {
    _netMgr->sendData(rms, mdf, 0.0f, 0, 0.0f);  // 疲劳度/质量/激活度暂为0
}
```

## 数据格式

小程序会收到 JSON 格式：
```json
{"ts":1778394558000,"r":12.34,"m":85.6,"f":0.0,"q":0,"a":0.0}
```

- `ts`: 时间戳（毫秒）
- `r`: RMS（mV）
- `m`: MDF（Hz）
- `f`: 疲劳度（校准阶段为0）
- `q`: 信号质量（校准阶段为0）
- `a`: 激活度（校准阶段为0）

## 文件修改

| 文件 | 修改内容 |
|------|----------|
| `src/5_AppController/AppController.cpp` | `_handleCalibRestState()` 添加 `sendData()` |
| `src/5_AppController/AppController.cpp` | `_handleCalibMaxState()` 添加 `sendData()` |

## 预期结果

重新编译上传后，小程序应该在校准阶段收到实时数据推送，RMS 和 MDF 曲线可以绘制。
