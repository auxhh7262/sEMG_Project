# sEMG Project

表面肌电信号(sEMG)采集与分析系统

## 项目结构

```
sEMG_Project/
├── firmware/           # 固件代码 (Arduino UNO R4 WiFi)
├── mini_program/       # 微信小程序
├── docs/              # 项目文档
├── logs/              # 日志文件
├── skills/            # OpenClaw Skills
└── temp/              # 临时文件
```

## 功能特性

- 📊 实时采集 sEMG 信号
- 💾 Flash 存储 (W25Q128FV, 16MB)
- 📶 WiFi 通信 (WebSocket)
- 📱 微信小程序监控
- 🔋 低功耗设计

## 硬件配置

| 模块 | 型号 |
|------|------|
| 主控 | Arduino UNO R4 WiFi (RA4M1) |
| 存储 | W25Q128FV (16MB Flash) |
| 传感器 | AD8232 sEMG 传感器 |
| 电平转换 | BSS138 模块 |

## 快速开始

### 固件上传
说 "**上传固件**" → 自动编译 + 上传 + 启动串口监控

### 小程序编译
说 "**编译小程序**" → 自动编译 + 生成预览码

### 日志分析
说 "**分析固件**" 或 "**分析小程序**" → 自动分析日志

---

## 版本历史

- **2026-05-30**: Skills 验证完成 (5/5) ✅
- **2026-05-29**: 项目结构优化，日志系统重构
- **2026-05-16**: 项目初始化

---

*Last updated: 2026-05-30 21:47*
*Git-push skill 验证中...* 🚀
