# miniprogram-debug 使用说明

小程序日志分析工具。

---

## 📋 触发词

- "分析小程序"
- "小程序 debug"
- "分析小程序日志"

---

## 🚀 功能说明

### 1️⃣ 读取最新小程序日志

**日志来源：** `E:\Personal\sEMG_Project\logs\mini\mini_log_YYYYMMDD_HHMMSS.txt`

**选择规则：** 按修改时间排序，选择最新的 `.txt` 文件。

---

### 2️⃣ 扫描常见问题模式

**检测的问题模式：**

| 严重度 | 问题类型 | 关键词 |
|--------|----------|--------|
| [HIGH] | 连接失败 | `connect fail`, `WiFi error` |
| [HIGH] | 命令超时 | `CMD_TIMEOUT`, `timeout` |
| [HIGH] | JSON 解析失败 | `parse error`, `JSON error` |
| [HIGH] | WebSocket 断连 | `disconnect`, `close` |
| [MED ] | 状态异常 | `state error`, `invalid state` |
| [MED ] | 数据异常 | `data error`, `RMS/MDF 异常` |
| [OK ] | 连接成功 | `connect success`, `ready` |
| [OK ] | 命令成功 | `CMD success`, `ACK` |

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
说："分析小程序" 或 "小程序 debug"
→ 自动读取最新日志 → 扫描问题 → 输出报告
```

---

### 方式2：命令行

```powershell
# 分析最新日志
python "E:\Personal\sEMG_Project\skills\miniprogram-debug\analyze_mini_log.py"

# 分析指定日志文件
python "E:\Personal\sEMG_Project\skills\miniprogram-debug\analyze_mini_log.py" -f "E:\Personal\sEMG_Project\logs\mini\mini_log_20260529_080100.txt"
```

---

## 📊 输出示例

```
============================================================
  sEMG Miniprogram Log Analysis Report
============================================================
  File: mini_log_20260529_080100.txt
  Log time: 2026-05-29 08:01:00
  Report time: 2026-05-29 23:33:00
  Total lines: 156

  [HIGH] High (1 items)
    1. Command timeout
       Log: start_calib timeout after 5000ms
       Suggestion: Check if firmware is responding normally

  [MED ] Medium (1 items)
    1. State anomaly
       Log: invalid state transition: IDLE -> STREAMING
       Suggestion: Check state machine logic
============================================================
```

---

## ⚙️ 配置说明

### 路径配置

| 配置项 | 值 |
|--------|-----|
| 日志目录 | `E:\Personal\sEMG_Project\logs\mini` |
| 日志文件格式 | `mini_log_YYYYMMDD_HHMMSS.txt` |
| 分析脚本 | `analyze_mini_log.py` |

### 分析规则配置

**文件：** `skills/miniprogram-debug/analyze_mini_log.py`

**修改检测模式：**
```python
PATTERNS = [
    (re.compile(r"connect fail|WiFi error"), "Connection failed", "[HIGH]"),
    (re.compile(r"CMD_TIMEOUT|timeout"), "Command timeout", "[HIGH]"),
    # ... add more patterns
]
```

---

## 🚨 常见问题

### 1. 找不到日志文件

**现象：**
```
[ERR] No log file found in E:\Personal\sEMG_Project\logs\mini
```

**原因：**
- 还没启动日志服务器（没生成日志）
- 日志目录路径错误

**解决：**
- 说 "编译小程序"，先启动日志服务器生成日志
- 检查日志目录是否存在

---

### 2. 报告输出 GBK 编码报错（PowerShell）

**现象：**
```
UnicodeEncodeError: 'gbk' codec can't encode characters
```

**原因：** Windows PowerShell 默认编码是 GBK，无法显示 Unicode 表情符号。

**解决：**
- Skill 已修复（纯 ASCII 输出，无 Unicode 表情）
- 或用 VS Code 查看报告（自动识别编码）

---

### 3. 想分析旧日志（不是最新的）

**现象：** 脚本只分析最新日志，无法选择旧日志。

**解决：**
```powershell
# 直接指定日志文件路径
python "E:\Personal\sEMG_Project\skills\miniprogram-debug\analyze_mini_log.py" -f "E:\Personal\sEMG_Project\logs\mini\mini_log_20260529_080100.txt"
```

---

### 4. 误报：连接失败（但实际成功）

**现象：**
```
[HIGH] High (1 items)
  1. Connection failed
     Log: connect fail, retrying... connect success
```

**原因：** 正则匹配了 `connect fail`，但没检查日志是否显示后续 `connect success`。

**解决：**
- 修改 `analyze_mini_log.py` 的 `analyze()` 函数，添加上下文检查
- 或调整正则，只匹配最终的连接失败（不包含 `success`）

---

## 📚 详细文档

- **Skill 体系概览：** [`../README.md`](../README.md)
- **固件日志分析：** [`../firmware-debug/USAGE.md`](../firmware-debug/USAGE.md)
- **组合工作流：** [`../semg-workflow/USAGE.md`](../semg-workflow/USAGE.md)

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

**新的检测模式想法？**
- 在 GitHub Issues 提出你的想法
- 或直接修改 `analyze_mini_log.py` 的 `PATTERNS` 列表

---

## 📄 许可证

MIT License
