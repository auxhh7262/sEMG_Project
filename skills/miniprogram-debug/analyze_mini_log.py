#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sEMG miniprogram log analyzer
Reads the latest .txt log file from logs/mini/ and reports structured findings.
"""

import sys
import os
import re
import glob
import time
from pathlib import Path

#  Config 
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent.parent    # skills/miniprogram-debug -> skills -> project root
LOG_DIR    = PROJECT_DIR / "logs" / "mini"

#  Severity levels (ASCII only, no Unicode emojis) 
CRIT  = "[CRIT]"   # crash / deadlock
HIGH  = "[HIGH]"   # timeout / fatal error
MED   = "[MED ]"   # warning / anomaly
LOW   = "[LOW ]"   # info
OK    = "[OK ]"     # expected / healthy

#  Pattern definitions 
PATTERNS = [
    # (severity, label, regex, hint)
    (CRIT,  "Connection failed",
     re.compile(r"connect\s+fail|WiFi\s+error|connection\s+failed", re.IGNORECASE),
     "Check WiFi connection and device IP"),

    (HIGH,  "Command timeout",
     re.compile(r"CMD_TIMEOUT|timeout", re.IGNORECASE),
     "Check if firmware is responding normally"),

    (HIGH,  "JSON parse failed",
     re.compile(r"parse\s+error|JSON\s+error", re.IGNORECASE),
     "Check if JSON format is correct"),

    (HIGH,  "WebSocket disconnected",
     re.compile(r"disconnect|onClose|close", re.IGNORECASE),
     "Check network connection and heartbeat config"),

    (MED,   "State anomaly",
     re.compile(r"state\s+error|invalid\s+state", re.IGNORECASE),
     "Check state machine transition logic"),

    (MED,   "Data anomaly",
     re.compile(r"data\s+error|RMS\s*<|MDF\s*<", re.IGNORECASE),
     "Check sensor connection and signal quality"),

    (MED,   "Buffer overflow",
     re.compile(r"buffer\s+overflow|overflow", re.IGNORECASE),
     "Check if buffer size is sufficient"),

    (LOW,   "Connection success",
     re.compile(r"connect\s+success|ready|connected", re.IGNORECASE),
     "Connection normal"),

    (LOW,   "Command success",
     re.compile(r"CMD\s+success|ACK", re.IGNORECASE),
     "Command executed successfully"),
]


def find_latest_log():
    """Return the most recent .txt file in LOG_DIR, or None."""
    files = glob.glob(str(LOG_DIR / "*.txt"))
    if not files:
        return None
    return max(files, key=os.path.getmtime)


def read_log(path):
    """Read log file content."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def analyze(path):
    """Analyze log file and return findings + total lines."""
    content = read_log(path)
    lines = content.split("\n")

    findings = {CRIT: [], HIGH: [], MED: [], LOW: []}

    for line in lines:
        for sev, label, pat, hint in PATTERNS:
            if pat.search(line):
                findings[sev].append((label, line.strip(), hint))
                break  # one finding per line

    # Deduplicate
    seen = set()
    for sev in findings:
        unique = []
        for item in findings[sev]:
            key = item[0]
            if key not in seen:
                seen.add(key)
                unique.append(item)
        findings[sev] = unique

    return findings, len(lines)


def format_report(path, findings, total_lines):
    """Format analysis report."""
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    mtime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(path)))

    lines = []
    lines.append("=" * 60)
    lines.append("  sEMG Miniprogram Log Analysis Report")
    lines.append("=" * 60)
    lines.append(f"  File: {Path(path).name}")
    lines.append(f"  Log time: {mtime}")
    lines.append(f"  Report time: {ts}")
    lines.append(f"  Total lines: {total_lines}")
    lines.append("")

    count = sum(len(v) for v in findings.values())
    if count == 0:
        lines.append("[OK] No known problem patterns found")
        lines.append("")
        return "\n".join(lines)

    for sev in [CRIT, HIGH, MED, LOW]:
        items = findings[sev]
        if not items:
            continue
        
        # Map severity to label (ASCII only)
        sev_label = {"[CRIT]": "[CRIT] Severe", "[HIGH]": "[HIGH] High",
                     "[MED ]": "[MED ] Medium", "[LOW ]": "[LOW ] Low"}.get(sev, sev)
        
        lines.append(f"  {sev_label} ({len(items)} items)")
        for i, (label, line, hint) in enumerate(items, 1):
            lines.append(f"    {i}. {label}")
            lines.append(f"       Log: {line[:120]}")
            lines.append(f"       Suggestion: {hint}")
        lines.append("")

    return "\n".join(lines)


def main():
    """Main function."""
    print("")
    print("=" * 60)
    print("  sEMG Miniprogram Log Analyzer")
    print("=" * 60)

    if not LOG_DIR.exists():
        print(f"[ERROR] Log directory not found: {LOG_DIR}")
        print("Please run miniprogram-preview first to generate logs.")
        sys.exit(1)

    path = find_latest_log()
    if not path:
        print(f"[ERROR] No .txt log files found in {LOG_DIR}")
        print("Please run miniprogram-preview first.")
        sys.exit(1)

    print(f"[INFO] Analyzing: {Path(path).name}")

    findings, total = analyze(path)
    report = format_report(path, findings, total)
    print("")
    print(report)
    print("=" * 60)


if __name__ == "__main__":
    main()
