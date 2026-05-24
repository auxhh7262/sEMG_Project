# TOOLS.md - Local Notes

Skills define _how_ tools work. This file is for _your_ specifics — the stuff that's unique to your setup.

---

## sEMG 调试快捷指令

| 指令 | 模式 | 动作 |
|------|------|------|
| **分析** | 继续分析上次的问题 | 读 DEBUG_CONTEXT.md → 读完整日志 → 只分析上次的问题 |
| **分析 XXX** | 开始分析新问题 | 更新 DEBUG_CONTEXT.md → 读完整日志 → 只分析XXX问题 |
| **分析日志** / **全面分析** | 全面分析log | 读完整日志 → 扫描所有问题 → 列出编号列表 |
| **开日志** | - | 自动启动固件串口监控 + 小程序日志服务器 |
| **关日志** | - | 关闭两个日志窗口 |
| **看日志** | - | 读取完整日志（两个都读） |
| **上传** | - | 编译固件 → 成功即直接上传（不问） + 自动启动串口监控 |

> 无需手动运行bat文件，说指令我直接帮你启动日志脚本

**注意**：编译固件时，成功即自动上传，不再询问确认。

---

## 📋 两种分析模式详解

### 模式1：继续分析上次的问题（"分析" 不带问题描述）

**触发方式：** 你说"分析"（不带问题描述）

**流程：**
```
1. 我读 DEBUG_CONTEXT.md，看看上次在分析什么问题
2. 我读完整日志（serial_log.txt + mini_log.txt）
3. 我检查：上次的问题还在吗？
   ├─ 还在 → 深度分析（根因 + 修复方案），不理其他问题
   └─ 不在了 → 告诉你"好像解决了"，让你确认
4. 你修复 → 再说"分析"
5. 我读新日志 → 检查修复成功了吗？
   ├─ 成功 → "✅ 验证通过"
   └─ 失败 → 继续分析
```

**关键：**
- 我**只关注上次的问题**（从 `DEBUG_CONTEXT.md` 读取）
- 即使日志里发现了其他问题，我也**只提一句**，不会展开分析
- 你说"分析" → 我就是继续分析上次的问题

---

### 模式2：全面分析log（"分析日志" 或 "全面分析"）

**触发方式：** 你说"分析日志" 或 "全面分析"

**流程：**
```
1. 我读完整日志
2. 我扫描所有 ERROR/WARN/异常指标
3. 我列出所有发现的问题（编号列表）：
   发现3个问题：
   1. realtime页面无数据
   2. 心跳超时只有45秒
   3. 固件开机就进入MONITORING
4. 你说"分析第2个" → 我深度分析第2个问题
5. 分析完 → 问你"还要分析第1个或第3个吗？"
```

**关键：**
- 我**扫描完整日志**，列出所有问题
- 你**选择先分析哪个**
- 分析完一个，再分析下一个

---

## 🔧 分析 自动流程（详细步骤）

### 步骤1：预检查 + 清旧日志

```powershell
# 杀 COM4 占用进程
taskkill /f /im platformio.exe
taskkill /f /im python.exe

# 启动微信开发者工具（如果未运行）
Start-Process "D:\Program Files\微信web开发者工具\微信web开发者工具.exe"

# 清空旧日志（关键！确保完整日志=本次运行）
Clear-Content -Path "E:\Personal\sEMG_Project\logs\serial_log.txt"
Clear-Content -Path "E:\Personal\sEMG_Project\logs\mini_log.txt"
```

---

### 步骤2：检测改动

```powershell
# 检测固件代码是否改动
$firmwarePath = "E:\Personal\sEMG_Project\firmware"
$firmwareModified = Get-ChildItem $firmwarePath -Recurse -File | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-30) }

# 检测小程序代码是否改动
$miniPath = "E:\Personal\sEMG_Project\mini_program"
$miniModified = Get-ChildItem $miniPath -Recurse -File | Where-Object { $_.LastWriteTime -gt (Get-Date).AddMinutes(-30) }
```

---

### 步骤3：上传/编译

```powershell
# 固件改了 → 上传固件
if ($firmwareModified) {
    cd $firmwarePath
    pio run -t upload
}

# 小程序改了 → 编译小程序（生成预览码）
if ($miniModified) {
    & "D:\Program Files\微信web开发者工具\cli.bat" auto-preview --project $miniPath
}
```

---

### 步骤4：开日志

```powershell
# 启动串口监控
Start-Process python -ArgumentList "E:\Personal\sEMG_Project\logs\serial_monitor.py" -WindowStyle Normal

# 启动小程序日志服务器
Start-Process python -ArgumentList "E:\Personal\sEMG_Project\logs\mini_log_server.py" -WindowStyle Normal
```

---

### 步骤5：等重启完成（2-3秒）

```powershell
Start-Sleep -Seconds 3
```

---

### 步骤6：读日志（完整文件）

```powershell
# 读完整串口日志
$serialLog = Get-Content "E:\Personal\sEMG_Project\logs\serial_log.txt" -Raw

# 读完整小程序日志
$miniLog = Get-Content "E:\Personal\sEMG_Project\logs\mini_log.txt" -Raw
```

---

### 步骤7：分析日志

```
# 如果是"分析"（继续上次的问题）
读 DEBUG_CONTEXT.md → 只分析"正在调试的问题"

# 如果是"分析日志"（全面分析）
扫描所有 ERROR/WARN → 列出所有问题 → 等你选择
```

---

### 步骤8：有错误？

```
if (发现错误) {
    告诉你根因 + 修复方案
    → 等你修复
    → 你再说"分析"
    → 回到步骤1（清旧日志 → 重新检测改动 → ...）
} else {
    告诉你"✅ 验证通过"
    → 更新 DEBUG_CONTEXT.md（"已解决问题"）
    → 等你下一个指令
}
```

---

## 📝 DEBUG_CONTEXT.md 设计

**文件路径：** `E:\Personal\sEMG_Project\docs	asks\DEBUG_CONTEXT.md`

**作用：** 记录当前调试上下文，支持跨会话记忆

**内容模板：**
```markdown
# 当前调试上下文

## 正在调试的问题
- **问题：** realtime页面无数据
- **状态：** 已修复监听器注册，待验证start命令
- **假设：** 可能是start命令没发，或固件没进STREAMING状态
- **下次分析重点：** 检查start命令是否发出，固件状态是否正确

## 已发现问题（待分析）
1. 心跳超时只有45秒
2. 固件开机就进入MONITORING

## 已解决问题
（无）
```

**流程：**
- 你说"分析 realtime页面无数据" → 我更新 `DEBUG_CONTEXT.md`，记录"正在调试的问题"
- 你说"分析"（不带问题描述）→ 我读 `DEBUG_CONTEXT.md`，继续分析"正在调试的问题"
- 你说"分析日志" → 我扫描完整日志，列出所有问题，并更新 `DEBUG_CONTEXT.md` 的"已发现问题"

---

## sEMG 项目路径

| 用途 | 路径 |
|------|------|
| 项目根目录 | `E:\Personal\sEMG_Project\` |
| **日志目录** | `E:\Personal\sEMG_Project\logs\` |
| **文档目录** | `E:\Personal\sEMG_Project\docs\` |
| 固件源码 | `E:\Personal\sEMG_Project\firmware` |
| 小程序源码 | `E:\Personal\sEMG_Project\mini_program` |

## 日志脚本（统一在 logs/ 目录）

| 脚本 | 功能 |
|------|------|
| `logs/start_logs.bat` | 一键开双日志（串口+小程序） |
| `logs/upload_and_log.bat` | 上传固件+开串口监控 |
| `logs/serial_monitor.py` | 串口监控 COM4→serial_log.txt |
| `logs/mini_log_server.py` | HTTP :9876→mini_log.txt |

## 快速命令（PowerShell）

```powershell
# 开日志（两个窗口）
E:\Personal\sEMG_Project\logs\start_logs.bat

# 上传固件+监控
E:\Personal\sEMG_Project\logs\upload_and_log.bat

# 读日志（完整当次session）
Get-Content E:\Personal\sEMG_Project\logs\serial_log.txt
Get-Content E:\Personal\sEMG_Project\logs\mini_log.txt

# 清日志
Clear-Content E:\Personal\sEMG_Project\logs\serial_log.txt
Clear-Content E:\Personal\sEMG_Project\logs\mini_log.txt

# 编译小程序（生成预览码）
& "D:\Program Files\微信web开发者工具\cli.bat" auto-preview --project E:\Personal\sEMG_Project\mini_program
```

## 微信开发者工具 CLI

```powershell
# 自动编译 + 生成预览码
& "D:\Program Files\微信web开发者工具\cli.bat" auto-preview --project E:\Personal\sEMG_Project\mini_program

# 打开项目
& "D:\Program Files\微信web开发者工具\cli.bat" open --project E:\Personal\sEMG_Project\mini_program
```

## 小程序日志转发链路
```
小程序 log/warn/error (logger.js)
  → 直接 wx.request POST（主通道，已验证稳定）
  → 同时 console.log → app.js拦截 → _forwardLog（备用，缓冲机制Tab切换时丢日志）
  → http://192.168.137.1:9876/log
  → mini_log_server.py → mini_log.txt + 控制台
```
- `LOG_ENABLED = true` 时转发，发布前改为 `false`
- **logger.js 用直接wx.request**，不依赖app.js缓冲转发

## 设备信息

| 项目 | 值 |
|------|-----|
| 设备 IP | 动态DHCP（近期: 192.168.137.234） |
| WebSocket 端口 | 8888 |
| 电脑 IP | 192.168.137.1（日志服务器） |
| 手机WiFi热点 | LT02 |
| 串口 | COM4 / 115200 |
| pio.exe | `C:\Users\honghuang\.platformio\penv\Scripts\pio.exe` |

## UNO R4 WiFi 特殊行为
- 1200bps open/close = 进入 bootloader，不是软复位！
- 开机信息捕获靠固件 `delay(3000)` + 脚本0延迟连接
- COM4 被脚本占用时 PlatformIO 无法同时监控

---
