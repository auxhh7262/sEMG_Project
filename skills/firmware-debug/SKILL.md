---
name: firmware-debug
description: sEMG固件日志分析工具。当用户说"分析"、"分析日志"、"debug"、"固件调试"时触发此skill。读取emg-firmware-upload生成的串口日志，自动扫描常见问题模式并输出结构化报告。无需启动日志，直接读取logs/serial/下的日志文件。
---

# emg-firmware-debug

sEMG固件日志分析skill。读取 `emg-firmware-upload` 生成的串口日志，扫描常见问题模式，输出结构化报告。

## 设计原则

- **不重复造轮子**：与 `emg-firmware-upload` 通过文件系统协作
- `emg-firmware-upload`：上传固件 → 写日志到 `logs/serial/`
- `emg-firmware-debug`：读日志 → 分析 → 输出报告

## 使用流程

```
1. 用户：说"上传"  →  emg-firmware-upload 运行，日志写入 logs/serial/
2. 用户：说"分析"  →  emg-firmware-debug 读日志，输出报告
```

## 检测的问题模式

| 严重度 | 问题类型 | 关键词 |
|--------|----------|--------|
| 🔴 严重 | 固件崩溃 | `Usage Fault`, `HardFault`, `ASSERT` |
| 🔴 高 | 命令超时 | `CMD_TIMEOUT` |
| 🔴 高 | JSON解析失败 | `deserializeJson failed`, `parse error` |
| 🔴 高 | 状态机非法转换 | `invalid transition`, `transitionTo` |
| 🔴 高 | WebSocket断连 | `TCP disconnected`, `client disconnect` |
| 🟡 中 | 校准验证失败 | `validation failed` |
| 🟡 中 | JSON序列化失败 | `serializeJson failed` |
| 🟡 中 | Flash操作失败 | `Flash error`, `SPI fail` |
| 🟡 中 | 数据异常 | RMS/MDF 超出合理范围 |
| 🟢 正常 | 阶段完成 | `REST done`, `MAX done`, `calibration PASS` |

## 输出示例

```
============================================================
  sEMG 固件日志分析报告
============================================================
  文件: serial_log_20260528_210500.txt
  日志时间: 2026-05-28 21:05:38
  报告时间: 2026-05-28 22:19:00
  总行数: 342

  🔴 高 [HIGH] (1 项)
    1. 命令超时 (CMD_TIMEOUT)
       日志: start_calib MAX failed: CMD_TIMEOUT
       建议: 小程序等待固件ACK超时，固件未响应

  🟡 中等 [MED] (1 项)
    1. 数据异常
       日志: RMS=8.9 超出合理范围 (0.01~10000 mV)
       建议: 检查电极接触或放大电路

  🟢 正常 [LOW] (3 项)
    1. REST阶段完成
       日志: REST done - SAFE checkpoint
       建议: REST阶段正常完成

============================================================
```

## 路径配置

| 配置项 | 值 |
|--------|-----|
| 日志目录 | `E:\Personal\sEMG_Project\logs\serial` |
| 分析脚本 | `analyze_log.py` |

## 使用方法

运行 `analyze_log.py` 即可分析最新日志文件。
