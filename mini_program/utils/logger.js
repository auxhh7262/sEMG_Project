/**
 * 统一日志模块 — 替代 console.log/warn/error
 * 同时输出到控制台 + 转发到日志服务器（通过 app.forwardLog）
 * 
 * 用法：
 *   const { log, warn, error } = require('../../utils/logger');
 *   log('[pagename] 消息');
 */

function _fwd(level, args) {
  try {
    const app = getApp();
    if (app && app.forwardLog) {
      app.forwardLog(level, args);
    }
  } catch (e) { /* ignore */ }
}

function _directSend(level, args) {
  try {
    const msg = args.map(v => v == null ? 'null' : typeof v === 'object' ? JSON.stringify(v) : String(v)).join(' ');
    wx.request({
      url: 'http://192.168.137.1:9876/log',
      method: 'POST',
      header: { 'content-type': 'application/json' },
      data: { logs: [{ level, msg, time: new Date().toLocaleTimeString('zh-CN', { hour12: false }) }] },
      fail() {}
    });
  } catch(e) {}
}

function log(...args) { console.log(...args); _directSend('log', args); }
function warn(...args) { console.warn(...args); _directSend('warn', args); }
function error(...args) { console.error(...args); _directSend('error', args); }

module.exports = { log, warn, error };
