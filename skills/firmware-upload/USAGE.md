# firmware-upload 使用说明

固件编译上传 + 串口监控 GUI 工具。

---

## 📋 触发词

- "上传固件"
- "编译上传"
- "烧录固件"
- "upload firmware"
- "刷固件"
- "重新烧录"

---

## 🚀 功能说明

### 1️⃣ 自动杀 COM4 占用进程

**问题：** 串口监控 GUI 占用 COM4，PlatformIO 无法同时上传。

**解决：** Skill 自动杀掉以下进程：
- `python.exe`（匹配 CommandLine 包含 `serial_monitor.py`）
- `platformio.exe`
- `pio.exe`

---

### 2️⃣ 编译上传固件

**执行命令：**
```powershell
cd E:\Personal\sEMG_Project\firmware
pio run -t upload --upload-port COM4
```

**成功：** 自动启动串口监控 GUI  
**失败：** 提示错误，不启动监控

---

### 3️⃣ 启动串口监控 GUI

**脚本：** `serial_monitor.py`（tkinter GUI）

**功能：**
- 实时显示：绿色终端风格界面，实时输出固件日志
- 精准关闭旧窗口：按脚本名匹配，只杀同名旧进程
- 时间戳文件名：每次运行生成独立 log 文件
  - 格式：`logs/serial/serial_log_YYYYMMDD_HHMMSS.txt`
- 颜色标签：
  - 绿色 = INFO / 正常日志
  - 红色 = ERROR / 异常
  - 蓝色 = BOOT / 系统启动
  - 灰色 = DATA / 原始数据
- 控制栏：📂 打开日志目录按钮

---

## 📦 使用方式

### 方式1：语音指令（推荐）

```
说："上传固件" 或 "编译上传"
→ 自动执行完整流程
```

---

### 方式2：命令行

```powershell
# 完整流程（杀进程 → 上传 → 启动 GUI）
python "E:\Personal\sEMG_Project\skills\firmware-upload\upload_and_monitor.py"

# 只启动串口监控 GUI（不上传）
python "E:\Personal\sEMG_Project\skills\firmware-upload\serial_monitor.py"
```

---

## 📊 输出示例

### 串口监控 GUI

```
========== BOOT ==========
Firmware started
WiFi connecting...
WiFi connected, IP: 192.168.137.234
WebSocket server started on port 8888
State: IDLE
=========================
```

### 日志文件

**路径：** `E:\Personal\sEMG_Project\logs\serial\serial_log_20260530_230500.txt`

**内容：** 与 GUI 显示一致，纯文本格式，可用 VS Code 查看。

---

## 🔧 配置说明

### 路径配置

| 配置项 | 值 |
|--------|-----|
| 固件目录 | `E:\Personal\sEMG_Project\firmware` |
| 串口日志目录 | `E:\Personal\sEMG_Project\logs\serial` |
| 串口日志格式 | `serial_log_YYYYMMDD_HHMMSS.txt` |
| 串口 | COM4 / 115200 |
| pio.exe | `C:\Users\honghuang\.platformio\penv\Scripts\pio.exe` |

### 固件端配置

**文件：** `firmware/src/main.cpp`

**关键代码：**
```cpp
void setup() {
    delay(3000);  // 等串口监控连接
    Serial.begin(115200);
    LOG("\n\n========== BOOT ==========\n");  // 这时才输出日志
}
```

**原理：**
- Skill 在 `pio upload` 完成后立即启动监控（~0.5 秒）
- 此时板子还在 `delay(3000)` 中
- 监控就绪时 BOOT 日志刚开始输出，**不会漏掉任何日志** ✅

---

## 🚨 常见问题

### 1. 上传失败（COM4 占用）

**现象：**
```
Error: Failed to connect to COM4
```

**原因：** 串口监控 GUI 占用 COM4。

**解决：**
- 说 "上传固件"，Skill 会自动杀 COM4 占用进程
- 或手动关闭串口监控 GUI

---

### 2. 开机日志丢失

**现象：** 串口监控启动后，看不到 `========== BOOT ==========` 日志。

**原因：** 监控启动太晚，板子已经输出了 BOOT 日志。

**解决：**
- 用本 Skill 上传固件（已修复时序问题）
- 或手动重启板子（按 RESET 按钮）

---

### 3. 日志文件 GBK 编码报错（PowerShell）

**现象：**
```
UnicodeEncodeError: 'gbk' codec can't encode characters
```

**原因：** Windows PowerShell 默认编码是 GBK，无法显示 Unicode 表情符号。

**解决：**
- Skill 已修复（纯 ASCII 输出，无 Unicode 表情）
- 或用 VS Code 查看日志文件（自动识别编码）

---

### 4. 固件上传成功，但板子没反应

**现象：** 上传成功，但板子没输出日志。

**原因：**
- 板子没进入 `setup()`（卡在 bootloader）
- 串口监控没连接到正确的 COM 口

**解决：**
- 检查设备管理器 → 端口（COM 和 LPT）→ 确认是 COM4
- 手动重启板子（按 RESET 按钮）
- 重新运行 `upload_and_monitor.py`

---

## 📚 详细文档

- **Skill 体系概览：** [`../README.md`](../README.md)
- **固件日志分析：** [`../firmware-debug/USAGE.md`](../firmware-debug/USAGE.md)
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
