#!/usr/bin/env node
/**
 * 微信小程序Console日志读取器 v4
 * 使用Node.js内置模块实现WebSocket，无需第三方依赖
 * 通过CDP协议连接微信开发者工具
 * 
 * 用法: node console_reader.js
 * 输出: 写入 logs/mini_console.txt
 */

const http = require('http');
const crypto = require('crypto');
const fs = require('fs');
const path = require('path');
const net = require('net');

const LOG_FILE = path.join(__dirname, '..', 'logs', 'mini_console.txt');

fs.mkdirSync(path.dirname(LOG_FILE), { recursive: true });
const startTime = new Date().toISOString();
fs.writeFileSync(LOG_FILE, `=== Console Reader Started ${startTime} ===\n`, 'utf-8');

function httpGet(url) {
  return new Promise((resolve, reject) => {
    http.get(url, res => {
      let data = '';
      res.on('data', chunk => data += chunk);
      res.on('end', () => resolve(data));
    }).on('error', reject);
  });
}

function connectWS(url) {
  return new Promise((resolve, reject) => {
    const parsed = new URL(url);
    const key = crypto.randomBytes(16).toString('base64');
    
    const socket = net.createConnection({ host: parsed.hostname, port: parseInt(parsed.port) }, () => {
      socket.write(
        `GET ${parsed.pathname} HTTP/1.1\r\n` +
        `Host: ${parsed.hostname}:${parsed.port}\r\n` +
        `Upgrade: websocket\r\n` +
        `Connection: Upgrade\r\n` +
        `Sec-WebSocket-Key: ${key}\r\n` +
        `Sec-WebSocket-Version: 13\r\n\r\n`
      );
    });

    let upgraded = false;
    let buffer = Buffer.alloc(0);
    const handlers = {};

    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      
      if (!upgraded) {
        const headerEnd = buffer.indexOf('\r\n\r\n');
        if (headerEnd === -1) return;
        upgraded = true;
        buffer = buffer.slice(headerEnd + 4);
      }

      while (buffer.length >= 2) {
        const opcode = buffer[0] & 0x0f;
        let payloadLen = buffer[1] & 0x7f;
        let offset = 2;

        if (payloadLen === 126) {
          if (buffer.length < 4) break;
          payloadLen = buffer.readUInt16BE(2);
          offset = 4;
        } else if (payloadLen === 127) {
          if (buffer.length < 10) break;
          payloadLen = Number(buffer.readBigUInt64BE(2));
          offset = 10;
        }

        if (buffer.length < offset + payloadLen) break;

        const payload = buffer.slice(offset, offset + payloadLen).toString('utf-8');
        buffer = buffer.slice(offset + payloadLen);

        if (opcode === 0x1 && handlers.message) handlers.message(payload);
        else if (opcode === 0x8) { socket.end(); if (handlers.close) handlers.close(); }
      }
    });

    socket.on('error', (err) => { if (handlers.error) handlers.error(err); });

    const ws = {
      on(event, cb) { handlers[event] = cb; },
      send(data) {
        const payload = Buffer.from(data, 'utf-8');
        const mask = crypto.randomBytes(4);
        let header;
        if (payload.length < 126) {
          header = Buffer.alloc(6);
          header[0] = 0x81;
          header[1] = 0x80 | payload.length;
          mask.copy(header, 2);
        } else if (payload.length < 65536) {
          header = Buffer.alloc(8);
          header[0] = 0x81;
          header[1] = 0x80 | 126;
          header.writeUInt16BE(payload.length, 2);
          mask.copy(header, 4);
        } else {
          header = Buffer.alloc(14);
          header[0] = 0x81;
          header[1] = 0x80 | 127;
          header.writeBigUInt64BE(BigInt(payload.length), 2);
          mask.copy(header, 10);
        }
        const masked_payload = Buffer.alloc(payload.length);
        for (let i = 0; i < payload.length; i++) masked_payload[i] = payload[i] ^ mask[i % 4];
        socket.write(Buffer.concat([header, masked_payload]));
      },
      close() { socket.end(); },
    };

    setTimeout(() => {
      if (upgraded) resolve(ws);
      else reject(new Error('WebSocket upgrade timeout'));
    }, 3000);
  });
}

async function main() {
  console.log('[console_reader] 查询调试页面...');
  
  const cdpPorts = [31629, 32123, 21221, 44840, 60780];
  let targets = null;
  
  for (const port of cdpPorts) {
    try {
      const data = await httpGet(`http://127.0.0.1:${port}/json`);
      targets = JSON.parse(data);
      console.log(`[console_reader] CDP端口: ${port}, ${targets.length} 个目标`);
      break;
    } catch (err) {}
  }
  
  if (!targets) {
    console.error('[console_reader] 无法连接CDP。请在微信开发者工具中：设置 → 安全设置 → 开启服务端口');
    process.exit(1);
  }

  const pageTarget = targets.find(t => t.type === 'page' || t.title.includes('小程序') || t.title.includes('mini'));
  const fallback = pageTarget || targets.find(t => t.webSocketDebuggerUrl);
  
  if (!fallback) {
    console.error('[console_reader] 未找到调试目标');
    process.exit(1);
  }

  console.log(`[console_reader] 目标: ${fallback.title || fallback.url}`);
  console.log(`[console_reader] WS: ${fallback.webSocketDebuggerUrl}`);

  const ws = await connectWS(fallback.webSocketDebuggerUrl);
  console.log('[console_reader] CDP连接成功！');

  let msgId = 0;
  function sendCDP(method, params = {}) {
    const id = ++msgId;
    ws.send(JSON.stringify({ id, method, params }));
    return id;
  }

  sendCDP('Runtime.enable');

  ws.on('message', (data) => {
    try {
      const msg = JSON.parse(data);
      
      if (msg.method === 'Runtime.consoleAPICalled') {
        const type = msg.params.type;
        const timestamp = msg.params.timestamp;
        const args = (msg.params.args || []).map(a => {
          if (a.value !== undefined) return String(a.value);
          if (a.description) return a.description;
          if (a.preview) return JSON.stringify(a.preview);
          return '';
        }).filter(Boolean).join(' ');
        
        const ts = new Date(timestamp).toTimeString().split(' ')[0];
        const level = type === 'warning' ? 'WARN' : type.toUpperCase();
        const line = `[${ts}] [${level}] ${args}`;
        
        fs.appendFileSync(LOG_FILE, line + '\n', 'utf-8');
        console.log(line);
      }
    } catch(e) {}
  });

  ws.on('error', (err) => console.error('[console_reader] WS错误:', err.message));
  ws.on('close', () => { console.log('[console_reader] 连接关闭'); process.exit(0); });

  process.on('SIGINT', () => { console.log('[console_reader] 关闭'); ws.close(); });
  console.log('[console_reader] 监听中... Ctrl+C 停止');
}

main().catch(err => {
  console.error('[console_reader] 错误:', err);
  process.exit(1);
});
