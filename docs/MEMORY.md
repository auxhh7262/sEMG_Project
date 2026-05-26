# MEMORY.md - Long-Term Memory

## 2026-05-06

### 项目：sEMG固件 V1.4
- 位置：E:\Personal\sEMG_Project\firmware
- 平台：Arduino UNO R4 WiFi (RA4M1 48MHz Cortex-M4 **有FPU**)
- 硬件：思知瑞干电极sEMG传感器(20-500Hz, 放大1000倍, 0-3.0V输出/基准1.5V, 检测范围±1.5mV), 14-bit ADC(0-5V参考, ADC_REF_MV=5000已验证正确)
- 外存：W25Q128FVSG SPI Flash 16MB (2.7-3.6V, 注意5V电平转换!)
- RAM: 32KB SRAM, Flash: 256KB, Data Flash(EEPROM): 8KB
- WiFi凭证存RA4M1 EEPROM, 其余数据存SPI Flash
- 当前编译：RAM 60.1%, Flash 44.8%

### 已完成重构
1. StorageManager/FlashDriver/StorageArch去掉_V8后缀
2. FlashDriver双层合并(删除1_Signal/Hal_FlashDriver，合并入3_Storage/FlashDriver)
3. SignalProcessor 8项优化(ISR拆分、snapshot防撕裂、掩码优化、RMS+MDF合并遍历、imag_buffer改成员、MDF频段限250Hz、评分修正满分100、MDF恢复加速)
4. S0-1修复：StorageManager::Init()不在栈上分配8KB
5. A0-1修复：校准状态机(CalibRest/CalibMax)实现
6. A0-2修复：update短路(FFT未就绪时跳过)
7. C0-1修复：MDF反向检查容忍度从1.5倍调整

### ⚠️ 重要更正
- ### 已确认
- RA4M1 有硬件FPU（Cortex-M4F），浮点FFT无需软件模拟
- **⚠️ UNO R4 WiFi GPIO输出5V**（板载电平转换），非RA4M1原生3.3V！W25Q128FVSG需电阻分压（MOSI/SCK/CS），MISO可直接连接
- 之前记录"RA4M1实际3.3V可直接连W25Q128FVSG"是**错误的**，已更正
- 固件SignalProcessor已完成8项优化（v4.1.0）
- 疲劳度算法改为MDF-only，不再用RMS参与疲劳计算
- 放大倍数已更正：文档明确1000倍（之前错误记录为1500倍）
- 传感器输出0-3.0V（之前错误记录为±2.25V），基准1.5V
- ADC参考电压5V已通过DCbias=1589mV≈1.5V验证正确
- P0~P2全部修复完成（20项），可上机测试

### 待修复问题

### 已修复（2026-05-10 16:10）
- ✅ **ADC定时器中断终于工作！** 根因：FspTimer API使用错误
  - `get_available_timer()` 返回int8_t通道号，必须用变量接收返回值
  - 正确用法：`int8_t ch = FspTimer::get_available_timer(type)`
  - 中断配置：`setup_overflow_irq(12, nullptr)` 使用FSP默认ISR
  - 调用顺序：`begin()` → `setup_overflow_irq()` → `open()` → `start()`
- ✅ WebSocket数据流正常：RMS=1537-1543, MDF=40.7Hz, 8包/秒

### 待验证
- 校准流程完整测试（`start`命令 → 状态机流转 → 数据推送）

### 存储架构
- 详见 **《存储分区详解.md》**
- A区(0x000000-0x001FFF)：2扇区×4KB个人档案，双缓冲轮转写
- B区(0x002000-0x1CFFFF)：49槽×40KB + 曲线库32KB（63条曲线）
- C区(0x200000-0xFFFFFF)：实时监控数据，12字节/点，64KB块管理
### 文档
- docs/ABX00087_Arduino_UNO_R4_WiFi_Datasheet.pdf
- docs/W25Q128FV_Datasheet.pdf
- ### 已修复
- 校准验证：MDF必须下降≥10%（肌肉疲劳生理特征）
- 疲劳度 = MDF-only（1.0 - current_mdf / calib_rest_mdf）
- 异步擦除状态机 + g_workBuf共享缓冲（4KB）
- ### 已完成重构（P0~P2全部20项）
- SignalProcessor 8项优化（v4.1.0）
- StorageManager完整实现（A/B/C三区异步擦除状态机）
- 校准状态机、MDF-only疲劳度算法
- BleConfigServer修复、NetManager JSON缓冲区扩展
- Logger栈缓冲改为全局静态缓冲
- ### 状态
- P0~P2全部修复完成
- WiFi配网页面已完成（小程序端 + 固件端逻辑验证）
- 当前编译：RAM 63.6% (20848/32768), Flash 48.0% (125764/262144)
- 等待硬件：电阻分压电路（UNO R4 WiFi GPIO输出5V vs W25Q128FVSG 3.3V，**电阻分压必须**）
- 待测试：WiFi配网页面真机测试 + COM4串口占用问题解决
- 校准流程已完成验证：固件逻辑正确，传感器悬空时校准验证失败（预期行为），需贴人体测试
- 校准验证5条规则全部正确：RMS>0, ref>2x_rest, ref>0.5mV, rest_mdf 30-200Hz, ref_mdf<rest_mdf*0.9
- 8888端口为WebSocket协议（非raw TCP），测试时必须用WebSocket客户端
- 当前编译：RAM 64.1% (21008/32768), Flash 57.5% (150672/262144)

## 当前项目与关注

- 微信小程序代码位置：E:\Personal\sEMG_Project\mini_program，用于与sEMG设备WiFi通信
- WiFi配网页面：E:\Personal\sEMG_Project\mini_program\pages\wifi-config\（5步向导：扫描→连接→发送凭证→等待IP→连接成功）
- 设置页入口：pages/index/index.wxml 中的 wifi-config-card 卡片
- **设备IP：192.168.137.253**（WebSocket端口8888）
- 疲劳度算法改为MDF-only：fatigue = 1.0 - (current_mdf / calib_rest_mdf)，放弃RMS参与疲劳计算（生理学依据：肌肉疲劳时MDF下降，RMS受收缩力度干扰方向不确定）
- RMS新用途：专用于收缩力度估算（activation = (current_rms - rest_rms) / (max_rms - rest_rms)），不再参与疲劳度计算

## 经验与决策

- 校准验证：要求最大收缩时MDF下降≥10%（ref_mdf < rest_mdf * 0.9），与肌肉疲劳时频谱向低频偏移的生理学特征一致
- TCP服务器启动条件：固件 `NetManager.init()` 中无条件启动 `_tcpServer.begin()`，不依赖WiFi → **设备开机后即可TCP连接**（无需等待WiFi连接成功）
- **FspTimer API教训**：
  1. `get_available_timer()` 返回int8_t通道号，必须用变量接收返回值
  2. `setup_overflow_irq(priority, nullptr)` 使用FSP默认ISR，它会正确调用 `begin()` 设置的回调
  3. 正确调用顺序：`begin()` → `setup_overflow_irq()` → `open()` → `start()`
  4. `open()` 已包含 `R_GPT_Enable()`，无需额外操作
