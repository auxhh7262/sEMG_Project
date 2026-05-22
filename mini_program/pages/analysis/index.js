const wifiClient = require('../../utils/wifiClient');
const { log, warn } = require('../../utils/logger');
const storage = require('../../utils/storage');
const { getCurrentUser } = storage;

Page({
  data: {
    isConnected: false,
    isLoading: false,
    isEmpty: true,
    hasCurve: false,
    algorithm: '--',       // A区是否有拟合曲线
    errorMsg: '',

    // 时间筛选
    dateRange: [],
    selectedRange: 'today',
    startDate: '',
    endDate: '',

    // 统计
    summary: { avgRms: '--', avgMdf: '--', avgFatigue: '--', maxFatigue: '--', duration: '--' },

    // 图表数据
    recentRecords: [],
    totalRecords: 0,
    _chartPoints: [],      // C区实测数据 [{ts, rms, mdf, fatigue}]
    _refCurve: [],         // B区参考曲线 [{t, mdf}]
    _personalCurve: [],    // A区个人拟合曲线 [{t, mdf}]
  },

  _chartCtx: null, _chartDpr: 1, _chartW: 0, _chartH: 0,
  _tabVisible: false,

  onLoad() { this._initDateRange(); this._initCanvas(); },
  onUnload() {},

  onShow() {



    console.log('[analysis] onShow');
    this._tabVisible = true;
    if (wifiClient.isConnected()) {
      this.setData({ isConnected: true });
    } else {
      this._silentConnect();
    }
  },

  onHide() {
    this._tabVisible = false;
  },

  // ──── 连接 ────

  _silentConnect() {
    const ip = wx.getStorageSync('device_ip');
    const port = wx.getStorageSync('device_port') || 8888;
    if (!ip) { this.setData({ isConnected: false }); return; }
    this.setData({ isConnected: false });
    let retry = 0;
    const tryConn = () => {
      if (!this._tabVisible || retry >= 5) return;
      retry++;
      wifiClient.connect(ip, port).then(() => {
        this.setData({ isConnected: true });
      }).catch(() => setTimeout(tryConn, 2500));
    };
    setTimeout(tryConn, 500);
  },

  // ──── 查询算法类型 + 加载数据 ────

  async _queryAlgorithmAndLoad() {
    try {
      // 先加载当前用户到设备，确保查询的是对应用户的数据
      const user = getCurrentUser();
      if (user) {
        await wifiClient.sendCmd('load_user', { slot: user.slot }).catch(() => {});
      }
      const res = await wifiClient.sendCmd('query_cz', {});
      if (res) {
        if (res.has_curve) {
          this.setData({ hasCurve: true, algorithm: '个性化曲线' });
        } else if (res.ref_mdf && res.ref_mdf > 0) {
          this.setData({ hasCurve: false, algorithm: '默认MDF下降率' });
        } else {
          this.setData({ hasCurve: false, algorithm: '无校准' });
        }
      }
    } catch (e) { /* 非关键，继续加载 */ }
  },

  // ──── 日期范围 ────

  _initDateRange() {
    const now = new Date();
    const fmt = d => `${d.getFullYear()}-${String(d.getMonth()+1).padStart(2,'0')}-${String(d.getDate()).padStart(2,'0')}`;
    const today = fmt(now);
    const yesterday = fmt(new Date(now - 864e5));
    const week = fmt(new Date(now - 864e5 * 7));
    const month = fmt(new Date(now - 864e5 * 30));
    this.setData({
      dateRange: [
        { key:'today', label:'今天', start:today, end:today },
        { key:'yesterday', label:'昨天', start:yesterday, end:yesterday },
        { key:'week', label:'最近7天', start:week, end:today },
        { key:'month', label:'最近30天', start:month, end:today },
      ],
      selectedRange: 'today', startDate: today, endDate: today,
    });
  },

  // ──── 数据查询 ────

  async _loadData() {
    if (this.data.isLoading) return;
    const { startDate, endDate } = this.data;
    this.setData({ isLoading: true, errorMsg: '', isEmpty: false });
    try {
      if (!wifiClient.isConnected()) return;
      const startTs = Math.floor(new Date(startDate + 'T00:00:00').getTime() / 1000);
      const endTs = Math.floor(new Date(endDate + 'T23:59:59').getTime() / 1000);

      // 查询C区历史数据
      const czRes = await wifiClient.sendCmd('query_cz', { start_ts: startTs, end_ts: endTs });
      const records = this._parseRecords(czRes);

      if (!records || records.length === 0) {
        this.setData({ isLoading: false, isEmpty: true, errorMsg: '暂无监测数据', recentRecords: [], totalRecords: 0 });
        return;
      }

      // TODO: 查询B区参考曲线 (等拟合算法确定后实现)
      // const bRes = await wifiClient.sendCmd('query_curve', { source: 'B' });

      // TODO: 查询A区个人拟合曲线 (has_curve=true时)
      // if (this.data.hasCurve) { ... }

      this._processAndRender(records);
    } catch (e) {
      warn('[analysis] _loadData:', e.message);
      this.setData({ isLoading: false, isEmpty: true, errorMsg: e.message });
    }
  },

  _parseRecords(res) {
    if (res && res.cmd === 'cz_data' && Array.isArray(res.points)) {
      return res.points.map(pt => ({
        ts: Math.floor(pt.ts / 1000),
        rms: pt.rms || 0,
        mdf: pt.mdf || 0,
        fatigue: Math.round((pt.f || 0) * 100),
      }));
    }
    if (Array.isArray(res?.records)) return res.records;
    return [];
  },

  _processAndRender(records) {
    const summary = this._calcSummary(records);
    const recent = records.slice(-10).reverse().map(r => ({
      ...r,
      timeStr: new Date(r.ts * 1000).toLocaleTimeString('zh-CN', { hour12: false, hour:'2-digit', minute:'2-digit', second:'2-digit' }),
      rms: (r.rms || 0).toFixed(3),
      mdf: (r.mdf || 0).toFixed(1),
    }));

    this.setData({
      isLoading: false, isEmpty: false,
      summary,
      recentRecords: recent,
      totalRecords: records.length,
      _chartPoints: records,
    });

    // 延迟绘制确保canvas已就绪
    setTimeout(() => this._drawChart(), 100);
  },

  _calcSummary(records) {
    if (!records.length) return { avgRms:'--', avgMdf:'--', avgFatigue:'--', maxFatigue:'--', duration:'--' };
    const n = records.length;
    const rmsSum = records.reduce((s,r) => s + (r.rms||0), 0);
    const mdfSum = records.reduce((s,r) => s + (r.mdf||0), 0);
    const fatSum = records.reduce((s,r) => s + (r.fatigue||0), 0);
    const maxFat = Math.max(...records.map(r => r.fatigue||0));
    const firstTs = records[0].ts, lastTs = records[n-1].ts;
    const durSec = lastTs - firstTs;
    const durStr = durSec >= 3600 ? `${Math.floor(durSec/3600)}h${Math.round((durSec%3600)/60)}m`
                 : durSec >= 60 ? `${Math.floor(durSec/60)}min` : `${durSec}s`;
    return {
      avgRms: (rmsSum/n).toFixed(3),
      avgMdf: (mdfSum/n).toFixed(1),
      avgFatigue: Math.round(fatSum/n),
      maxFatigue: Math.round(maxFat),
      duration: durStr,
    };
  },

  // ──── Canvas 初始化 & 绘图 ────

  _initCanvas() {
    const query = wx.createSelectorQuery();
    query.select('#mdfChart').fields({ node:true, size:true }).exec(res => {
      if (!res[0]?.node) return;
      const c = res[0].node;
      this._chartCtx = c.getContext('2d');
      this._chartDpr = wx.getSystemInfoSync().pixelRatio;
      c.width = res[0].width * this._chartDpr;
      c.height = res[0].height * this._chartDpr;
      this._chartCtx.scale(this._chartDpr, this._chartDpr);
      this._chartW = res[0].width;
      this._chartH = res[0].height;
    });
  },

  _drawChart() {
    const ctx = this._chartCtx, w = this._chartW, h = this._chartH;
    if (!ctx || w === 0) return;

    const pts = this.data._chartPoints;
    if (!pts || pts.length < 2) {
      ctx.clearRect(0, 0, w, h);
      ctx.fillStyle = '#666'; ctx.font = '14px sans-serif'; ctx.textAlign = 'center';
      ctx.fillText('数据不足，无法绘制图表', w/2, h/2);
      return;
    }

    // 计算MDF范围
    const mdfs = pts.map(p => p.mdf);
    const mdfMin = Math.floor(Math.min(...mdfs) / 10) * 10;
    const mdfMax = Math.ceil(Math.max(...mdfs) / 10) * 10;
    const mdfRange = Math.max(mdfMax - mdfMin, 10);

    // 绘图区域（留边距给轴标签）
    const pad = { top: 10, right: 10, bottom: 30, left: 45 };
    const cw = w - pad.left - pad.right;
    const ch = h - pad.top - pad.bottom;

    const toX = (i, total) => pad.left + (i / Math.max(total - 1, 1)) * cw;
    const toY = (mdf) => pad.top + (1 - (mdf - mdfMin) / mdfRange) * ch;

    ctx.clearRect(0, 0, w, h);

    // 网格线
    ctx.strokeStyle = '#1e1e1e'; ctx.lineWidth = 0.5;
    for (let i = 0; i <= 4; i++) {
      const y = pad.top + (ch / 4) * i;
      ctx.beginPath(); ctx.moveTo(pad.left, y); ctx.lineTo(w - pad.right, y); ctx.stroke();
    }

    // Y轴刻度
    ctx.fillStyle = '#666'; ctx.font = '10px monospace'; ctx.textAlign = 'right';
    for (let i = 0; i <= 4; i++) {
      const val = mdfMax - (mdfRange / 4) * i;
      const y = pad.top + (ch / 4) * i;
      ctx.fillText(val.toFixed(0), pad.left - 5, y + 3);
    }

    // X轴时间标签
    ctx.textAlign = 'center';
    const n = pts.length;
    const tickCount = Math.min(5, n);
    for (let i = 0; i < tickCount; i++) {
      const idx = Math.floor(i * (n - 1) / (tickCount - 1));
      const x = toX(idx, n);
      const t = new Date(pts[idx].ts * 1000);
      const label = `${String(t.getHours()).padStart(2,'0')}:${String(t.getMinutes()).padStart(2,'0')}`;
      ctx.fillText(label, x, h - 8);
    }

    // TODO: B区参考曲线（虚线）
    // 当有B区曲线数据时绘制
    // ctx.setLineDash([4, 4]);
    // ctx.strokeStyle = '#60a5fa'; ctx.lineWidth = 1.5;
    // ...

    // TODO: A区个人拟合曲线（实线）
    // if (this.data.hasCurve && this.data._personalCurve.length > 0) {
    //   ctx.setLineDash([]);
    //   ctx.strokeStyle = '#07c160'; ctx.lineWidth = 2;
    //   ...
    // }

    // C区实测数据（散点连线 + 圆点）
    ctx.setLineDash([]);
    ctx.strokeStyle = 'rgba(248,113,113,0.6)'; ctx.lineWidth = 1.5;
    ctx.beginPath();
    for (let i = 0; i < n; i++) {
      const x = toX(i, n), y = toY(pts[i].mdf);
      i === 0 ? ctx.moveTo(x, y) : ctx.lineTo(x, y);
    }
    ctx.stroke();

    // 填充区域
    ctx.beginPath();
    ctx.moveTo(toX(0, n), toY(pts[0].mdf));
    for (let i = 1; i < n; i++) ctx.lineTo(toX(i, n), toY(pts[i].mdf));
    ctx.lineTo(toX(n-1, n), pad.top + ch);
    ctx.lineTo(toX(0, n), pad.top + ch);
    ctx.closePath();
    ctx.fillStyle = 'rgba(248,113,113,0.08)'; ctx.fill();

    // 数据点
    const step = Math.max(1, Math.floor(n / 30)); // 最多画30个点避免密集
    for (let i = 0; i < n; i += step) {
      const x = toX(i, n), y = toY(pts[i].mdf);
      ctx.beginPath(); ctx.arc(x, y, 3, 0, Math.PI * 2);
      ctx.fillStyle = '#f87171'; ctx.fill();
      ctx.strokeStyle = 'rgba(248,113,113,0.3)'; ctx.lineWidth = 1; ctx.stroke();
    }
    // 最后一个点必画
    if (n > 1) {
      const lx = toX(n-1, n), ly = toY(pts[n-1].mdf);
      ctx.beginPath(); ctx.arc(lx, ly, 4, 0, Math.PI * 2);
      ctx.fillStyle = '#fff'; ctx.fill();
      ctx.strokeStyle = '#f87171'; ctx.lineWidth = 2; ctx.stroke();
    }

    ctx.setLineDash([]);
  },

  // ──── 事件 ────

  onRangeChange(e) {
    const key = e.currentTarget.dataset.key;
    if (!key) return;
    const item = this.data.dateRange.find(r => r.key === key);
    if (!item) return;
    this.setData({ selectedRange: key, startDate: item.start, endDate: item.end });
  },

  // 点击查询按钮：发指令加载数据
  async onQuery() {
    if (!wifiClient.isConnected()) {
      wx.showToast({ title: '请先连接设备', icon: 'none' });
      return;
    }
    this._queryAlgorithmAndLoad();
  },

  onRefresh() { this._loadData(); },

  async onExportData() {
    const pts = this.data._chartPoints;
    if (!pts || !pts.length) { wx.showToast({ title:'无数据', icon:'none' }); return; }
    const header = 'timestamp,datetime,RMS(mV),MDF(Hz),Fatigue(%)\n';
    const rows = pts.map(r => `${r.ts},${new Date(r.ts*1000).toLocaleString()},${r.rms},${r.mdf},${r.fatigue}`).join('\n');
    const csv = header + rows;
    const name = `sEMG_${this.data.startDate}_${this.data.endDate}.csv`;
    const path = `${wx.env.USER_DATA_PATH}/${name}`;
    try {
      wx.getFileSystemManager().writeFileSync(path, csv, 'utf-8');
      wx.shareFileMessage({ filePath: path });
    } catch (e) {
      wx.showToast({ title:'导出失败', icon:'none' });
    }
  },
});
