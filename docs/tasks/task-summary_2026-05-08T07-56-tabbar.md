# TabBar 底部导航改造 — 实施记录

## 目标
将小程序从「页面跳转模式」（index → navigateTo → realtime/calibrate）改为 **底部 TabBar 模式**

## 架构决策（已确认）
- **配网Tab**：复用现有 index 逻辑（连接状态 + WiFi配网）
- **校准Tab**：快速校准（A区）+ 建库管理（B区）
- **实时Tab**：实时监测（Tab切换进入发start，切走发stop）
- **分析Tab**：C区历史数据查询
- **未连接处理**：静默重连，超时显示未连接遮罩（不强制跳转）
- **TabBar样式**：emoji图标 + 绿色选中态 (#10b981)

## 改动文件清单

### 新增
- `components/custom-tab-bar/index.js` — TabBar组件逻辑
- `components/custom-tab-bar/index.json` — 组件声明
- `components/custom-tab-bar/index.wxml` — TabBar模板（cover-view）
- `components/custom-tab-bar/index.wxss` — 深色主题样式

### 修改
- `app.json` — 新增 `tabBar.custom=true` + 4个Tab配置
- `app.js` — 新增 `getTabBar()` 方法供全局调用

### 页面 JS 改造
| 页面 | 关键变化 |
|------|---------|
| `realtime/index.js` | `onLoad`→注册；`onShow`→静默重连+发start；`onHide`→发stop；移除`stopAndExit`；`goToNetwork`切换Tab |
| `calibrate/index.js` | 移除所有`wx.navigateBack`→`wx.switchTab`；校准成功跳实时Tab；建库保存后留在校准Tab |
| `analysis/index.js` | `onShow`→检查连接；移除`navigateBack`；加`goToNetwork` |

### 页面模板/样式
| 文件 | 关键变化 |
|------|---------|
| `realtime/index.wxml` | 移除「停止并退出」按钮；加连接丢失遮罩；加4条迷你趋势线 |
| `realtime/index.wxss` | 适配spark-row布局；加断连遮罩样式；移除旧chart-block |
| `calibrate/index.wxss` | 加底部100rpx+安全区留白 |
| `analysis/index.wxml` | 加「前往配网」按钮（未连接时） |
| `analysis/index.wxss` | 加底部tabBar留白 |

## TabBar 组件说明
- 使用 `cover-view`（微信官方推荐，可覆盖原生组件）
- `attached()` 时通过 `getCurrentPages()` 自动检测当前路由并高亮对应Tab
- `switchTab` 直接调用 `wx.switchTab`，由小程序自动管理页面栈

## 下次继续
- 微信开发者工具 CLI 编译验证（需用户手动开启服务端口）
- 上机测试（等待 TXB0108 电平转换器）
