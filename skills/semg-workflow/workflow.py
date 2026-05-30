#!/usr/bin/env python3
"""
semg-workflow: sEMG 
 2 
  1. deploy + 
  2. analyze + 
"""

import sys
import time
import subprocess
from pathlib import Path

# 2 semg-workflow -> skills -> sEMG_Project
SKILLS_DIR = Path(__file__).parent.parent

#  Skill 
FIRMWARE_UPLOAD_SCRIPT = SKILLS_DIR / "firmware-upload" / "upload_and_monitor.py"
MINIPROGRAM_PREVIEW_SCRIPT = SKILLS_DIR / "miniprogram-preview" / "preview.py"
FIRMWARE_DEBUG_SCRIPT = SKILLS_DIR / "firmware-debug" / "analyze_log.py"
MINIPROGRAM_DEBUG_SCRIPT = SKILLS_DIR / "miniprogram-debug" / "analyze_mini_log.py"


def deploy():
    """
    1
    firmware-upload -> miniprogram-preview
    """
    print("=" * 60)
    print(" 1 (Deploy)")
    print("=" * 60)

    # 1
    print("\n 1/2...")
    print(f"  {FIRMWARE_UPLOAD_SCRIPT}")
    
    result1 = subprocess.run(
        ["python", str(FIRMWARE_UPLOAD_SCRIPT)],
        cwd=str(SKILLS_DIR / "firmware-upload"),
        capture_output=False  #  GUI 
    )
    
    if result1.returncode != 0:
        print(" ")
        print(f"   {result1.returncode}")
        sys.exit(1)
    
    print(" ")
    print("   -  GUI ")
    print("   - ...")

    # 
    print("   - 3 ...")
    time.sleep(3)

    # 2
    print("\n 2/2...")
    print(f"  {MINIPROGRAM_PREVIEW_SCRIPT}")
    
    result2 = subprocess.run(
        ["python", str(MINIPROGRAM_PREVIEW_SCRIPT)],
        cwd=str(SKILLS_DIR / "miniprogram-preview"),
        capture_output=False  #  GUI 
    )
    
    if result2.returncode != 0:
        print(" ")
        print(f"   {result2.returncode}")
        sys.exit(1)
    
    print(" ")
    print("   -  GUI ")
    print("   - ")

    # 
    print("\n" + "=" * 60)
    print(" ")
    print("=" * 60)
    print("\n ")
    print("  -  GUI ")
    print("  -  GUI ")
    print("\n ")
    print("  -  GUI ")
    print("  -  GUI ")
    print("  - ")


def analyze():
    """
    2
    firmware-debug -> miniprogram-debug
    """
    print("=" * 60)
    print(" 2 (Analyze)")
    print("=" * 60)

    # 1
    print("\n 1/2...")
    print(f"  {FIRMWARE_DEBUG_SCRIPT}")
    
    result1 = subprocess.run(
        ["python", str(FIRMWARE_DEBUG_SCRIPT)],
        cwd=str(SKILLS_DIR / "firmware-debug"),
        capture_output=False  # 
    )
    
    if result1.returncode != 0:
        print(" ")
        print(f"    {result1.returncode}")
        # 
    else:
        print(" ")

    # 2
    print("\n 2/2...")
    print(f"  {MINIPROGRAM_DEBUG_SCRIPT}")
    
    result2 = subprocess.run(
        ["python", str(MINIPROGRAM_DEBUG_SCRIPT)],
        cwd=str(SKILLS_DIR / "miniprogram-debug"),
        capture_output=False  # 
    )
    
    if result2.returncode != 0:
        print(" ")
        print(f"    {result2.returncode}")
    else:
        print(" ")

    # 
    print("\n" + "=" * 60)
    print(" ")
    print("=" * 60)
    print("\n ")
    print("  - ")
    print("  - ")
    print("  - ")
    print(f"    - {SKILLS_DIR.parent}\\logs\\serial\\")
    print(f"    - {SKILLS_DIR.parent}\\logs\\mini\\")


def main():
    """
    
    """
    if len(sys.argv) < 2:
        print("python workflow.py [deploy|analyze]")
        print("\n")
        print("  1. deploy   - ")
        print("  2. analyze  - ")
        sys.exit(1)

    action = sys.argv[1].lower()
    
    if action == "deploy":
        deploy()
    elif action == "analyze":
        analyze()
    else:
        print(f" {action}")
        print("\n")
        print("  - deploy  : ")
        print("  - analyze : ")
        sys.exit(1)


if __name__ == "__main__":
    main()
