# Phase 1 小程序拆分写入完成

## 写入的 10 个文件

| 文件 | 操作 | 大小 |
|------|------|------|
| app.json | 重写 | 960B |
| utils/storage.js | 新建 | 1.3KB |
| pages/calibrate/index.js | 重构 | 11.7KB |
| pages/calibrate/index.wxml | 重构 | 7.3KB |
| pages/calibrate/index.wxss | 重构 | 3.8KB |
| pages/calibrate/form.json | 新建 | 64B |
| pages/database/index.js | 新建 | 7.2KB |
| pages/database/index.wxml | 新建 | 6.5KB |
| pages/database/index.wxss | 新建 | 4.2KB |
| pages/database/form.json | 新建 | 62B |

## 核心设计
- **TabBar 5项**: 校准 | 建库 | 实时监测 | 数据分析 | 网络配置
- **校准页**: 用户profile(2槽位切换) → REST 10s → MAX 15s → 结果验证 → 曲线拟合(自动匹配)
- **建库页**: 新建档案(5min采集+4段标记) / 记录列表(B区) / 曲线库(B区) / 原始数据导出
- **storage.js**: current_user + slot0/slot1 profile 本地持久化，getAgeGroup() 映射
- **曲线匹配**: gender + age_group(4档) + handedness → 自动精确匹配 → save_curve

## 待完成
1. 固件 A区V1.2（has_curve + curve_coef字段）
2. 固件 save_curve 命令处理
3. bleClient.js 添加缺少的命令（list_curves, db_*, delete_record 等）
4. 实时/分析页改用 has_curve 判断算法
5. 小程序编译预览码验证
