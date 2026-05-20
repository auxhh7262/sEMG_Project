#!/usr/bin/env python3
"""
小程序日志转发服务器
接收小程序 app.js 通过 wx.request() 发送的 console.log 日志
写入 logs/mini_log.txt 并在控制台实时显示

端口: 60780
接口: POST /log

启动: python mini_log_server.py
停止: Ctrl+C
"""

import http.server
import json
import os
import sys
import threading
from datetime import datetime

# ========== 配置 ==========
PORT = 9876
LOG_DIR = os.path.dirname(os.path.abspath(__file__))
LOG_FILE = os.path.join(LOG_DIR, 'mini_log.txt')

# 确保日志目录存在
os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)

# 过滤关键词（不写入日志文件）
FILTER_KEYWORDS = ['LogForward', 'heartbeat', 'ping', 'pong']

# 日志文件锁
file_lock = threading.Lock()


def should_filter(msg):
    """判断是否应过滤该日志"""
    for kw in FILTER_KEYWORDS:
        if kw in msg:
            return True
    return False


def write_log(level, msg, timestamp=None):
    """写入日志文件并打印到控制台"""
    if should_filter(msg):
        return

    ts = timestamp or datetime.now().strftime('%H:%M:%S')
    line = f"[{ts}] [{level}] {msg}"

    # 控制台输出
    print(line, flush=True)

    # 文件写入
    with file_lock:
        with open(LOG_FILE, 'a', encoding='utf-8') as f:
            f.write(line + '\n')


class LogHandler(http.server.BaseHTTPRequestHandler):
    """处理小程序日志请求"""

    def do_POST(self):
        if self.path != '/log':
            self.send_response(404)
            self.end_headers()
            return

        try:
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)

            # 解析JSON
            data = json.loads(body.decode('utf-8'))

            # 支持批量日志（数组格式）
            if isinstance(data, list):
                logs = data
            elif isinstance(data, dict):
                # 单条日志
                if 'logs' in data:
                    logs = data['logs']
                elif 'level' in data and 'msg' in data:
                    logs = [data]
                else:
                    logs = [{'level': 'INFO', 'msg': json.dumps(data, ensure_ascii=False)}]
            else:
                logs = [{'level': 'INFO', 'msg': str(data)}]

            # 写入每条日志
            for log_entry in logs:
                level = log_entry.get('level', 'INFO').upper()
                msg = log_entry.get('msg', '')
                ts = log_entry.get('time', None)
                write_log(level, msg, ts)

            # 返回成功
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(json.dumps({'ok': True}).encode('utf-8'))

        except json.JSONDecodeError:
            self.send_response(400)
            self.end_headers()
        except Exception as e:
            print(f"[ERROR] 处理日志请求失败: {e}", flush=True)
            self.send_response(500)
            self.end_headers()

    def do_OPTIONS(self):
        """处理CORS预检请求"""
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self):
        """健康检查"""
        if self.path == '/health':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({'status': 'ok', 'port': PORT}).encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        """打印HTTP请求日志（调试用）"""
        print(f"[HTTP] {self.client_address[0]} - {format % args}", flush=True)


def main():
    # 清空旧日志（可选）
    with open(LOG_FILE, 'w', encoding='utf-8') as f:
        f.write(f"=== Mini Log Server Started {datetime.now().isoformat()} ===\n")

    server = http.server.HTTPServer(('0.0.0.0', PORT), LogHandler)
    print(f"[mini_log_server] 监听 0.0.0.0:{PORT}")
    print(f"[mini_log_server] 日志文件: {LOG_FILE}")
    print(f"[mini_log_server] 过滤关键词: {FILTER_KEYWORDS}")
    print(f"[mini_log_server] Ctrl+C 停止")

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n[mini_log_server] 正在关闭...")
        server.shutdown()


if __name__ == '__main__':
    main()
