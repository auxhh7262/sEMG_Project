# -*- coding: utf-8 -*-
import os, sys, time, serial
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

def connect_serial():
    safe_print(f'\n[upload_and_monitor] Step 1: Opening {PORT}...')
    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.1)
        safe_print(f'[upload_and_monitor] Port opened, waiting 3s for device...')
        time.sleep(3)
        return ser
    except serial.SerialException as e:
        safe_print(f'[upload_and_monitor] ERROR: {e}')
        sys.exit(1)

def monitor_loop(ser):
    safe_print(f'[upload_and_monitor] Step 2: Monitoring (log -> {LOG_FILE})')
    safe_print('=' * 60)
    start_time = time.time()

    try:
        with open(LOG_FILE, 'w', encoding='utf-8') as f:
            f.write(f'=== Serial Log Started {time.strftime("%Y-%m-%d %H:%M:%S")} ===\n')
            f.flush()

            last_heartbeat = time.time()
            last_callbacks = None
            line_count = 0

            line_buf = ''
            while True:
                try:
                    data = ser.read(ser.in_waiting or 1)
                    if data:
                        try:
                            text = data.decode('utf-8', errors='replace')
                        except Exception:
                            text = data.decode('latin-1', errors='replace')
                        line_buf += text
                        while '\n' in line_buf:
                            line, line_buf = line_buf.split('\n', 1)
                            line = line.strip()
                            if line:
                                ts = time.strftime('%H:%M:%S')
                                log_line = f'[{ts}] {line}\n'
                                sys.stdout.write(log_line)
                                sys.stdout.flush()
                                f.write(log_line)
                                f.flush()
                                line_count += 1
                                last_heartbeat = time.time()
                                last_callbacks = line
                    else:
                        now = time.time()
                        if line_count > 0 and now - last_heartbeat > 5:
                            elapsed = int(now - start_time)
                            sys.stdout.write(f'[MONITOR] Connected {elapsed}s, {line_count} lines. Last: {last_callbacks}\n')
                            sys.stdout.flush()
                            f.write(f'[MONITOR] Heartbeat: {elapsed}s, {line_count} lines.\n')
                            f.flush()
                            last_heartbeat = now
                    time.sleep(0.01)
                except Exception as e:
                    sys.stdout.write(f'[ERROR] {e}\n')
                    sys.stdout.flush()
                    time.sleep(1)

    except KeyboardInterrupt:
        safe_print('\n[upload_and_monitor] Stopped by user.')
    finally:
        ser.close()

if __name__ == '__main__':
    ser = connect_serial()
    monitor_loop(ser)