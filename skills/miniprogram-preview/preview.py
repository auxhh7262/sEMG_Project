#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
emg-miniprogram-preview
 + 
 CLI
"""
import os
import sys
import time
import subprocess
from pathlib import Path

# 
# Project paths
# 
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent.parent    # skills/miniprogram-preview -> skills -> project
CLI_PATH = r"D:\Program Files\微信web开发者工具\cli.bat"
LOG_SERVER_SCRIPT = SCRIPT_DIR / "mini_log_server.py"
LOG_DIR = PROJECT_DIR / "logs" / "mini"

# 
# Helpers
# 
def log(msg):
    print(f"[INFO] {msg}", flush=True)

def log_ok(msg):
    print(f"[SUCCESS] {msg}", flush=True)

def log_err(msg):
    print(f"[ERROR] {msg}", flush=True)

def ensure_dir(path):
    path.mkdir(parents=True, exist_ok=True)

def kill_previous_log_server():
    """Kill previous mini_log_server.py processes (WMI matching)."""
    my_pid = os.getpid()
    # Use PowerShell Get-CimInstance to match command line reliably
    ps_cmd = '@(Get-CimInstance Win32_Process -Filter "Name=\'python.exe\'").' + \
             'Where({ $_.CommandLine -like "*mini_log_server.py*" }) | ' + \
             'ForEach-Object { if ($_.ProcessId -ne ' + str(my_pid) + ') { Stop-Process -Id $_.ProcessId -Force } }'
    try:
        subprocess.run(['powershell', '-Command', ps_cmd],
                       capture_output=True, text=True, timeout=10)
        time.sleep(0.5)
    except Exception as e:
        log(f"Kill previous log server warning: {e}")

def start_log_server():
    """Launch mini_log_server.py (tkinter GUI, no extra console window)."""
    LOG_DIR.mkdir(parents=True, exist_ok=True)
    #  CREATE_NEW_CONSOLE / DETACHED_PROCESS
    # 
    subprocess.Popen(
        [sys.executable, str(LOG_SERVER_SCRIPT)],
        cwd=str(LOG_DIR))
    time.sleep(1)
    log(" (:9876)")

def detect_changes():
    """Check if mini_program source files changed in last 30 min."""
    mini_dir = PROJECT_DIR / "mini_program"
    cutoff = time.time() - 30 * 60
    changed = []
    for ext in ('.js', '.json', '.wxml', '.wxss', '.ts'):
        for f in mini_dir.rglob(f'*{ext}'):
            if f.stat().st_mtime > cutoff:
                changed.append(str(f.relative_to(PROJECT_DIR)))
    return changed

# 
# Main
# 
def main():
    print("=" * 60, flush=True)
    print("  emg-miniprogram-preview", flush=True)
    print("=" * 60, flush=True)

    changed = detect_changes()
    if changed:
        log(f" ({len(changed)})")
        log(f": {', '.join(changed[:5])}")
        if len(changed) > 5:
            log("(5)")
    else:
        log(" (30)")

    # Step 1: Kill old mini_log_server
    kill_previous_log_server()

    # Step 2: Compile
    log("...")
    log(f": {CLI_PATH} auto-preview --project {PROJECT_DIR / 'mini_program'}")

    ensure_dir(LOG_DIR)
    ts = time.strftime("%Y%m%d_%H%M%S")
    mini_log = LOG_DIR / f"mini_log_{ts}.txt"

    cli_result = subprocess.run(
        [CLI_PATH, "auto-preview", "--project", str(PROJECT_DIR / "mini_program")],
        capture_output=True, text=True, timeout=120,
        encoding='utf-8', errors='replace')

    if cli_result.returncode == 0:
        log_ok("")
    else:
        err = cli_result.stderr.strip()
        log_err(f": {err if err else ''}")

    # Step 3: Start mini_log_server (tkinter GUI only, no extra console)
    start_log_server()

    print("=" * 60, flush=True)
    print("[DONE]  (:9876)", flush=True)
    print("=" * 60, flush=True)

if __name__ == "__main__":
    main()
