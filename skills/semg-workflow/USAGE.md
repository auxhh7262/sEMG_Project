# semg-workflow 使用说明

组合工作流工具（一键部署 + 一键分析）。

---

## 📋 触发词

- "上传并编译"
- "完整上传"
- "分析所有日志"
- "完整分析"

---

## 🚀 功能说明

### 工作流1：「上传并编译」（deploy）

**触发词：** "上传并编译" 或 "完整上传"

**执行步骤：**
```
1. 杀 COM4 占用进程
2. 编译上传固件
3. 启动串口监控 GUI
4. 检测小程序代码改动（30 分钟内）
5. 编译小程序（微信开发者工具 CLI）
6. 生成预览码（推送到手机）
7. 启动小程序日志服务器 GUI
```

**输出：**
- 固件上传成功 ✅
- 小程序编译成功 ✅
- 两个 GUI 窗口（串口监控 + 小程序日志）

**命令行：**
```powershell
python "E:\Personal\sEMG_Project\skills\semg-workflow\workflow.py" deploy
```

---

### 工作流2：「分析所有日志」（analyze）

**触发词：** "分析所有日志" 或 "完整分析"

**执行步骤：**
```
1. 读取最新固件日志（logs/serial/）
2. 扫描常见问题模式（HardFault、超时、JSON 解析失败等）
3. 输出固件日志分析报告
4. 读取最新小程序日志（logs/mini/）
5. 扫描常见问题模式（连接失败、超时、WebSocket 断连等）
6. 输出小程序日志分析报告
7. 输出联合总结
```

**输出：**
- 固件日志分析报告（含问题列表 + 建议）
- 小程序日志分析报告（含问题列表 + 建议）
- 联合总结（下一步建议）

**命令行：**
```powershell
python "E:\Personal\sEMG_Project\skills\semg-workflow\workflow.py" analyze
```

---

## 📦 使用方式

### 方式1：语音指令（推荐）

```
说："上传并编译" 或 "完整上传"
→ 自动执行：固件上传 + 小程序编译 + 启动两个日志窗口
```

```
说："分析所有日志" 或 "完整分析"
→ 自动执行：固件日志分析 + 小程序日志分析 + 联合报告
```

---

### 方式2：命令行

```powershell
# 工作流1：上传并编译
python "E:\Personal\sEMG_Project\skills\semg-workflow\workflow.py" deploy

# 工作流2：分析所有日志
python "E:\Personal\sEMG_Project\skills\semg-workflow\workflow.py" analyze
```

---

## 📊 输出示例

### 工作流1 输出

```
============================================================
  sEMG Workflow: Deploy (Upload + Preview)
============================================================
  [1/2] Firmware upload
        Uploading...
        [OK] Firmware uploaded successfully
        [OK] Serial monitor started

  [2/2] Miniprogram preview
        Compiling...
        [OK] Miniprogram compiled successfully
        [OK] Preview pushed to phone
        [OK] Log server started

  [DONE] Workflow completed successfully!
============================================================
```

---

### 工作流2 输出

```
============================================================
  sEMG Workflow: Analyze All Logs
============================================================
  [1/2] Firmware log analysis
        File: serial_log_20260530_230500.txt
        [HIGH] High (1 items)
          1. Command timeout
             Log: start_calib MAX failed: CMD_TIMEOUT
             Suggestion: Miniprogram waiting for firmware ACK timeout

        [MED] Medium (1 items)
          1. Data anomaly
             Log: RMS=8.9 out of reasonable range
             Suggestion: Check electrode contact

  [2/2] Miniprogram log analysis
        File: mini_log_20260530_230500.txt
        [HIGH] High (1 items)
          1. WebSocket disconnected
             Log: WebSocket closed, code: 1006
             Suggestion: Check WiFi connection stability

  [SUMMARY] Next steps:
  1. Fix firmware command timeout issue (start_calib CMD_TIMEOUT)
  2. Investigate WiFi disconnection (WebSocket 1006)
  3. Verify electrode contact (RMS too low)

  [DONE] Workflow completed successfully!
============================================================
```

---

## ⚙️ 配置说明

### 路径配置

| 配置项 | 值 |
|--------|-----|
| 项目根目录 | `E:\Personal\sEMG_Project\` |
| 固件目录 | `E:\Personal\sEMG_Project\firmware` |
| 小程序目录 | `E:\Personal\sEMG_Project\mini_program` |
| 日志目录 | `E:\Personal\sEMG_Project\logs\` |
| 工作流脚本 | `workflow.py` |

### 子 Skill 路径

| Skill | 脚本路径 |
|-------|----------|
| firmware-upload | `..\firmware-upload\upload_and_monitor.py` |
| miniprogram-preview | `..\miniprogram-preview\preview.py` |
| firmware-debug | `..\firmware-debug\analyze_log.py` |
| miniprogram-debug | `..\miniprogram-debug\analyze_mini_log.py` |

---

## 🚨 常见问题

### 1. 工作流执行失败（子 Skill 未找到）

**现象：**
```
[ERR] Skill not found: ..\firmware-upload\upload_and_monitor.py
```

**原因：** 子 Skill 路径错误，或 Skill 未安装。

**解决：**
- 检查 `skills/` 目录下是否存在 `firmware-upload/`、`miniprogram-preview/` 等子目录
- 重新创建缺失的 Skill

---

### 2. 固件上传失败（COM4 占用）

**现象：**
```
[ERR] Firmware upload failed: Failed to connect to COM4
```

**原因：** 串口监控 GUI 占用 COM4。

**解决：**
- 工作流会自动杀 COM4 占用进程
- 或手动关闭串口监控 GUI

---

### 3. 小程序编译失败（CLI 未找到）

**现象：**
```
[ERR] Miniprogram preview failed: CLI not found
```

**原因：** 微信开发者工具未安装或 CLI 路径错误。

**解决：**
- 安装微信开发者工具
- 检查 CLI 路径：`D:\Program Files\微信web开发者工具\cli.bat`

---

### 4. 日志分析无输出

**现象：** 工作流2 执行后，没有日志分析报告。

**原因：**
- 还没生成日志（没上传固件 / 没启动日志服务器）
- 日志目录路径错误

**解决：**
- 先执行工作流1（上传并编译），生成日志
- 再执行工作流2（分析所有日志）

---

## 📚 详细文档

- **Skill 体系概览：** [`../README.md`](../README.md)
- **firmware-upload 使用说明：** [`../firmware-upload/USAGE.md`](../firmware-upload/USAGE.md)
- **miniprogram-preview 使用说明：** [`../miniprogram-preview/USAGE.md`](../miniprogram-preview/USAGE.md)
- **firmware-debug 使用说明：** [`../firmware-debug/USAGE.md`](../firmware-debug/USAGE.md)
- **miniprogram-debug 使用说明：** [`../miniprogram-debug/USAGE.md`](../miniprogram-debug/USAGE.md)
- **git-push 使用说明：** [`../git-push/USAGE.md`](../git-push/USAGE.md)

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

**新的工作流想法？**
- 在 GitHub Issues 提出你的想法
- 或直接修改 `workflow.py` 添加新工作流

---

## 📄 许可证

MIT License
