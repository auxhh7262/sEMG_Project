# -*- coding: utf-8 -*-
"""串口监控 v2 - 监控 serial_log.txt 文件变化，不独占串口
配合 serial_port_reader.py 使用（serial_port_reader.py 独占串口读取，本脚本监控文件）
"""
import os, sys, time
from pathlib import Path

SCRIPT_DIR = Path(__file__).parent.resolve()
LOG_FILE = SCRIPT_DIR / 'serial_log.txt'
PORT = 'COM4'
BAUD = 115200

def safe_print(msg):
    try:
        print(msg)
        sys.stdout.flush()
    except Exception:
        pass

def wait_for_file_change():
    """等待文件被创建或修改（通过检查文件修改时间）"""
    last_mtime = None
    while True:
        if LOG_FILE.exists():
            current_mtime = LOG_FILE.stat().st_mtime
            if last_mtime is None:
                last_mtime = current_mtime
                safe_print(f'[file_monitor] Watching {LOG_FILE} (size: {LOG_FILE.stat().st_size})...')
            elif current_mtime > last_mtime:
                # 文件有变化，读取新内容
                last_mtime = current_mtime
                try:
                    with open(LOG_FILE, 'r', encoding='utf-8') as f:
                        f.seek(0)
                        content = f.read()
                    lines = content.split('\n')
                    # 找到第一个 "[HH:MM:SS]" 格式的行作为起点
                    start_idx = 0
                    for i, line in enumerate(lines):
                        if line.strip() and len(line) >= 12 and line.strip()[0] == '[':
                            start_idx = i
                            break
                    # 打印新增的行（跳过已打印过的）
                    for line in lines[start_idx:]:
                        if line.strip():
                            print(line)
                            sys.stdout.flush()
                except Exception as e:
                    safe_print(f'[file_monitor] Read error: {e}')
        else:
            if last_mtime is None:
                safe_print(f'[file_monitor] Waiting for {LOG_FILE} to be created...')
                last_mtime = -1
        time.sleep(0.1)

def monitor_loop():
    safe_print(f'[file_monitor] Starting file monitor for {LOG_FILE}')
    safe_print(f'[file_monitor] NOTE: Start serial_port_reader.py separately to feed the log file')
    safe_print('=' * 60)
    wait_for_file_change()

if __name__ == '__main__':
    monitor_loop()
