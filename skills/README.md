# sEMG 调试 Skill 体系

全套调试工具，覆盖 **固件开发 → 小程序开发 → 日志分析 → Git 提交** 全流程。

---

## 📋 Skill 快速参考

### 基础 Skill（5 个）

| Skill | 触发词 | 功能 | 脚本 |
|-------|--------|------|------|
| **firmware-upload** | "上传固件"、"编译上传" | 固件编译上传 + 串口监控 GUI | `upload_and_monitor.py` |
| **miniprogram-preview** | "编译小程序"、"预览小程序" | 小程序编译 + 日志服务器 GUI | `preview.py` |
| **firmware-debug** | "分析"、"分析日志"、"debug" | 固件日志分析 + 结构化报告 | `analyze_log.py` |
| **miniprogram-debug** | "分析小程序"、"小程序 debug" | 小程序日志分析 + 结构化报告 | `analyze_mini_log.py` |
| **git-push** | "提交代码"、"git push" | Git 提交推送（自动代理） | `push.py` |

### 组合 Skill（1 个）

| Skill | 触发词 | 功能 | 脚本 |
|-------|--------|------|------|
| **semg-workflow** | "上传并编译"、"分析所有日志" | 组合工作流（一键部署 + 一键分析） | `workflow.py` |

---

## 🚀 快速开始

### 场景1：修改了固件 + 小程序代码，想一键部署

**语音指令：**
```
说："上传并编译" 或 "完整上传"
→ 自动执行：固件上传 + 小程序编译 + 启动两个日志窗口
```

**命令行：**
```powershell
python "E:\Personal\sEMG_Project\skills\semg-workflow\workflow.py" deploy
```

---

### 场景2：测试后想看所有日志

**语音指令：**
```
说："分析所有日志" 或 "完整分析"
→ 自动执行：固件日志分析 + 小程序日志分析 + 联合报告
```

**命令行：**
```powershell
python "E:\Personal\sEMG_Project\skills\semg-workflow\workflow.py" analyze
```

---

### 场景3：只上传固件

**语音指令：**
```
说："上传固件" 或 "编译上传"
→ 只执行：固件上传 + 串口监控
```

**命令行：**
```powershell
python "E:\Personal\sEMG_Project\skills\firmware-upload\upload_and_monitor.py"
```

---

### 场景4：只编译小程序

**语音指令：**
```
说："编译小程序" 或 "预览小程序"
→ 只执行：小程序编译 + 日志服务器
```

**命令行：**
```powershell
python "E:\Personal\sEMG_Project\skills\miniprogram-preview\preview.py"
```

---

## 📦 安装

### 前置条件

| 工具 | 版本要求 | 安装说明 |
|------|----------|----------|
| PlatformIO | v6.x | `pio.exe` 已安装 |
| 微信开发者工具 | 最新版 | CLI 路径：`D:\Program Files\微信web开发者工具\cli.bat` |
| Git | v2.x | 已配置 `C:\Git` junction |
| Python | v3.x | `sys.executable` 可用 |

### 项目路径

| 配置项 | 值 |
|--------|-----|
| 项目根目录 | `E:\Personal\sEMG_Project\` |
| 固件目录 | `E:\Personal\sEMG_Project\firmware` |
| 小程序目录 | `E:\Personal\sEMG_Project\mini_program` |
| 日志目录 | `E:\Personal\sEMG_Project\logs\` |
| Skill 目录 | `E:\Personal\sEMG_Project\skills\` |

---

## 🧰 详细使用说明

### 1️⃣ firmware-upload（固件上传 + 串口监控）

**触发词：** "上传固件"、"编译上传"、"烧录固件"、"upload firmware"

**功能：**
1. 杀 COM4 占用进程（python.exe / platformio.exe / pio.exe）
2. `pio run -t upload --upload-port COM4`（上传时串口监控关闭）
3. 上传完成后立即启动 `serial_monitor.py`（tkinter GUI + timestamped log）
4. 板子重启 → `delay(3000)` 等待期间监控已就绪 → 完整捕获开机日志 ✅

**串口监控 GUI 功能：**
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

**详细文档：** [`firmware-upload/USAGE.md`](firmware-upload/USAGE.md)

---

### 2️⃣ miniprogram-preview（小程序编译 + 日志采集）

**触发词：** "编译小程序"、"预览小程序"、"生成预览码"、"开小程序日志"

**功能：**
1. 调用微信开发者工具 CLI 编译小程序
2. 生成预览二维码（推送到手机，无需扫码）
3. 启动日志服务器 GUI（端口 9876，tkinter GUI 实时显示）
4. 小程序日志实时采集 + 写入日志文件

**日志服务器 GUI 功能：**
- 实时显示：绿色终端风格界面，实时输出小程序日志
- 过滤关键词：LogForward, heartbeat, ping, pong
- 时间戳文件名：每次运行生成独立 log 文件
  - 格式：`logs/mini/mini_log_YYYYMMDD_HHMMSS.txt`

**详细文档：** [`miniprogram-preview/USAGE.md`](miniprogram-preview/USAGE.md)

---

### 3️⃣ firmware-debug（固件日志分析）

**触发词：** "分析"、"分析日志"、"debug"、"固件调试"

**功能：**
1. 读取 `logs/serial/` 下最新日志文件
2. 扫描常见问题模式（HardFault、超时、JSON 解析失败等）
3. 输出结构化报告（按严重度分类）

**检测的问题模式：**

| 严重度 | 问题类型 | 关键词 |
|--------|----------|--------|
| 🔴 严重 | 固件崩溃 | `Usage Fault`, `HardFault`, `ASSERT` |
| 🔴 高 | 命令超时 | `CMD_TIMEOUT` |
| 🔴 高 | JSON 解析失败 | `deserializeJson failed`, `parse error` |
| 🔴 高 | 状态机非法转换 | `invalid transition`, `transitionTo` |
| 🔴 高 | WebSocket 断连 | `TCP disconnected`, `client disconnect` |
| 🟡 中 | 校准验证失败 | `validation failed` |
| 🟡 中 | JSON 序列化失败 | `serializeJson failed` |
| 🟡 中 | Flash 操作失败 | `Flash error`, `SPI fail` |
| 🟢 正常 | 阶段完成 | `REST done`, `MAX done`, `calibration PASS` |

**详细文档：** [`firmware-debug/USAGE.md`](firmware-debug/USAGE.md)

---

### 4️⃣ miniprogram-debug（小程序日志分析）

**触发词：** "分析小程序"、"小程序 debug"、"分析小程序日志"

**功能：**
1. 读取 `logs/mini/` 下最新日志文件
2. 扫描常见问题模式（连接失败、超时、WebSocket 断连等）
3. 输出结构化报告（按严重度分类）

**检测的问题模式：**

| 严重度 | 问题类型 | 关键词 |
|--------|----------|--------|
| 🔴 严重 | 连接失败 | `connect fail`, `WiFi error` |
| 🔴 高 | 命令超时 | `CMD_TIMEOUT`, `timeout` |
| 🔴 高 | JSON 解析失败 | `parse error`, `JSON error` |
| 🔴 高 | WebSocket 断连 | `disconnect`, `close` |
| 🟡 中 | 状态异常 | `state error`, `invalid state` |
| 🟡 中 | 数据异常 | `data error`, `RMS/MDF 异常` |
| 🟢 正常 | 连接成功 | `connect success`, `ready` |
| 🟢 正常 | 命令成功 | `CMD success`, `ACK` |

**详细文档：** [`miniprogram-debug/USAGE.md`](miniprogram-debug/USAGE.md)

---

### 5️⃣ git-push（Git 提交推送）

**触发词：** "提交代码"、"git push"、"推送代码"、"git commit"、"保存到 GitHub"

**功能：**
1. 临时设置公司代理（`http://shproxy.asrmicro.com:80`）
2. `git status` 检测当前改动
3. `git add` 全部改动（或用户指定的文件）
4. `git commit`（自动生成 message 或用户指定）
5. `git push origin main`
6. 清除代理设置（避免影响其他网络访问）

**使用方式：**

| 命令 | 说明 |
|------|------|
| `python push.py` | 默认模式（自动生成 commit message） |
| `python push.py -m "feat: 添加XXX功能"` | 自定义 commit message |
| `python push.py -f "firmware/src/"` | 只提交指定文件 |
| `python push.py --dry-run` | 只看改动，不提交 |

**Commit Message 规范：**

| 前缀 | 用途 |
|------|------|
| `feat:` | 新功能 |
| `fix:` | Bug 修复 |
| `chore:` | 构建/工具/文档等非业务改动 |
| `refactor:` | 代码重构（不改功能） |
| `docs:` | 文档更新 |
| `style:` | 格式调整（不影响逻辑） |
| `perf:` | 性能优化 |

**详细文档：** [`git-push/USAGE.md`](git-push/USAGE.md)

---

### 6️⃣ semg-workflow（组合工作流）

**触发词：** "上传并编译"、"完整上传"、"分析所有日志"、"完整分析"

**工作流1：「上传并编译」（deploy）**
1. 杀 COM4 占用进程
2. 编译上传固件
3. 启动串口监控 GUI
4. 检测小程序代码改动（30 分钟内）
5. 编译小程序（微信开发者工具 CLI）
6. 生成预览码（推送到手机）
7. 启动小程序日志服务器 GUI

**工作流2：「分析所有日志」（analyze）**
1. 读取最新固件日志（`logs/serial/`）
2. 扫描常见问题模式（HardFault、超时、JSON 解析失败等）
3. 输出固件日志分析报告
4. 读取最新小程序日志（`logs/mini/`）
5. 扫描常见问题模式（连接失败、超时、WebSocket 断连等）
6. 输出小程序日志分析报告
7. 输出联合总结

**详细文档：** [`semg-workflow/USAGE.md`](semg-workflow/USAGE.md)

---

## 📊 输出示例

### 固件日志分析报告

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

### 小程序日志分析报告

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

## 🔧 常见问题

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

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

**新的 Skill 想法？**
- 在 GitHub Issues 提出你的想法
- 或参考 [`../docs/SKILL_GUIDE.md`](../docs/SKILL_GUIDE.md) 自己创建

---

## 📄 许可证

MIT License

---

## 📞 联系方式

- **项目地址：<ADDRESS_REMOVED>
- **Issues：** https://github.com/auxhh7262/sEMG_Project/issues
