# 多页面数据流冲突修复 - 全局模式管理

## 问题背景

realtime 和 calibrate 页面同时注册 TCP 消息监听器并发送 `start` 命令，导致固件状态混乱、校准数据采集失败。

## 解决方案：全局模式管理

让 `app.js` 作为协调者，每个页面只在"自己的模式"下工作。

### 核心机制

```
app.js 全局状态：
  dataMode: 'idle' | 'realtime' | 'calibrate' | 'analysis'

页面切换规则：
  1. 页面 onShow() → 设置对应模式 → 清理旧监听器 → 延迟发送 start → 注册新监听器
  2. 页面 onHide() → 清理监听器
  3. 页面 onUnload() → 清理监听器
```

## 已完成修改

### 1. app.js — 添加全局模式管理 ✅

```javascript
dataMode: 'idle',

setDataMode(newMode) {
  const prevMode = this.dataMode;
  if (prevMode === newMode) return prevMode;
  
  // 停止之前的数据流
  if (prevMode !== 'idle' && this.tcp) {
    this.tcp.send('{"cmd":"stop"}').catch(() => {});
  }
  
  this.dataMode = newMode;
  console.log(`[App] 数据模式: ${prevMode} → ${newMode}`);
  return prevMode;
}
```

### 2. realtime/index.js ✅

- onShow(): `app.setDataMode('realtime')` → 清理监听器 → 延迟200ms发送start → 注册监听器
- onHide(): 清理监听器（不发送stop，让下一页面决定）
- onUnload(): 清理监听器

### 3. calibrate/index.js ✅

- onShow(): `app.setDataMode('calibrate')` → 清理监听器 → 连接检查
- startCalibration(): 再次确认模式 → 清理监听器 → 发送start
- onHide(): 清理监听器（不发送stop）
- onUnload(): 清理监听器 + 发送stop

### 4. analysis/index.js ✅

- onShow(): `app.setDataMode('analysis')` → 清理监听器 → 静默连接
- onHide(): 清理监听器

### 5. pages/index/index.js (网络配置页) ✅

- onShow(): `app.setDataMode('idle')` → 不接收数据流

## 验证要点

1. 切换页面时日志显示模式切换（如 `[App] 数据模式: realtime → calibrate`）
2. 切换到新页面后，旧页面的监听器不再触发
3. 校准流程能正常接收数据
4. 实时监测页面切换后能正常显示数据

## 文件状态

- `app.js` ✅ 已有 dataMode 和 setDataMode
- `pages/realtime/index.js` ✅ 已修改
- `pages/calibrate/index.js` ✅ 已修改
- `pages/analysis/index.js` ✅ 已修改
- `pages/index/index.js` ✅ 已修改
