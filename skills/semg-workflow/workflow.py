#!/usr/bin/env python3
"""
semg-workflow: sEMG project combined workflows
Supports 2 workflows:
  1. deploy (upload firmware + compile miniprogram)
  2. analyze (analyze firmware log + miniprogram log)
"""

import sys
import time
import subprocess
from pathlib import Path

# Project root directory (2 levels up: semg-workflow -> skills -> sEMG_Project)
SKILLS_DIR = Path(__file__).parent.parent

# Sub-skill script paths
FIRMWARE_UPLOAD_SCRIPT = SKILLS_DIR / "firmware-upload" / "upload_and_monitor.py"
MINIPROGRAM_PREVIEW_SCRIPT = SKILLS_DIR / "miniprogram-preview" / "preview.py"
FIRMWARE_DEBUG_SCRIPT = SKILLS_DIR / "firmware-debug" / "analyze_log.py"
MINIPROGRAM_DEBUG_SCRIPT = SKILLS_DIR / "miniprogram-debug" / "analyze_mini_log.py"


def deploy():
    """
    Workflow 1: Upload and compile (deploy class)
    Execution order: firmware-upload -> miniprogram-preview
    """
    print("=" * 60)
    print("[WORKFLOW] Upload and Compile (Deploy)")
    print("=" * 60)

    # Step 1: Upload firmware
    print("\n[1/2] Uploading firmware...")
    print(f"  Script: {FIRMWARE_UPLOAD_SCRIPT}")
    
    result1 = subprocess.run(
        ["python", str(FIRMWARE_UPLOAD_SCRIPT)],
        cwd=str(SKILLS_DIR / "firmware-upload"),
        capture_output=False  # Allow GUI window to pop up
    )
    
    if result1.returncode != 0:
        print("[ERR] Firmware upload failed")
        print(f"       Return code: {result1.returncode}")
        sys.exit(1)
    
    print("[OK] Firmware upload successful")
    print("       - Serial monitor GUI started")
    print("       - Firmware rebooting...")

    # Wait for firmware reboot (give it some time)
    print("       - Waiting for firmware reboot (3 seconds)...")
    time.sleep(3)

    # Step 2: Compile miniprogram
    print("\n[2/2] Compiling miniprogram...")
    print(f"  Script: {MINIPROGRAM_PREVIEW_SCRIPT}")
    
    result2 = subprocess.run(
        ["python", str(MINIPROGRAM_PREVIEW_SCRIPT)],
        cwd=str(SKILLS_DIR / "miniprogram-preview"),
        capture_output=False  # Allow GUI window to pop up
    )
    
    if result2.returncode != 0:
        print("[ERR] Miniprogram compilation failed")
        print(f"       Return code: {result2.returncode}")
        sys.exit(1)
    
    print("[OK] Miniprogram compilation successful")
    print("       - Log server GUI started")
    print("       - Preview pushed to phone")

    # Done
    print("\n" + "=" * 60)
    print("[DONE] Deploy complete!")
    print("=" * 60)
    print("\n[REPORT] Running status:")
    print("  - Serial monitor GUI is running (firmware log)")
    print("  - Miniprogram log server GUI is running (miniprogram log)")
    print("\n[TIP]")
    print("  - View firmware log: Serial monitor GUI window")
    print("  - View miniprogram log: Log server GUI window")
    print("  - Preview pushed to WeChat on phone")


def analyze():
    """
    Workflow 2: Analyze all logs (debug class)
    Execution order: firmware-debug -> miniprogram-debug
    """
    print("=" * 60)
    print("[WORKFLOW] Analyze All Logs (Debug)")
    print("=" * 60)

    # Step 1: Analyze firmware log
    print("\n[1/2] Analyzing firmware log...")
    print(f"  Script: {FIRMWARE_DEBUG_SCRIPT}")
    
    result1 = subprocess.run(
        ["python", str(FIRMWARE_DEBUG_SCRIPT)],
        cwd=str(SKILLS_DIR / "firmware-debug"),
        capture_output=False  # Allow interaction
    )
    
    if result1.returncode != 0:
        print("[WARN] Firmware log analysis failed (maybe no log file)")
        print(f"        Return code: {result1.returncode}")
        # Don't exit, continue to analyze miniprogram log
    else:
        print("[OK] Firmware log analysis complete")

    # Step 2: Analyze miniprogram log
    print("\n[2/2] Analyzing miniprogram log...")
    print(f"  Script: {MINIPROGRAM_DEBUG_SCRIPT}")
    
    result2 = subprocess.run(
        ["python", str(MINIPROGRAM_DEBUG_SCRIPT)],
        cwd=str(SKILLS_DIR / "miniprogram-debug"),
        capture_output=False  # Allow interaction
    )
    
    if result2.returncode != 0:
        print("[WARN] Miniprogram log analysis failed (maybe no log file)")
        print(f"        Return code: {result2.returncode}")
    else:
        print("[OK] Miniprogram log analysis complete")

    # Done
    print("\n" + "=" * 60)
    print("[REPORT] Combined analysis report generated")
    print("=" * 60)
    print("\n[TIP]")
    print("  - Firmware log analysis: See output above")
    print("  - Miniprogram log analysis: See output above")
    print("  - Full log files:")
    print(f"    - Firmware: {SKILLS_DIR.parent}\\logs\\serial\\")
    print(f"    - Miniprogram: {SKILLS_DIR.parent}\\logs\\mini\\")


def main():
    """
    Main function: Parse command line arguments, call corresponding workflow
    """
    if len(sys.argv) < 2:
        print("Usage: python workflow.py [deploy|analyze]")
        print("\nAvailable workflows:")
        print("  1. deploy   - Upload and compile (deploy)")
        print("  2. analyze  - Analyze all logs (debug)")
        sys.exit(1)

    action = sys.argv[1].lower()
    
    if action == "deploy":
        deploy()
    elif action == "analyze":
        analyze()
    else:
        print(f"[ERR] Unknown action: {action}")
        print("\nSupported actions:")
        print("  - deploy  : Upload and compile")
        print("  - analyze : Analyze all logs")
        sys.exit(1)


if __name__ == "__main__":
    main()
