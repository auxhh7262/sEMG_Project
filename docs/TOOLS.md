# TOOLS.md - 调试与工具速查

---

## 调试快捷指令

| 指令 | 动作 |
|------|------|
| **分析** | 启动日志 → 上传固件/编译小程序 → 读日志 → 修复 → 循环 |
| **开日志** | 启动串口监控 + 小程序日志服务器 |
| **关日志** | 关闭两个日志窗口 |
| **看日志** | 读取 serial_log.txt + mini_log.txt |
| **上传** | 编译上传固件 + 启动串口监控 |

> 说指令即可，无需手动运行bat

---

## 项目路径

| 用途 | 路径 |
|------|------|
| 项目根 | `E:\Personal\sEMG_Project\` |
| 日志 | `E:\Personal\sEMG_Project\logs\` |
| 文档 | `E:\Personal\sEMG_Project\docs\` |
| 固件 | `E:\Personal\sEMG_Project\firmware` |
| 小程序 | `E:\Personal\sEMG_Project\mini_program` |

---

## 日志脚本与工具

| 脚本 | 功能 |
|------|------|
| `logs/start_logs.bat` | 开双日志 |
| `logs/upload_and_log.bat` | 上传固件+监控 |
| `logs/serial_monitor.py` | COM4→serial_log.txt |
| `logs/mini_log_server.py` | HTTP :9876→mini_log.txt |
| 微信CLI | `cli.bat auto-preview --project` + 小程序路径 |

> 微信CLI路径：`D:\Program Files\微信web开发者工具\cli.bat`
>
> 小程序日志：logger.js 直接 wx.request POST → :9876 → mini_log.txt（`LOG_ENABLED=true` 转发，发布前改 `false`）

---

## 设备配置

| 项目 | 值 |
|------|-----|
| WebSocket | 端口 8888（设备IP动态DHCP） |
| 串口 | COM4 / 115200 |
| pio | `C:\Users\honghuang\.platformio\penv\Scripts\pio.exe` |

---

## UNO R4 WiFi 注意事项

- 1200bps open/close = 进 bootloader，非软复位
- 开机信息靠固件 `delay(3000)` + 脚本0延迟连接
- COM4 被脚本占用时 PlatformIO 无法同时监控
