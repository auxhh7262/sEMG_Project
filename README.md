# sEMG 表面肌电信号监测手环

Arduino UNO R4 WiFi + 微信小程序，实时监测肌肉疲劳。

---

## 📦 项目结构

```
sEMG_Project/
├── firmware/          # 固件（PlatformIO + RA4M1）
├── mini_program/      # 微信小程序
├── logs/              # 日志文件
│   ├── serial/        # 固件串口日志（.txt）
│   └── mini/          # 小程序日志（.txt）
├── skills/            # sEMG 调试 Skill 体系
│   ├── firmware-upload/       # 固件上传 + 串口监控
│   ├── miniprogram-preview/  # 小程序编译 + 日志采集
│   ├── firmware-debug/       # 固件日志分析
│   ├── miniprogram-debug/    # 小程序日志分析
│   ├── git-push/             # Git 提交推送
│   └── semg-workflow/        # 组合工作流
└── docs/              # 项目文档
```

---

## 🚀 快速开始（3 步搞定）

### 1️⃣ 上传固件

**方式1：语音指令**
```
说："上传固件" 或 "编译上传"
→ 自动杀 COM4 占用 → 编译上传 → 启动串口监控 GUI
```

**方式2：命令行**
```powershell
python "E:\Personal\sEMG_Project\skills\firmware-upload\upload_and_monitor.py"
```

---

### 2️⃣ 编译小程序

**方式1：语音指令**
```
说："编译小程序" 或 "预览小程序"
→ 调用微信开发者工具 CLI → 生成预览码 → 推送到手机
```

**方式2：命令行**
```powershell
python "E:\Personal\sEMG_Project\skills\miniprogram-preview\preview.py"
```

---

### 3️⃣ 分析日志

**方式1：语音指令**
```
说："分析所有日志" 或 "完整分析"
→ 自动分析固件 + 小程序日志 → 输出联合报告
```

**方式2：命令行**
```powershell
python "E:\Personal\sEMG_Project\skills\semg-workflow\workflow.py" analyze
```

---

## 🧰 sEMG 调试 Skill 体系

全套调试工具，覆盖 **固件开发 → 小程序开发 → 日志分析 → Git 提交** 全流程。

### 基础 Skill（5 个）

| Skill | 触发词 | 功能 |
|-------|--------|------|
| **firmware-upload** | "上传固件"、"编译上传" | 固件编译上传 + 串口监控 GUI |
| **miniprogram-preview** | "编译小程序"、"预览小程序" | 小程序编译 + 日志服务器 GUI |
| **firmware-debug** | "分析"、"分析日志"、"debug" | 固件日志分析 + 结构化报告 |
| **miniprogram-debug** | "分析小程序"、"小程序 debug" | 小程序日志分析 + 结构化报告 |
| **git-push** | "提交代码"、"git push" | Git 提交推送（自动代理） |

### 组合 Skill（1 个）

| Skill | 触发词 | 功能 |
|-------|--------|------|
| **semg-workflow** | "上传并编译"、"分析所有日志" | 组合工作流（一键部署 + 一键分析） |

---

## 📋 组合工作流详解

### 工作流1：「上传并编译」（deploy）

**触发词：** "上传并编译"、"完整上传"

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

**触发词：** "分析所有日志"、"完整分析"

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

## 🛠️ 开发环境

| 工具 | 版本 / 路径 |
|------|--------------|
| PlatformIO | v6.x（`pio.exe`） |
| Arduino UNO R4 WiFi | RA4M1, 48MHz, 32KB RAM |
| 微信开发者工具 | CLI: `D:\Program Files\微信web开发者工具\cli.bat` |
| Git | v2.53.0（GitHub Desktop 内置） |
| Python | v3.x（`sys.executable`） |

---

## 📝 常见问题

### 1. 固件上传失败（COM4 占用）

**原因：** 串口监控 GUI 占用 COM4，PlatformIO 无法同时上传。

**解决：**
- 说 "上传固件"，Skill 会自动杀 COM4 占用进程
- 或手动关闭串口监控 GUI

---

### 2. 小程序编译失败（CLI 未找到）

**原因：** 微信开发者工具未安装或 CLI 路径错误。

**解决：**
- 安装微信开发者工具
- 检查 CLI 路径：`D:\Program Files\微信web开发者工具\cli.bat`

---

### 3. Git push 失败（公司网络）

**原因：** 公司网络封禁 `github.com:443`，需要代理。

**解决：**
- 说 "提交代码"，Skill 会自动设置代理（`http://shproxy.asrmicro.com:80`）
- 或手动设置代理：
  ```powershell
  git config --global http.proxy http://shproxy.asrmicro.com:80
  git push origin main
  git config --global --unset http.proxy
  ```

---

### 4. 日志文件 GBK 编码报错（PowerShell）

**原因：** Windows PowerShell 默认编码是 GBK，无法显示 Unicode 表情符号。

**解决：**
- Skill 已修复（纯 ASCII 输出，无 Unicode 表情）
- 或用 VS Code 查看日志文件（自动识别编码）

---

## 📚 详细文档

- **Skill 体系概览：** [`skills/README.md`](skills/README.md)
- **firmware-upload 使用说明：** [`skills/firmware-upload/USAGE.md`](skills/firmware-upload/USAGE.md)
- **miniprogram-preview 使用说明：** [`skills/miniprogram-preview/USAGE.md`](skills/miniprogram-preview/USAGE.md)
- **firmware-debug 使用说明：** [`skills/firmware-debug/USAGE.md`](skills/firmware-debug/USAGE.md)
- **miniprogram-debug 使用说明：** [`skills/miniprogram-debug/USAGE.md`](skills/miniprogram-debug/USAGE.md)
- **git-push 使用说明：** [`skills/git-push/USAGE.md`](skills/git-push/USAGE.md)
- **semg-workflow 使用说明：** [`skills/semg-workflow/USAGE.md`](skills/semg-workflow/USAGE.md)

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

---

## 📄 许可证

MIT License

---

## 📞 联系方式

- **项目地址：<ADDRESS_REMOVED>
- **Issues：** https://github.com/auxhh7262/sEMG_Project/issues
