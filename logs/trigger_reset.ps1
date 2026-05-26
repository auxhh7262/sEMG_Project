# -*- coding: utf-8 -*-
# trigger_reset.py - 1200bps 复位触发器（快速 open/close，不独占端口）
import sys
import time
try:
    import serial
except ImportError:
    print('[ERROR] pyserial not installed')
    sys.exit(1)

PORT = 'COM4'

def trigger_reset():
    try:
        # 1200bps 快速 open/close，触发设备进入 bootloader
        print(f'[trigger_reset] Sending 1200bps trigger to {PORT}...')
        ser = serial.Serial(PORT, 1200, timeout=0.5)
        time.sleep(0.1)
        ser.close()
        print(f'[trigger_reset] Done. Device should enter bootloader now.')
        return True
    except Exception as e:
        print(f'[trigger_reset] ERROR: {e}')
        return False

if __name__ == '__main__':
    trigger_reset()
