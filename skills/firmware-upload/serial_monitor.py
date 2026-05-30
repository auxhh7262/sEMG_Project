# -*- coding: utf-8 -*-
"""
Serial monitor with real-time tkinter GUI.
- Timestamped log files in logs/
- Auto-closes previous monitor window
- Shows real-time output in GUI
"""
import os, sys, time, serial, threading, re
from pathlib import Path
from tkinter import *
from tkinter import scrolledtext, messagebox

SCRIPT_DIR = Path(__file__).parent.resolve()
PORT = 'COM4'
BAUD = 115200

# ── Kill previous monitor (same script only) ──────────────────────────────────────
def kill_previous_monitors():
    """Kill previous python.exe instances running this script only."""
    import subprocess, os as osmod
    my_pid = osmod.getpid()
    my_script = osmod.path.basename(__file__)
    subprocess.run(
        ['powershell', '-Command',
         f'$procs = Get-CimInstance Win32_Process -Filter "Name=\'python.exe\'";'
         f'foreach ($p in $procs) {{ if ($p.ProcessId -ne {my_pid} -and $p.CommandLine -like "*{my_script}*") {{ Stop-Process -Id $p.ProcessId -Force }} }}'],
        capture_output=True, text=True)

kill_previous_monitors()

# ── Log file naming ────────────────────────────────────────────────────────────
LOG_DIR = SCRIPT_DIR.parent.parent / 'logs' / 'serial'
ts_str = time.strftime('%Y%m%d_%H%M%S')
LOG_FILE = LOG_DIR / f'serial_log_{ts_str}.txt'

# ── Tkinter GUI ───────────────────────────────────────────────────────────────
root = Tk()
root.title(f'sEMG Serial Monitor — {ts_str} — {PORT} @ {BAUD}')
root.geometry('900x600')
root.rowconfigure(1, weight=1)
root.columnconfigure(0, weight=1)

# Header
hdr = Frame(root, bg='#1a1a2e', pady=4)
hdr.grid(row=0, column=0, sticky='ew')
Label(hdr, text=f'📡 {PORT} @ {BAUD} baud',
      bg='#1a1a2e', fg='#00ff88', font='Consolas 10 bold').pack(side=LEFT, padx=10)
Label(hdr, text=f'Log: {LOG_FILE.name}',
      bg='#1a1a2e', fg='#888', font='Consolas 9').pack(side=RIGHT, padx=10)

# Text area
txt = scrolledtext.ScrolledText(
    root, wrap='none', font='Consolas 9',
    bg='#0d0d0d', fg='#00cc66',
    insertbackground='#00ff88',
    relief='flat', padx=6, pady=4
)
txt.grid(row=1, column=0, sticky='nsew')

# Horizontal scrollbar
hbar = Scrollbar(root, orient='horizontal', command=txt.xview)
txt['xscrollcommand'] = hbar.set
hbar.grid(row=2, column=0, sticky='ew')
txt.tag_config('INFO',  foreground='#00cc66')
txt.tag_config('WARN',  foreground='#ffcc00')
txt.tag_config('ERROR', foreground='#ff4444')
txt.tag_config('BOOT',  foreground='#66ccff')
txt.tag_config('DATA',  foreground='#cccccc')
txt.tag_config('RAW',   foreground='#888888')

def append(line, tag='INFO'):
    """Thread-safe append to text widget."""
    # Strip ALL CR characters - tkinter treats \r as carriage return
    line = line.replace('\r', '')
    # Add timestamp (match log file format)
    ts = time.strftime('%H:%M:%S')
    def _do():
        txt.configure(state='normal')
        txt.insert(END, f'[{ts}] {line}\n', tag)
        txt.see(END)
        txt.configure(state='disabled')
    txt.after(0, _do)

def log_to_file(line):
    """Write to both file and stdout."""
    ts = time.strftime('%H:%M:%S')
    with open(LOG_FILE, 'a', encoding='utf-8') as f:
        f.write(f'[{ts}] {line}\n')
    print(f'[{ts}] {line}')

append(f'=== Serial Monitor Started {time.strftime("%Y-%m-%d %H:%M:%S")} ===', 'BOOT')
append(f'Log file: {LOG_FILE}', 'INFO')
log_to_file(f'PORT={PORT} BAUD={BAUD}')

# ── Serial ────────────────────────────────────────────────────────────────────
ser = None

def detect_tag(line):
    l = line.upper()
    if 'ERROR' in l or 'FATAL' in l or 'ASSERT' in l: return 'ERROR'
    if 'WARN' in l: return 'WARN'
    if 'BOOT' in l or 'START' in l or '=========' in l: return 'BOOT'
    if '{' in line and ('rms' in l or 'mdf' in l or 'ts' in l or '"r"' in l): return 'DATA'
    if any(k in l for k in ('[', 'CMD', 'STATE', 'CALIB', 'MONITOR')): return 'INFO'
    return 'RAW'

def read_serial():
    global ser
    while True:
        try:
            if ser is None:
                try:
                    ser = serial.Serial(PORT, BAUD, timeout=0.5)
                    log_to_file(f'Port {PORT} opened. Waiting for device boot...')
                    # Wait for boot data to arrive (firmware prints BOOT after delay(3000))
                    # Instead of a fixed sleep, wait until we see actual boot data
                    boot_wait_start = time.time()
                    boot_timeout = 8  # max 8s waiting for boot data
                    boot_data_received = False
                    while time.time() - boot_wait_start < boot_timeout:
                        if ser.in_waiting > 0:
                            boot_data_received = True
                            break
                        time.sleep(0.1)
                    if boot_data_received:
                        append(f'[Device boot data detected, reading...]', 'BOOT')
                        log_to_file('Device boot data detected.')
                    else:
                        append(f'[No boot data after {boot_timeout}s, reading anyway]', 'WARN')
                        log_to_file(f'No boot data after {boot_timeout}s timeout.')
                except serial.SerialException as e:
                    append(f'Cannot open {PORT}: {e}', 'ERROR')
                    time.sleep(2)
                    continue

            pending_line = ''
            while True:
                data = ser.read(ser.in_waiting or 1)
                if data:
                    try:
                        text = data.decode('utf-8', errors='replace')
                    except Exception:
                        text = data.decode('latin-1', errors='replace')
                    text = text.replace('\r', '')
                    # Prepend leftover from previous frame
                    if pending_line:
                        text = pending_line + text
                        pending_line = ''
                    lines = text.splitlines()
                    # Keep last incomplete line for next frame
                    if text and not text.endswith('\n') and lines:
                        pending_line = lines.pop()
                    # ── Merge interrupt-split fragments ────────────────
                    merged = []
                    for raw_line in lines:
                        stripped = raw_line.rstrip()
                        if not stripped:
                            continue
                        ch = stripped[0]
                        is_new_entry = ch in ('[', '=', ' ', '\t') or ch.isdigit()
                        if merged and not is_new_entry:
                            merged[-1] += raw_line
                        else:
                            merged.append(raw_line)
                    # Pass 2: merge lines ending with '[' into next line
                    final = []
                    i = 0
                    while i < len(merged):
                        line = merged[i]
                        while line.rstrip().endswith('[') and i + 1 < len(merged):
                            i += 1
                            line += merged[i]
                        final.append(line)
                        i += 1
                    for line in final:
                        tag = detect_tag(line)
                        append(line, tag)
                        log_to_file(line)
                else:
                    time.sleep(0.01)

        except serial.SerialException as e:
            append(f'Serial error: {e}', 'ERROR')
            try:
                ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(2)
        except Exception as e:
            append(f'Error: {e}', 'ERROR')
            time.sleep(1)

# ── Controls ─────────────────────────────────────────────────────────────────
ctrl = Frame(root, bg='#1a1a2e', pady=4)
ctrl.grid(row=2, column=0, sticky='ew')

def on_close():
    if messagebox.askokcancel('Quit', 'Stop monitoring?'):
        try:
            if ser:
                ser.close()
        except Exception:
            pass
        root.destroy()
        sys.exit(0)

root.protocol('WM_DELETE_WINDOW', on_close)

# Open log folder button
def open_log_folder():
    os.startfile(LOG_DIR)
Button(ctrl, text='📂 Open Logs', command=open_log_folder,
       font='Consolas 9', bg='#2d2d4a', fg='white',
       relief='flat', padx=10).pack(side=LEFT, padx=6)

# Current log path
Label(ctrl, text=f'{LOG_FILE}',
      bg='#1a1a2e', fg='#555', font='Consolas 8').pack(side=RIGHT, padx=6)

# Start serial thread
t = threading.Thread(target=read_serial, daemon=True)
t.start()

root.mainloop()
