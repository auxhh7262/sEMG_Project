# -*- coding: utf-8 -*-
import sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

"""
sEMG firmware log analyzer.
Reads the latest .txt log file from logs/serial/ and reports structured findings.
"""
import os, re, sys, glob
from pathlib import Path

# ── Config ────────────────────────────────────────────────────────────────────
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent.parent    # skills/firmware-debug -> skills -> project root
LOG_DIR    = PROJECT_DIR / "logs" / "serial"

# ── Severity levels ────────────────────────────────────────────────────────────
CRIT  = "[CRIT]"   # 🔴 crash / deadlock
HIGH  = "[HIGH]"   # 🔴 timeout / fatal error
MED   = "[MED ]"   # 🟡 warning / anomaly
LOW   = "[LOW ]"   # 🟢 info
OK    = "[ OK ]"   # ✅ expected / healthy

# ── Pattern definitions ────────────────────────────────────────────────────────
PATTERNS = [
    # (severity, label, regex, hint)
    (CRIT,  "固件崩溃 (HardFault/UsageFault)",
     re.compile(r"(Usage Fault|HardFault|ASSERT|FATAL)", re.IGNORECASE),
     "设备触发硬件异常，可能是非法内存访问或状态机错误"),

    (HIGH,  "命令超时 (CMD_TIMEOUT)",
     re.compile(r"CMD_TIMEOUT", re.IGNORECASE),
     "小程序等待固件ACK超时，固件未响应"),

    (HIGH,  "JSON解析失败",
     re.compile(r"(deserializeJson|parse.*error|JSON.*fail)", re.IGNORECASE),
     "JSON解析失败，可能是缓冲区问题或数据损坏"),

    (HIGH,  "状态机非法转换",
     re.compile(r"(invalid transition|transitionTo.*failed)", re.IGNORECASE),
     "状态机收到非法命令，协议设计有问题"),

    (HIGH,  "WebSocket断连",
     re.compile(r"(TCP disconnected|WebSocket.*close|client.*disconnect)", re.IGNORECASE),
     "TCP连接断开，可能是网络问题或固件崩溃"),

    (MED,   "校准验证失败 (validation failed)",
     re.compile(r"(validation failed|calib.*fail|校验失败)", re.IGNORECASE),
     "校准数据未通过校验，RMS/MDF比例不满足要求"),

    (MED,   "JSON序列化失败 (serializeJson)",
     re.compile(r"(serializeJson.*fail|Failed to serialize)", re.IGNORECASE),
     "JSON序列化失败，可能是缓冲区太小或数据格式错误"),

    (MED,   "Flash操作失败",
     re.compile(r"(Flash.*error|SPI.*fail|write.*fail|erase.*fail)", re.IGNORECASE),
     "Flash读写失败，可能是硬件问题或时序问题"),

    (LOW,   "REST阶段完成",
     re.compile(r"REST done|REST.*SAFE|REST.*checkpoint", re.IGNORECASE),
     "REST阶段正常完成"),

    (LOW,   "MAX阶段启动/完成",
     re.compile(r"(CALIB_MAX|MAX.*done|MAX phase)", re.IGNORECASE),
     "MAX阶段已启动"),

    (LOW,   "校准PASS",
     re.compile(r"(validation.*pass|calibration.*PASS|validateResult.*true)", re.IGNORECASE),
     "校准通过"),

    (LOW,   "ACK已发送",
     re.compile(r"(ACK sent|ack sent)", re.IGNORECASE),
     "固件已发送ACK响应"),

    (LOW,   "连接成功",
     re.compile(r"(TCP CONNECT|client.*connect|WiFi connected)", re.IGNORECASE),
     "设备连接成功"),
]

# RMS/MDF 数据异常检测
DATA_RE = re.compile(r"rms[=:]?\s*([\d.]+)|mdf[=:]?\s*([\d.]+)", re.IGNORECASE)

def find_latest_log():
    """Return the most recent .txt file in LOG_DIR, or None."""
    files = glob.glob(str(LOG_DIR / "*.txt"))
    if not files:
        return None, None
    latest = max(files, key=os.path.getmtime)
    return latest, os.path.getmtime(latest)

def read_log(path):
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
                    anomalies.append(f"RMS={val} 超出合理范围 (0.01~10000 mV)")
            except ValueError:
                pass
        for v in mdf_vals:
            try:
                val = float(v)
                if val < 5 or val > 500:
                    anomalies.append(f"MDF={val} 超出合理范围 (5~500 Hz)")
            except ValueError:
                pass
    return anomalies

def analyze(path):
    content = read_log(path)
    lines = content.split("\n")

    findings = {CRIT: [], HIGH: [], MED: [], LOW: []}

    for line in lines:
        for sev, label, pat, hint in PATTERNS:
            if pat.search(line):
                findings[sev].append((label, line.strip(), hint))
                break  # one finding per line

    # Data anomaly
    anomalies = check_data_anomaly(lines)
    for a in anomalies:
        findings[MED].append(("数据异常", a, "检查电极接触或放大电路"))

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
    import time
    ts = time.strftime("%Y-%m-%d %H:%M:%S")
    mtime = time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(os.path.getmtime(path)))

    lines = []
    lines.append("=" * 60)
    lines.append("  sEMG 固件日志分析报告")
    lines.append("=" * 60)
    lines.append(f"  文件: {Path(path).name}")
    lines.append(f"  日志时间: {mtime}")
    lines.append(f"  报告时间: {ts}")
    lines.append(f"  总行数: {total_lines}")
    lines.append("")

    count = sum(len(v) for v in findings.values())
    if count == 0:
        lines.append("  ✅ 未发现已知问题模式")
        lines.append("")
        return "\n".join(lines)

    for sev in [CRIT, HIGH, MED, LOW]:
        items = findings[sev]
        if not items:
            continue
        icon = {"[CRIT]": "🔴 严重", "[HIGH]": "🔴 高",
                "[MED ]": "🟡 中等", "[LOW ]": "🟢 正常"}.get(sev, sev)
        lines.append(f"  {icon} {sev} ({len(items)} 项)")
        for i, (label, line, hint) in enumerate(items, 1):
            lines.append(f"    {i}. {label}")
            lines.append(f"       日志: {line[:120]}")
            lines.append(f"       建议: {hint}")
        lines.append("")

    return "\n".join(lines)

def main():
    print("")
    print("=" * 50)
    print("  sEMG Firmware Log Analyzer")
    print("=" * 50)

    if not LOG_DIR.exists():
        print(f"[ERROR] Log directory not found: {LOG_DIR}")
        print("Please run emg-firmware-upload first to generate logs.")
        sys.exit(1)

    path, mtime = find_latest_log()
    if not path:
        print(f"[ERROR] No .txt log files found in {LOG_DIR}")
        print("Please run emg-firmware-upload first.")
        sys.exit(1)

    print(f"[INFO] Analyzing: {Path(path).name}")

    findings, total = analyze(path)
    report = format_report(path, findings, total)
    print("")
    print(report)
    print("=" * 50)

if __name__ == "__main__":
    main()
