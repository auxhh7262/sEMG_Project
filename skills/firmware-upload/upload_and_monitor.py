# -*- coding: utf-8 -*-
"""
sEMG firmware upload + serial monitor script.

Key timing: pio uploads (COM4 occupied) -> done, board reboots ->
immediately start serial monitor (tkinter GUI, timestamped log file) ->
board still in delay(3000) -> BOOT log starts -> monitor already ready.

Each run creates a new timestamped log file in logs/.
Previous monitor window is auto-closed by serial_monitor.py.
"""
import subprocess
import time
import os
import sys

# ==================== Config ====================
FIRMWARE_DIR = r"E:\Personal\sEMG_Project\firmware"
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))  # E:\...\skills\firmware-upload\
PROJECT_DIR = os.path.dirname(os.path.dirname(SCRIPT_DIR))  # up 2 levels: skills/firmware-upload -> skills -> project root
LOG_DIR = os.path.join(PROJECT_DIR, "logs")
SERIAL_SCRIPT = os.path.join(SCRIPT_DIR, "serial_monitor.py")
COM_PORT = "COM4"
PIO_EXE = r"C:\Users\honghuang\.platformio\penv\Scripts\pio.exe"

def log(msg):
    ts = time.strftime("%H:%M:%S")
    print(f"[{ts}] {msg}", flush=True)

def kill_com_port_processes():
    log(">>> Step 1: Killing COM4 processes...")
    import os as osmod
    my_pid = osmod.getpid()
    # 只杀可能占用COM4的进程：串口监控、pio、platformio
    targets = ['serial_monitor.py']
    for t in targets:
        subprocess.run(
            ['powershell', '-Command',
             f'$procs = Get-CimInstance Win32_Process -Filter "Name=\'python.exe\'";'
             f'foreach ($p in $procs) {{ if ($p.ProcessId -ne {my_pid} -and $p.CommandLine -like "*{t}*") {{ Stop-Process -Id $p.ProcessId -Force }} }}'],
            capture_output=True, text=True)
    # 杀 pio/platformio
    subprocess.run(
        ['powershell', '-Command',
         f'Get-Process pio,platformio -ErrorAction SilentlyContinue | Stop-Process -Force'],
        capture_output=True)
    log("    Done. COM4 should be free.")
    time.sleep(1)

def upload_firmware():
    log(f">>> Step 2: Uploading firmware to {COM_PORT}...")
    result = subprocess.run(
        [PIO_EXE, "run", "-t", "upload", "--upload-port", COM_PORT],
        cwd=FIRMWARE_DIR,
        capture_output=True,
        encoding="utf-8",
        errors="replace"
    )
    if result.returncode != 0:
        log(f"!!! Upload failed (exit {result.returncode}):")
        stderr = result.stderr or ""
        stdout = result.stdout or ""
        output = (stderr + stdout)[-800:]
        for line in output.split('\n')[-12:]:
            if line.strip():
                log(f"    {line}")
        raise RuntimeError(f"Upload failed (exit code {result.returncode})")
    log("    Upload OK, board rebooting...")

def start_serial_monitor():
    log(">>> Step 3: Starting serial monitor (tkinter GUI + timestamped log)...")
    try:
        # serial_monitor.py auto-kills previous instances and opens tkinter GUI
        # Log file: logs/serial_log_YYYYMMDD_HHMMSS.txt
        # No CREATE_NEW_CONSOLE: serial_monitor.py uses tkinter GUI,
        # we don't need an extra empty console window
        subprocess.Popen(
            [sys.executable, SERIAL_SCRIPT],
            cwd=LOG_DIR
        )
        log("    Monitor started. GUI window should appear shortly.")
        log(f"    Log dir: {LOG_DIR}")
    except Exception as e:
        log(f"!!! Failed to start monitor: {e}")
        raise

def main():
    log("=" * 60)
    log("sEMG Firmware Upload + Serial Monitor")
    log("=" * 60)
    try:
        kill_com_port_processes()
        upload_firmware()
        time.sleep(0.5)
        start_serial_monitor()
        log("=" * 60)
        log(">>> DONE. Monitor running, log dir:")
        log(f">>> {LOG_DIR}")
        log("=" * 60)
    except Exception as e:
        log(f"!!! ERROR: {e}")
        sys.exit(1)

if __name__ == "__main__":
    main()
