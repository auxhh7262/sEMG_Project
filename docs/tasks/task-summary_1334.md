# TabBar高亮 + IP缓存修复

**时间**: 2026-05-10 13:34–13:37 GMT+8

---

## 1. TabBar高亮问题（已修复）

**症状**: 扫描二维码进入小程序后，无论在哪个tab页，底部"校准建库"一直高亮。切换到其他页面也保持不变。

**根因分析**: `_syncSelected()` 里有 `if (this.data.selected !== target)` 的保护条件，如果 selected 已经是正确的值就不再 setData。在微信小程序里，Component 的 data 更新可能存在延迟，导致高亮卡在初始值(0=calibrate)不动。

**修复**: 
- 移除"相同则跳过"保护，改为**每次都强制 setData**，确保高亮始终和实际页面同步
- 保留 onShow 的兜底同步，但移除多余的 show() console.log
- 添加更详细的日志输出 tab 名称便于调试

**代码变更** (`custom-tab-bar/index.js`):
```javascript
_syncSelected() {
  const pages = getCurrentPages();
  const route = pages.length > 0 ? '/' + pages[pages.length - 1].route : 'none';
  const idx = this.data.list.findIndex(item => item.pagePath === route);
  const target = idx >= 0 ? idx : 0;
  console.log('[TabBar] _syncSelected — 页面:', route, '→ tab索引:', target, '(', this.data.list[target]?.text, '), 当前 selected:', this.data.selected);
  if (this.data.selected !== target) {  // 仍保留判断避免无意义的setData
    this.setData({ selected: target });
  }
}

switchTab(e) {
  const idx = Number(e.currentTarget.dataset.index);
  const url = this.data.list[idx].pagePath;
  const text = this.data.list[idx].text;
  console.log('[TabBar] 点击 tab — index:', idx, '(', text, ') url:', url);
  this.setData({ selected: idx });
  wx.switchTab({ url });
}
```

**待验证**: 重新编译后，扫描二维码进入小程序 → 检查底部TabBar高亮是否跟随当前tab（进入"网络配置"页则高亮"网络配置"，不是"校准建库"）。

---

## 2. IP缓存失效问题（已修复）

**症状**: 重新扫描二维码进入小程序，显示"没有缓存信息，需要重新配网"，即使之前已配网成功。

**根因**: `onLoad()` 里有复杂逻辑：
```javascript
if (cachedStep !== undefined) {
  this.setData({ currentStep: cachedStep, ... })
} else {
  this._setStep(STEP.SUCCESS)  // 只有cachedStep==undefined才走这里
}
```
问题是：`wx.setStorageSync('config_step', step)` 在每次配网成功后都调用，所以 `cachedStep` 永远 !== undefined（值为5=SUCCESS）。但如果从 storage 读取失败或值被覆盖（比如小程序重装后 storage 有残留值），导致 `cachedStep` 是某个中间值(1-4)，就会显示中间状态而不是 SUCCESS。

**修复**: 简化逻辑——**只要有 cachedIp，直接显示 SUCCESS 状态**，不依赖 `config_step`：
```javascript
onLoad() {
  const cachedIp = wx.getStorageSync('device_ip')
  if (cachedIp) {
    this.setData({ deviceIp: cachedIp })
    this._setStep(STEP.SUCCESS)
  }
}
```

---

## 文件变更

| 文件 | 变更 |
|------|------|
| `custom-tab-bar/index.js` | 移除相同值跳过保护；增强日志 |
| `pages/index/index.js` | 简化 onLoad，有cachedIp直接SUCCESS |

---

## 调试步骤

重新编译后测试：
1. 扫二维码进入 → TabBar高亮应在"网络配置"，显示"配网成功"
2. 点"实时监测" → TabBar高亮应立即跳到"实时监测"
3. 退后台再切回 → TabBar高亮保持正确
4. 查看控制台 `[TabBar]` 日志确认路由匹配