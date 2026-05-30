---
name: miniprogram-debug
description: 小程序日志分析工具。当用户说"分析小程序"、"小程序debug"、"分析小程序日志"时触发此skill。读取mini_log_server.py生成的日志，自动扫描常见问题模式并输出结构化报告。
---

# emg-miniprogram-debug

小程序日志分析skill。读取日志文件，扫描常见问题模式，输出结构化报告。

## 设计原则

- **不重复造轮子**：与 `emg-miniprogram-preview` 通过文件系统协作
- `emg-miniprogram-preview/mini_log_server.py`：采集小程序日志 → 写入 `logs/mini/`
- `emg-miniprogram-debug/analyze_mini_log.py`：读日志 → 分析 → 输出报告
- **日志规则与固件端一致**：统一放 `logs/mini/`，最新文件优先

## 使用流程

```
1. 用户：说"开小程序日志"  →  miniprogram-preview 启动日志服务器
2. 用户：操作小程序，产生日志 → 日志写入 logs/mini/
3. 用户：说"分析小程序"     →  miniprogram-debug 读日志，输出报告
```

## 检测的问题模式

| 严重度 | 问题类型 | 关键词 |
|--------|----------|--------|
| 🔴 严重 | 连接失败 | `connect fail`, `WiFi error` |
| 🔴 高 | 命令超时 | `CMD_TIMEOUT`, `timeout` |
| 🔴 高 | JSON解析失败 | `parse error`, `JSON error` |
| 🔴 高 | WebSocket断连 | `disconnect`, `close` |
| 🟡 中 | 状态异常 | `state error`, `invalid state` |
| 🟡 中 | 数据异常 | `data error`, `RMS/MDF 异常` |
| 🟢 正常 | 连接成功 | `connect success`, `ready` |
| 🟢 正常 | 命令成功 | `CMD success`, `ACK` |

## 输出示例

```
============================================================
  小程序日志分析报告
============================================================
  文件: mini_log_20260529_080100.txt
  报告时间: 2026-05-28 23:33:00
  总行数: 156

  🔴 高 [HIGH] (1 项)
    1. 命令超时 (CMD_TIMEOUT)
       日志: start_calib timeout after 5000ms
       建议: 检查固件是否正常响应

  🟡 中等 [MED] (1 项)
    1. 状态异常
       日志: invalid state transition: IDLE -> STREAMING
       建议: 检查状态机逻辑
============================================================
```

## 路径配置

| 配置项 | 值 |
|--------|-----|
| 日志目录 | `E:\Personal\sEMG_Project\logs\mini` |
| 日志文件格式 | `mini_log_YYYYMMDD_HHMMSS.txt` |
| 分析脚本 | `analyze_mini_log.py` |

## 使用方法

运行 `analyze_mini_log.py` 即可分析最新日志文件。
