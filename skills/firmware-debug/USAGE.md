# firmware-debug 使用说明

固件日志分析工具。

---

## 📋 触发词

- "分析"
- "分析日志"
- "debug"
- "固件调试"
- "分析固件"

---

## 🚀 功能说明

### 1️⃣ 读取最新固件日志

**日志来源：** `E:\Personal\sEMG_Project\logs\serial\serial_log_YYYYMMDD_HHMMSS.txt`

**选择规则：** 按修改时间排序，选择最新的 `.txt` 文件。

---

### 2️⃣ 扫描常见问题模式

**检测的问题模式：**

| 严重度 | 问题类型 | 关键词 |
|--------|----------|--------|
| [HIGH] | 固件崩溃 | `Usage Fault`, `HardFault`, `ASSERT` |
| [HIGH] | 命令超时 | `CMD_TIMEOUT` |
| [HIGH] | JSON 解析失败 | `deserializeJson failed`, `parse error` |
| [HIGH] | 状态机非法转换 | `invalid transition`, `transitionTo` |
| [HIGH] | WebSocket 断连 | `TCP disconnected`, `client disconnect` |
| [MED ] | 校准验证失败 | `validation failed` |
| [MED ] | JSON 序列化失败 | `serializeJson failed` |
| [MED ] | Flash 操作失败 | `Flash error`, `SPI fail` |
| [MED ] | 数据异常 | RMS/MDF 超出合理范围 |
| [OK ] | 阶段完成 | `REST done`, `MAX done`, `calibration PASS` |

**误报过滤：**
- 日志包含 `deserializeJson` 但同时包含 `err=OK` → 跳过，不记为 "JSON parse failed"

---

### 3️⃣ 输出结构化报告

**报告内容：**
- 文件信息（文件名、日志时间、报告时间、总行数）
- 问题列表（按严重度分类）
- 每个问题的日志原文 + 建议

**输出格式：** 纯 ASCII，无 Unicode 表情（避免 Windows GBK 编码错误）

---

## 📦 使用方式

### 方式1：语音指令（推荐）

```
说："分析" 或 "分析日志"
→ 自动读取最新日志 → 扫描问题 → 输出报告
```

---

### 方式2：命令行

```powershell
# 分析最新日志
python "E:\Personal\sEMG_Project\skills\firmware-debug\analyze_log.py"

# 分析指定日志文件
python "E:\Personal\sEMG_Project\skills\firmware-debug\analyze_log.py" -f "E:\Personal\sEMG_Project\logs\serial\serial_log_20260528_210500.txt"
```

---

## 📊 输出示例

```
============================================================
  sEMG Firmware Log Analysis Report
============================================================
  File: serial_log_20260528_210500.txt
  Log time: 2026-05-28 21:05:38
  Report time: 2026-05-28 22:19:00
  Total lines: 342

  [HIGH] High (1 items)
    1. Command timeout
       Log: start_calib MAX failed: CMD_TIMEOUT
       Suggestion: Miniprogram waiting for firmware ACK timeout, firmware not responding

  [MED ] Medium (1 items)
    1. Data anomaly
       Log: RMS=8.9 out of reasonable range (0.01~10000 mV)
       Suggestion: Check electrode contact or amplifier circuit

  [OK ] No known problem patterns found
============================================================
```

---

## ⚙️ 配置说明

### 路径配置

| 配置项 | 值 |
|--------|-----|
| 日志目录 | `E:\Personal\sEMG_Project\logs\serial` |
| 日志文件格式 | `serial_log_YYYYMMDD_HHMMSS.txt` |
| 分析脚本 | `analyze_log.py` |

### 分析规则配置

**文件：** `skills/firmware-debug/analyze_log.py`

**修改检测模式：**
```python
PATTERNS = [
    (re.compile(r"Usage Fault|HardFault|ASSERT"), "Firmware crash", "[HIGH]"),
    (re.compile(r"CMD_TIMEOUT"), "Command timeout", "[HIGH]"),
    # ... 添加更多模式
]
```

---

## 🚨 常见问题

### 1. 找不到日志文件

**现象：**
```
[ERR] No log file found in E:\Personal\sEMG_Project\logs\serial
```

**原因：**
- 还没上传固件（没生成日志）
- 日志目录路径错误

**解决：**
- 说 "上传固件"，先上传固件生成日志
- 检查日志目录是否存在

---

### 2. 误报：JSON 解析失败（但实际成功）

**现象：**
```
[HIGH] High (1 items)
  1. JSON parse failed
     Log: deserializeJson returned, err=OK
```

**原因：** 正则匹配了 `deserializeJson`，但没检查日志是否显示 `err=OK`（成功）。

**解决：**
- Skill 已修复（添加了误报过滤逻辑）
- 如果还遇到，检查 `analyze_log.py` 的 `analyze()` 函数

---

### 3. 报告输出 GBK 编码报错（PowerShell）

**现象：**
```
UnicodeEncodeError: 'gbk' codec can't encode characters
```

**原因：** Windows PowerShell 默认编码是 GBK，无法显示 Unicode 表情符号。

**解决：**
- Skill 已修复（纯 ASCII 输出，无 Unicode 表情）
- 或用 VS Code 查看报告（自动识别编码）

---

### 4. 想分析旧日志（不是最新的）

**现象：** 脚本只分析最新日志，无法选择旧日志。

**解决：**
```powershell
# 直接指定日志文件路径
python "E:\Personal\sEMG_Project\skills\firmware-debug\analyze_log.py" -f "E:\Personal\sEMG_Project\logs\serial\serial_log_20260528_210500.txt"
```

---

## 📚 详细文档

- **Skill 体系概览：** [`../README.md`](../README.md)
- **小程序日志分析：** [`../miniprogram-debug/USAGE.md`](../miniprogram-debug/USAGE.md)
- **组合工作流：** [`../semg-workflow/USAGE.md`](../semg-workflow/USAGE.md)

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

**新的检测模式想法？**
- 在 GitHub Issues 提出你的想法
- 或直接修改 `analyze_log.py` 的 `PATTERNS` 列表

---

## 📄 许可证

MIT License
