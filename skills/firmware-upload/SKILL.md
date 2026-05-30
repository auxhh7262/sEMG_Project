---
name: firmware-upload
description: sEMG固件编译上传工具。当用户说"上传固件"、"编译上传"、"烧录固件"、"upload firmware"、"刷固件"、"重新烧录"时触发此skill。自动处理COM4端口占用问题，并确保串口监控能在固件重启后立即捕获完整的开机日志。包含检测代码改动、自动清理旧日志、杀占端口进程、顺序执行上传和监控启动。
---

# emg-firmware-upload

sEMG固件编译上传skill。解决COM4端口占用和开机日志丢失两大痛点。

## 工作流程

```
1. 杀COM4占用进程（python.exe / platformio.exe / pio.exe）
2. pio run -t upload --upload-port COM4（上传时串口监控关闭）
3. 上传完成后立即启动 serial_monitor.py（tkinter GUI + timestamped log）
4. 板子重启 → delay(3000)等待期间监控已就绪 → 完整捕获开机日志 ✅
```

## 关键时序保障

固件`main.cpp`设计了3秒开机等待：
```cpp
void setup() {
    delay(3000);  // 等串口监控连接
    Serial.begin(115200);
    LOG("\n\n========== BOOT ==========\n");  // 这时才输出日志
}
```

skill脚本在pio上传完成后立即启动监控（~0.5秒），此时板子还在delay(3000)中。监控就绪时BOOT日志刚开始输出，**不会漏掉任何日志**。

## 串口监控 GUI

`serial_monitor.py` 为 tkinter GUI 版本：

- **实时显示**：绿色终端风格界面，实时输出固件日志
- **精准关闭旧窗口**：按脚本名匹配，只杀同名旧进程，不影响其他窗口
- **时间戳文件名**：每次运行生成独立 log 文件
  - 格式：`logs/serial/serial_log_YYYYMMDD_HHMMSS.txt`
  - 例如：`serial_log_20260528_204500.txt`
- **颜色标签**：
  - 绿色 = INFO / 正常日志
  - 红色 = ERROR / 异常
  - 蓝色 = BOOT / 系统启动
  - 灰色 = DATA / 原始数据
- **控制栏**：📂打开日志目录按钮

## 脚本说明

| 脚本 | 作用 |
|------|------|
| `upload_and_monitor.py` | 完整流程：杀进程 → 上传 → 启动GUI |
| `serial_monitor.py` | 独立串口监控 GUI（也可单独运行） |

## 路径配置

| 配置项 | 值 |
|--------|-----|
| 固件目录 | `E:\Personal\sEMG_Project\firmware` |
| 串口日志目录 | `E:\Personal\sEMG_Project\logs\serial` |
| 串口日志格式 | `serial_log_YYYYMMDD_HHMMSS.txt` |
| 串口 | COM4 / 115200 |
| pio.exe | `C:\Users\honghuang\.platformio\penv\Scripts\pio.exe` |

## 使用方法

运行 `upload_and_monitor.py` 即可完成完整流程：
- 自动清理COM4占用进程
- 上传固件
- 启动串口监控 GUI
