# -*- coding: utf-8 -*-
"""串口读取器 - 独占串口，持续读取并写入 serial_log.txt
使用 DTR/RTS 触发设备复位（替代 1200bps 技巧）
"""
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
    safe_print(f'\n[serial_reader] Opening {PORT} at {BAUD}...')
    try:
        # dsrdtr=True 让 setDTR/setRTS 生效
        # rtscts=False 避免硬件流控问题
        ser = serial.Serial(
            PORT, BAUD, timeout=0.1,
            dsrdtr=True,   # 启用 DTR/DSR 握手
            rtscts=False   # 禁用 RTS/CTS 握手
        )
        # DTR/RTS 默认可能已是 True（触发复位），显式设置一次确保复位
        ser.setDTR(True)
        ser.setRTS(True)
        safe_print(f'[serial_reader] Port opened, DTR={ser.dtr} RTS={ser.rts}')
        safe_print(f'[serial_reader] Waiting 2s for device boot...')
        time.sleep(2)
        # 清空缓冲区（丢弃复位期间的数据）
        ser.reset_input_buffer()
        safe_print(f'[serial_reader] Buffer flushed, starting to read...')
        return ser
    except serial.SerialException as e:
        safe_print(f'[serial_reader] ERROR: {e}')
        sys.exit(1)

def monitor_loop(ser):
    safe_print(f'[serial_reader] Logging to {LOG_FILE}')
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
                            sys.stdout.write(f'[serial_reader] Connected {elapsed}s, {line_count} lines. Last: {last_callbacks}\n')
                            sys.stdout.flush()
                            f.write(f'[serial_reader] Heartbeat: {elapsed}s, {line_count} lines.\n')
                            f.flush()
                            last_heartbeat = now
                    time.sleep(0.01)
                except Exception as e:
                    sys.stdout.write(f'[ERROR] {e}\n')
                    sys.stdout.flush()
                    time.sleep(1)

    except KeyboardInterrupt:
        safe_print('\n[serial_reader] Stopped by user.')
    finally:
        ser.close()

if __name__ == '__main__':
    ser = connect_serial()
    monitor_loop(ser)
