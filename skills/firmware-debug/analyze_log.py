#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
sEMG firmware log analyzer.
Reads the latest .txt log file from logs/serial/ and reports structured findings.
"""

import sys
import os
import re
import glob
import time
from pathlib import Path

# ── Config ────────────────────────────────────────────────────────────────────
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent.parent    # skills/firmware-debug -> skills -> project root
LOG_DIR    = PROJECT_DIR / "logs" / "serial"

# ── Severity levels (ASCII only, no Unicode emojis) ─────────────────────────
CRIT  = "[CRIT]"   # crash / deadlock
HIGH  = "[HIGH]"   # timeout / fatal error
MED   = "[MED ]"   # warning / anomaly
LOW   = "[LOW ]"   # info
OK    = "[OK ]"     # expected / healthy

# ── Pattern definitions ────────────────────────────────────────────────────────
PATTERNS = [
    # (severity, label, regex, hint)
    (CRIT,  "Firmware crash (HardFault/UsageFault)",
     re.compile(r"(Usage Fault|HardFault|ASSERT|FATAL)", re.IGNORECASE),
     "Device triggered hardware exception, possible illegal memory access or state machine error"),

    (HIGH,  "Command timeout (CMD_TIMEOUT)",
     re.compile(r"CMD_TIMEOUT", re.IGNORECASE),
     "Miniprogram waiting for firmware ACK timeout, firmware not responding"),

    (HIGH,  "JSON parse failed",
     re.compile(r"(deserializeJson|parse.*error|JSON.*fail)", re.IGNORECASE),
     "JSON parsing failed, possible buffer issue or data corruption"),

    (HIGH,  "State machine invalid transition",
     re.compile(r"(invalid transition|transitionTo.*failed)", re.IGNORECASE),
     "State machine received illegal command, protocol design issue"),

    (HIGH,  "WebSocket disconnected",
     re.compile(r"(TCP disconnected|WebSocket.*close|client.*disconnect)", re.IGNORECASE),
     "TCP connection lost, possible network issue or firmware crash"),

    (MED,   "Calibration validation failed",
     re.compile(r"(validation failed|calib.*fail|validation failed)", re.IGNORECASE),
     "Calibration data failed validation, RMS/MDF ratio does not meet requirements"),

    (MED,   "JSON serialization failed",
     re.compile(r"(serializeJson.*fail|Failed to serialize)", re.IGNORECASE),
     "JSON serialization failed, possible buffer too small or data format error"),

    (MED,   "Flash operation failed",
     re.compile(r"(Flash.*error|SPI.*fail|write.*fail|erase.*fail)", re.IGNORECASE),
     "Flash read/write failed, possible hardware issue or timing problem"),

    (LOW,   "REST phase completed",
     re.compile(r"REST done|REST.*SAFE|REST.*checkpoint", re.IGNORECASE),
     "REST phase completed normally"),

    (LOW,   "MAX phase started/completed",
     re.compile(r"(CALIB_MAX|MAX.*done|MAX phase)", re.IGNORECASE),
     "MAX phase has started"),

    (LOW,   "Calibration PASS",
     re.compile(r"(validation.*pass|calibration.*PASS|validateResult.*true)", re.IGNORECASE),
     "Calibration passed"),

    (LOW,   "ACK sent",
     re.compile(r"(ACK sent|ack sent)", re.IGNORECASE),
     "Firmware has sent ACK response"),

    (LOW,   "Connection success",
     re.compile(r"(TCP CONNECT|client.*connect|WiFi connected)", re.IGNORECASE),
     "Device connected successfully"),
]

# RMS/MDF data anomaly detection
DATA_RE = re.compile(r"rms[=:]?\s*([\d.]+)|mdf[=:]?\s*([\d.]+)", re.IGNORECASE)


def find_latest_log():
    """Return the most recent .txt file in LOG_DIR, or None."""
    files = glob.glob(str(LOG_DIR / "*.txt"))
    if not files:
        return None, None
    latest = max(files, key=os.path.getmtime)
    return latest, os.path.getmtime(latest)


def read_log(path):
    """Read log file content."""
    with open(path, "r", encoding="utf-8", errors="replace") as f:
        return f.read()


def check_data_anomaly(lines):
    """Check for unreasonable RMS/MDF values."""
    anomalies = []
    for line in lines:
        rms_vals = re.findall(r"rms[=:]?\s*([0-9.]+)", line, re.IGNORECASE)
        mdf_vals = re.findall(r"mdf[=:]?\s*([0-9.]+)", line, re.IGNORECASE)
        for v in rms_vals:
            try:
                val = float(v)
                if val < 0.01 or val > 10000:
                    anomalies.append(f"RMS={val} out of reasonable range (0.01~10000 mV)")
            except ValueError:
                pass
        for v in mdf_vals:
            try:
                val = float(v)
                if val < 5 or val > 500:
                    anomalies.append(f"MDF={val} out of reasonable range (5~500 Hz)")
            except ValueError:
                pass
    return anomalies


def analyze(path):
    """Analyze log file and return findings + total lines."""
    content = read_log(path)
    lines = content.split("\n")

    findings = {CRIT: [], HIGH: [], MED: [], LOW: []}

    for line in lines:
        for sev, label, pat, hint in PATTERNS:
            if pat.search(line):
                # ── Special case: "JSON parse failed" but line contains "err=OK" → skip ──
                if label == "JSON parse failed" and re.search(r"err=OK", line, re.IGNORECASE):
                    continue  # skip this pattern, try next pattern
                findings[sev].append((label, line.strip(), hint))
                break  # one finding per line

    # Data anomaly
    anomalies = check_data_anomaly(lines)
    for a in anomalies:
        findings[MED].append(("Data anomaly", a, "Check electrode contact or amplifier circuit"))

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
    lines.append("  sEMG Firmware Log Analysis Report")
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
    print("=" * 50)
    print("  sEMG Firmware Log Analyzer")
    print("=" * 50)

    if not LOG_DIR.exists():
        print(f"[ERROR] Log directory not found: {LOG_DIR}")
        print("Please run firmware-upload first to generate logs.")
        sys.exit(1)

    path, mtime = find_latest_log()
    if not path:
        print(f"[ERROR] No .txt log files found in {LOG_DIR}")
        print("Please run firmware-upload first.")
        sys.exit(1)

    print(f"[INFO] Analyzing: {Path(path).name}")

    findings, total = analyze(path)
    report = format_report(path, findings, total)
    print("")
    print(report)
    print("=" * 50)


if __name__ == "__main__":
    main()
