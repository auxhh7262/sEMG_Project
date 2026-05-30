# semg-workflow Skill

sEMG 项目组合工作流 Skill，一键执行多个子任务。

## 触发词

### 工作流1：上传并编译（部署类）

**触发词（任选其一）：**
- `上传并编译`
- `完整上传`
- `一键部署`
- `部署全部`

**执行动作：**
1. 调用 `firmware-upload` skill → 上传固件 + 启动串口监控 GUI
2. 等待固件上传完成（检查串口日志确认重启成功）
3. 调用 `miniprogram-preview` skill → 编译小程序 + 启动日志服务器 GUI
4. 报告结果

**预期输出：**
```
✅ 固件上传成功
  - 串口监控 GUI 已启动
  - 固件启动正常（SPI Flash / WiFi / ADC）
✅ 小程序编译成功
  - 日志服务器 GUI 已启动
  - 预览已推送到手机
🎉 部署完成！两个 GUI 窗口正在运行...
```

---

### 工作流2：分析所有日志（调试类）

**触发词（任选其一）：**
- `分析所有日志`
- `完整分析`
- `全面调试`
- `分析全部`

**执行动作：**
1. 调用 `firmware-debug` skill → 读取最新串口日志 + 扫描问题
2. 调用 `miniprogram-debug` skill → 读取最新小程序日志 + 扫描问题
3. 合并两份报告，输出联合分析

**预期输出：**
```
📊 联合分析报告

【固件侧】
✅ 启动正常（Flash / WiFi / ADC）
⚠️ 串口日志行首丢失（已知问题）

【小程序侧】
🔴 WebSocket 连接 10 秒断开
🔴 WiFi 连接超时（errCode:1004）
🟡 BLE 扫描有时失败

【建议】
1. 优先处理 WebSocket 断连问题
2. 检查 WiFi 热点 LT02 是否稳定
```

---

## 使用方法

### 部署固件 + 编译小程序

**用户说：**
```
上传并编译
```

**AI 执行：**
1. 调用 `firmware-upload` skill（上传固件）
2. 等待固件重启完成
3. 调用 `miniprogram-preview` skill（编译小程序）
4. 报告部署结果

---

### 分析所有日志

**用户说：**
```
分析所有日志
```

**AI 执行：**
1. 调用 `firmware-debug` skill（分析固件日志）
2. 调用 `miniprogram-debug` skill（分析小程序日志）
3. 合并报告并输出

---

## 技术实现

### 调用方式

`workflow.py` 通过 `subprocess.run()` 直接调用其他 skill 的 Python 脚本：

```python
import subprocess
from pathlib import Path

SKILLS_DIR = Path(__file__).parent.parent

# 调用 firmware-upload
subprocess.run(
    ["python", SKILLS_DIR / "firmware-upload/upload_and_monitor.py"],
    cwd=str(SKILLS_DIR / "firmware-upload")
)
```

### 执行策略

- **顺序执行**（默认）：等第一个 skill 完成再执行第二个
  - 优点：稳定可靠，易调试
  - 缺点：慢（需要等固件上传完成才能编译小程序）
- **并行执行**（可选）：同时启动两个 GUI
  - 优点：快
  - 缺点：可能互相干扰（COM4 被占用等）

**当前使用顺序执行，稳定优先。**

---

## 文件结构

```
E:\Personal\sEMG_Project\skills\semg-workflow\
├── SKILL.md          # 本文件（触发词 + 使用说明）
└── workflow.py       # 编排脚本（调用其他 skill）
```

---

## 依赖 Skill

| Skill | 用途 | 必须 |
|-------|------|------|
| firmware-upload | 上传固件 + 串口监控 | ✅ |
| miniprogram-preview | 编译小程序 + 日志服务器 | ✅ |
| firmware-debug | 分析固件日志 | ✅ |
| miniprogram-debug | 分析小程序日志 | ✅ |

---

## 注意事项

1. **固件上传时间**：约 30-60 秒（取决于固件大小）
2. **小程序编译时间**：约 10-20 秒
3. **日志文件路径**：
   - 固件日志：`E:\Personal\sEMG_Project\logs\serial\serial_log_*.txt`
   - 小程序日志：`E:\Personal\sEMG_Project\logs\mini\mini_log_*.txt`
4. **GUI 窗口**：两个工作流都会启动 GUI 窗口，确保屏幕上有足够空间

---

## 示例对话

**用户：** 上传并编译

**AI：**
```
收到！开始执行「上传并编译」工作流...

[1/2] 上传固件...
✅ 固件上传成功
  - 串口监控 GUI 已启动（PID 12345）
  - 固件启动正常（SPI Flash / WiFi / ADC）

[2/2] 编译小程序...
✅ 小程序编译成功
  - 日志服务器 GUI 已启动（PID 12346）
  - 预览已推送到手机

🎉 部署完成！两个 GUI 窗口正在运行...
```

---

## 扩展计划

未来可能添加的工作流：

- **工作流3：完整推送** → 上传代码 + git push + 告诉你 push 结果
- **工作流4：一键调试** → 上传固件 + 打开所有日志 + 实时显示数据
- **工作流5：打包发布** → 编译固件 + 编译小程序 + 生成 Release Notes + git push --tag

---

## 版本历史

| 版本 | 日期 | 变更 |
|------|------|------|
| v1.0 | 2026-05-30 | 初始版本，支持2个工作流 |
