# miniprogram-preview 使用说明

小程序编译预览 + 日志采集工具。

---

## 📋 触发词

- "编译小程序"
- "预览小程序"
- "生成预览码"
- "开小程序日志"
- "启动日志服务器"

---

## 🚀 功能说明

### 1️⃣ 编译预览（preview.py）

**功能：**
1. 检测小程序代码改动（30 分钟内）
2. 调用微信开发者工具 CLI 编译小程序
3. 生成预览二维码（推送到手机，无需扫码）
4. 启动日志服务器 GUI（端口 9876）

**执行命令：**
```powershell
& "D:\Program Files\微信web开发者工具\cli.bat" auto-preview --project E:\Personal\sEMG_Project\mini_program
```

---

### 2️⃣ 日志采集（mini_log_server.py）

**功能：**
- 监听 HTTP 9876 端口，接收小程序 `wx.request()` 发来的日志
- **tkinter GUI 实时显示**（深色终端主题，与串口监控风格统一）
- 同时写入日志文件（`logs/mini/mini_log_YYYYMMDD_HHMMSS.txt`）

**日志来源：**
```
小程序 log/warn/error (logger.js)
  → 直接 wx.request POST（主通道，已验证稳定）
  → 同时 console.log → app.js拦截 → _forwardLog（备用，缓冲机制Tab切换时丢日志）
  → http://192.168.137.1:9876/log
  → mini_log_server.py → mini_log.txt + 控制台
```

---

## 📦 使用方式

### 方式1：语音指令（推荐）

```
说："编译小程序" 或 "预览小程序"
→ 自动执行完整流程
```

---

### 方式2：命令行

```powershell
# 完整流程（编译 + 启动日志服务器）
python "E:\Personal\sEMG_Project\skills\miniprogram-preview\preview.py"

# 只启动日志服务器（不编译）
python "E:\Personal\sEMG_Project\skills\miniprogram-preview\mini_log_server.py"
```

---

## 📊 输出示例

### 日志服务器 GUI

```
[22:50:24 GMT+0800 (CST)] [LOG] App onLaunch
[22:50:25 GMT+0800 (CST)] [LOG] WiFi connected, IP: 192.168.137.234
[22:50:26 GMT+0800 (CST)] [LOG] WebSocket connected
[22:50:27 GMT+0800 (CST)] [WARN] start_calib REST failed: CMD_TIMEOUT
=========================
```

### 日志文件

**路径：** `E:\Personal\sEMG_Project\logs\mini\mini_log_20260530_225000.txt`

**内容：** 与 GUI 显示一致，纯文本格式，可用 VS Code 查看。

---

## ⚙️ 配置说明

### 路径配置

| 配置项 | 值 |
|--------|-----|
| 小程序目录 | `E:\Personal\sEMG_Project\mini_program` |
| CLI路径 | `D:\Program Files\微信web开发者工具\cli.bat` |
| 日志目录 | `E:\Personal\sEMG_Project\logs\mini` |
| 日志文件格式 | `mini_log_YYYYMMDD_HHMMSS.txt` |
| 日志端口 | 9876 |

### 小程序端配置

**文件：** `mini_program/utils/logger.js`

**关键代码：**
```javascript
// 直接 wx.request（主通道）
wx.request({
  url: 'http://192.168.137.1:9876/log',
  method: 'POST',
  data: { level: 'LOG', msg: message }
})

// 备用：console.log → app.js拦截 → _forwardLog
// 注意：Tab切换时可能丢日志，不推荐
```

**开关：**
- `LOG_ENABLED = true`：转发日志
- `LOG_ENABLED = false`：不转发（发布前改为 false）

---

## 🚨 常见问题

### 1. 编译失败（CLI 未找到）

**现象：**
```
'D:\Program Files\微信web开发者工具\cli.bat' is not recognized as an internal or external command
```

**原因：** 微信开发者工具未安装或 CLI 路径错误。

**解决：**
- 安装微信开发者工具
- 检查 CLI 路径：`D:\Program Files\微信web开发者工具\cli.bat`

---

### 2. 日志服务器启动失败（端口占用）

**现象：**
```
OSError: [WinError 10048] Only one usage of each socket address is normally permitted
```

**原因：** 另一个 `mini_log_server.py` 进程已占用 9876 端口。

**解决：**
- 说 "编译小程序"，Skill 会自动杀旧进程
- 或手动关闭日志服务器 GUI

---

### 3. 小程序日志未显示

**现象：** 日志服务器 GUI 没有显示小程序日志。

**原因：**
- 小程序未调用 `logger.log()`
- 小程序 WiFi 未连接到同一网段（手机 WiFi IP 应为 192.168.137.x）
- 日志服务器 IP 地址错误（应为 192.168.137.1）

**解决：**
- 检查小程序代码，确保调用 `logger.log()`
- 检查手机 WiFi 连接（应为 LT02 热点）
- 检查日志服务器 IP 地址（`mini_program/utils/logger.js` 中的 `LOG_SERVER_IP`）

---

### 4. 日志文件 GBK 编码报错（PowerShell）

**现象：**
```
UnicodeEncodeError: 'gbk' codec can't encode characters
```

**原因：** Windows PowerShell 默认编码是 GBK，无法显示 Unicode 表情符号。

**解决：**
- Skill 已修复（纯 ASCII 输出，无 Unicode 表情）
- 或用 VS Code 查看日志文件（自动识别编码）

---

## 📚 详细文档

- **Skill 体系概览：** [`../README.md`](../README.md)
- **小程序日志分析：** [`../miniprogram-debug/USAGE.md`](../miniprogram-debug/USAGE.md)
- **组合工作流：** [`../semg-workflow/USAGE.md`](../semg-workflow/USAGE.md)

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

**新的想法？**
- 在 GitHub Issues 提出你的想法
- 或参考 [`../../docs/SKILL_GUIDE.md`](../../docs/SKILL_GUIDE.md) 自己创建

---

## 📄 许可证

MIT License
