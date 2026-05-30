---
name: miniprogram-preview
description: 小程序编译预览+日志采集工具。当用户说"编译小程序"、"预览小程序"、"生成预览码"、"开小程序日志"、"启动日志服务器"时触发此skill。
---

# emg-miniprogram-preview

小程序编译预览 + 日志采集skill。调用微信开发者工具CLI编译小程序,同时提供日志采集服务器。

## 功能

### 1. 编译预览(preview.py)
调用微信开发者工具CLI,编译小程序并生成预览二维码。

### 2. 日志采集(mini_log_server.py)
监听HTTP 9876端口,接收小程序 `wx.request()` 发来的日志,**tkinter GUI实时显示**,同时写入日志文件。与串口监控风格统一(深色终端主题)。

## 使用流程

```
用户:说"编译小程序" / "预览"
  → 关闭旧日志服务器(杀 mini_log_server.py 旧进程)
  → 检测代码改动(30分钟内)
  → 调用微信开发者工具CLI编译
  → 预览热推送到手机(无需扫码)
  → 启动新日志服务器(GUI窗口,端口9876)
  → 用户操作小程序,日志实时采集

用户：说"开小程序日志" / "开日志"
  → 【推荐】说"编译小程序"（会自动启动日志服务器，且只显示GUI窗口）
  → 【或直接】启动 mini_log_server.py（注意：会弹出GUI + Python控制台两个窗口）
  → 监听 0.0.0.0:9876
  → 小程序日志实时显示 + 写入 logs/mini/
```

## 脚本说明

| 脚本 | 作用 |
|------|------|
| `preview.py` | 编译小程序 + 生成预览码 |
| `mini_log_server.py` | HTTP日志采集服务器 + tkinter GUI(端口9876) |

## 路径配置

| 配置项 | 值 |
|--------|-----|
| 小程序目录 | `E:\Personal\sEMG_Project\mini_program` |
| CLI路径 | `D:\Program Files\微信web开发者工具\cli.bat` |
| 日志目录 | `E:\Personal\sEMG_Project\logs\mini` |
| 日志文件格式 | `mini_log_YYYYMMDD_HHMMSS.txt` |
| 日志端口 | 9876 |

## 注意事项

- 微信开发者工具需要提前安装
- 日志服务器会过滤关键词:LogForward, heartbeat, ping, pong
- 日志文件每次启动独立文件,不会覆盖旧日志
