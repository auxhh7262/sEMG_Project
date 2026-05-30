#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
emg-miniprogram-debug
小程序日志分析脚本
读取 logs/mini/mini_log.txt，扫描常见问题模式
"""
import os
import re
import sys
from pathlib import Path
from datetime import datetime

# ── 路径配置 ────────────────────────────────────────────────────────────────
SCRIPT_DIR = Path(__file__).parent.resolve()
PROJECT_DIR = SCRIPT_DIR.parent.parent    # skills/miniprogram-debug -> skills -> project root
MINI_LOG_DIR = PROJECT_DIR / "logs" / "mini"

# ── 问题模式定义 ────────────────────────────────────────────────────────────
PATTERNS = [
    # 🔴 严重/高优先级
    ("🔴 严重", "连接失败", r"connect\s+fail|WiFi\s+error|connection\s+failed", "检查WiFi连接和设备IP"),
    ("🔴 高",   "命令超时",   r"CMD_TIMEOUT|timeout",                    "检查固件是否正常响应"),
    ("🔴 高",   "JSON解析失败", r"parse\s+error|JSON\s+error",           "检查JSON格式是否正确"),
    ("🔴 高",   "WebSocket断连", r"disconnect|onClose|close",           "检查网络连接和心跳配置"),

    # 🟡 中等优先级
    ("🟡 中等", "状态异常",     r"state\s+error|invalid\s+state",        "检查状态机转换逻辑"),
    ("🟡 中等", "数据异常",     r"data\s+error|RMS\s*<|MDF\s*<",       "检查传感器连接和信号质量"),
    ("🟡 中等", "缓冲区溢出",   r"buffer\s+overflow|overflow",          "检查缓冲区大小是否足够"),

    # 🟢 正常/低优先级
    ("🟢 正常", "连接成功",     r"connect\s+success|ready|connected",   "连接正常"),
    ("🟢 正常", "命令成功",     r"CMD\s+success|ACK",                   "命令执行成功"),
]

# ── 日志分析函数 ────────────────────────────────────────────────────────────

def find_latest_log():
    """查找最新的日志文件"""
    if not MINI_LOG_DIR.exists():
        return None

    log_files = list(MINI_LOG_DIR.glob("mini_log_*.txt"))
    if not log_files:
        return None

    # 按修改时间排序，返回最新的
    return max(log_files, key=lambda f: f.stat().st_mtime)

def analyze_log(log_file):
    """分析日志文件，返回问题列表"""
    if not log_file or not log_file.exists():
        print(f"[ERROR] 日志文件不存在: {log_file}")
        return []

    print(f"[INFO] 分析文件: {log_file.name}")

    with open(log_file, "r", encoding="utf-8", errors="replace") as f:
        lines = f.readlines()

    print(f"[INFO] 总行数: {len(lines)}")

    # 按优先级收集问题
    issues_by_level = {
        "🔴 严重": [],
        "🔴 高":   [],
        "🟡 中等": [],
        "🟢 正常": [],
    }

    for line_no, line in enumerate(lines, 1):
        line = line.strip()
        if not line:
            continue

        for level, pattern_name, pattern, suggestion in PATTERNS:
            if re.search(pattern, line, re.IGNORECASE):
                issues_by_level[level].append({
                    "line": line_no,
                    "name": pattern_name,
                    "content": line[:120],  # 截断长行
                    "suggestion": suggestion
                })
                break  # 匹配第一个就够

    return issues_by_level

def print_report(log_file, issues_by_level):
    """打印分析报告"""
    now_str = datetime.now().strftime("%Y-%m-%d %H:%M:%S")

    print("\n" + "=" * 60)
    print("  小程序日志分析报告")
    print("=" * 60)
    print(f"  文件: {log_file.name}")
    print(f"  报告时间: {now_str}")
    print("=" * 60)

    total_issues = sum(len(v) for v in issues_by_level.values())
    if total_issues == 0:
        print("\n  ✅ 未发现已知问题模式")
        print("=" * 60)
        return

    # 按优先级输出
    for level in ["🔴 严重", "🔴 高", "🟡 中等", "🟢 正常"]:
        issues = issues_by_level[level]
        if not issues:
            continue

        print(f"\n  {level} [{level[1:]}] ({len(issues)} 项)")
        for i, issue in enumerate(issues, 1):
            print(f"    {i}. {issue['name']}")
            print(f"       日志: {issue['content']}")
            print(f"       建议: {issue['suggestion']}")

    print("\n" + "=" * 60)

# ── 主流程 ────────────────────────────────────────────────────────────────────

def main():
    print("\n" + "=" * 60)
    print("  sEMG 小程序日志分析工具")
    print("=" * 60)

    # 查找最新日志
    log_file = find_latest_log()
    if not log_file:
        print(f"[ERROR] 未找到日志文件 (*.txt) 在: {MINI_LOG_DIR}")
        print("[INFO] 请先运行小程序并开启日志服务器")
        print("=" * 60)
        sys.exit(1)

    # 分析日志
    issues_by_level = analyze_log(log_file)

    # 输出报告
    print_report(log_file, issues_by_level)

if __name__ == "__main__":
    # Windows 控制台 UTF-8 支持
    if sys.platform == "win32":
        sys.stdout.reconfigure(encoding="utf-8")
        sys.stderr.reconfigure(encoding="utf-8")

    main()
