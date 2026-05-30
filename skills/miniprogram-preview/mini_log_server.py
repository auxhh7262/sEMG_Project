# -*- coding: utf-8 -*-
"""
小程序日志转发服务器 (tkinter GUI版)
接收小程序 wx.request() 发送的日志，实时显示在GUI窗口，同时写入日志文件。
时间戳命名，每次启动独立文件，与串口监控风格统一。

端口: 9876
接口: POST /log

启动: python mini_log_server.py
停止: 关闭窗口
"""
import http.server
import json
import os
import sys
import threading
import time
from pathlib import Path
from datetime import datetime
from tkinter import *
from tkinter import scrolledtext, messagebox

# ── 路径配置 ─────────────────────────────────────────────────────────────────
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent.parent

# ── Kill previous monitors (same script only) ──────────────────────────────────
def kill_previous_monitors():
    import subprocess, os as osmod
    my_pid = osmod.getpid()
    my_script = osmod.path.basename(__file__)
    r = subprocess.run(
        ['powershell', '-Command',
         f'$procs = Get-CimInstance Win32_Process -Filter "Name=\'python.exe\'";'
         f'foreach ($p in $procs) {{ if ($p.ProcessId -ne {my_pid} -and $p.CommandLine -like "*{my_script}*") {{ Stop-Process -Id $p.ProcessId -Force }} }}'],
        capture_output=True, text=True)

kill_previous_monitors()

# ── 配置 ─────────────────────────────────────────────────────────────────────
PORT = 9876
LOG_DIR = PROJECT_DIR / 'logs' / 'mini'

ts_str = time.strftime('%Y%m%d_%H%M%S')
LOG_FILE = LOG_DIR / f'mini_log_{ts_str}.txt'

LOG_DIR.mkdir(parents=True, exist_ok=True)

FILTER_KEYWORDS = ['LogForward', 'heartbeat', 'ping', 'pong']
file_lock = threading.Lock()


# ── Tkinter GUI ────────────────────────────────────────────────────────────────
root = Tk()
root.title(f'sEMG Mini Log — {ts_str} — :{PORT}')
root.geometry('900x600')
root.rowconfigure(1, weight=1)
root.columnconfigure(0, weight=1)

# Header
hdr = Frame(root, bg='#1a1a2e', pady=4)
hdr.grid(row=0, column=0, sticky='ew')
Label(hdr, text=f'📱 Mini Log Server — :{PORT}',
      bg='#1a1a2e', fg='#00ccff', font='Consolas 10 bold').pack(side=LEFT, padx=10)
Label(hdr, text=f'Log: {LOG_FILE.name}',
      bg='#1a1a2e', fg='#888', font='Consolas 9').pack(side=RIGHT, padx=10)

# Text area
txt = scrolledtext.ScrolledText(
    root, wrap='word', font='Consolas 9',
    bg='#0d0d0d', fg='#00ccff',
    insertbackground='#00ccff',
    relief='flat', padx=6, pady=4
)
txt.grid(row=1, column=0, sticky='nsew')
txt.tag_config('INFO',  foreground='#00ccff')
txt.tag_config('WARN',  foreground='#ffcc00')
txt.tag_config('ERROR', foreground='#ff4444')
txt.tag_config('BOOT',  foreground='#66ccff')
txt.tag_config('HTTP',  foreground='#888888')

def append(line, tag='INFO'):
    """Thread-safe append to text widget."""
    def _do():
        txt.configure(state='normal')
        txt.insert(END, line + '\n', tag)
        txt.see(END)
        txt.configure(state='disabled')
    txt.after(0, _do)

def write_log(level, msg, timestamp=None):
    """写入日志文件并显示在GUI"""
    if any(kw in msg for kw in FILTER_KEYWORDS):
        return

    ts = timestamp or time.strftime('%H:%M:%S')
    line = f"[{ts}] [{level}] {msg}"

    with file_lock:
        with open(LOG_FILE, 'a', encoding='utf-8') as f:
            f.write(line + '\n')

    # 根据level选颜色
    tag = 'INFO'
    upper_level = level.upper()
    if 'ERROR' in upper_level or 'FATAL' in upper_level:
        tag = 'ERROR'
    elif 'WARN' in upper_level:
        tag = 'WARN'
    elif 'BOOT' in upper_level or 'START' in upper_level:
        tag = 'BOOT'

    append(line, tag)


# ── HTTP Log Handler ─────────────────────────────────────────────────────────
class LogHandler(http.server.BaseHTTPRequestHandler):
    def do_POST(self):
        if self.path != '/log':
            self.send_response(404)
            self.end_headers()
            return

        try:
            content_length = int(self.headers.get('Content-Length', 0))
            body = self.rfile.read(content_length)
            data = json.loads(body.decode('utf-8'))

            if isinstance(data, list):
                logs = data
            elif isinstance(data, dict):
                if 'logs' in data:
                    logs = data['logs']
                elif 'level' in data and 'msg' in data:
                    logs = [data]
                else:
                    logs = [{'level': 'INFO', 'msg': json.dumps(data, ensure_ascii=False)}]
            else:
                logs = [{'level': 'INFO', 'msg': str(data)}]

            for log_entry in logs:
                level = log_entry.get('level', 'INFO').upper()
                msg = log_entry.get('msg', '')
                ts = log_entry.get('time', None)
                write_log(level, msg, ts)

            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.send_header('Access-Control-Allow-Origin', '*')
            self.end_headers()
            self.wfile.write(json.dumps({'ok': True}).encode('utf-8'))

        except json.JSONDecodeError:
            self.send_response(400)
            self.end_headers()
        except Exception as e:
            write_log('ERROR', f'处理日志请求失败: {e}')
            self.send_response(500)
            self.end_headers()

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header('Access-Control-Allow-Origin', '*')
        self.send_header('Access-Control-Allow-Methods', 'POST, OPTIONS')
        self.send_header('Access-Control-Allow-Headers', 'Content-Type')
        self.end_headers()

    def do_GET(self):
        if self.path == '/health':
            self.send_response(200)
            self.send_header('Content-Type', 'application/json')
            self.end_headers()
            self.wfile.write(json.dumps({'status': 'ok', 'port': PORT}).encode('utf-8'))
        else:
            self.send_response(404)
            self.end_headers()

    def log_message(self, format, *args):
        append(f'[HTTP] {self.client_address[0]} - {format % args}', 'HTTP')


# ── Controls ─────────────────────────────────────────────────────────────────
ctrl = Frame(root, bg='#1a1a2e', pady=4)
ctrl.grid(row=2, column=0, sticky='ew')

def on_close():
    if messagebox.askokcancel('Quit', 'Stop mini log server?'):
        server.shutdown()
        root.destroy()
        sys.exit(0)

root.protocol('WM_DELETE_WINDOW', on_close)

Button(ctrl, text='📂 Open Logs', command=lambda: os.startfile(LOG_DIR),
       font='Consolas 9', bg='#2d2d4a', fg='white',
       relief='flat', padx=10).pack(side=LEFT, padx=6)

Label(ctrl, text=f'{LOG_FILE}',
      bg='#1a1a2e', fg='#555', font='Consolas 8').pack(side=RIGHT, padx=6)


# ── 启动 ──────────────────────────────────────────────────────────────────────
append(f'=== Mini Log Server Started {time.strftime("%Y-%m-%d %H:%M:%S")} ===', 'BOOT')
append(f'Log file: {LOG_FILE}', 'INFO')
append(f'Port: :{PORT}', 'INFO')
append(f'Filter: {FILTER_KEYWORDS}', 'INFO')
append(f'Waiting for mini program logs...', 'INFO')

server = http.server.HTTPServer(('0.0.0.0', PORT), LogHandler)

# HTTP server 在后台线程运行
http_thread = threading.Thread(target=server.serve_forever, daemon=True)
http_thread.start()

root.mainloop()
