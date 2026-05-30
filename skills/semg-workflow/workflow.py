#!/usr/bin/env python3
"""
semg-workflow: sEMG 项目组合工作流
支持 2 个工作流：
  1. deploy（上传固件 + 编译小程序）
  2. analyze（分析固件日志 + 小程序日志）
"""

import sys
import time
import subprocess
from pathlib import Path

# 项目根目录（2 级上溯：semg-workflow -> skills -> sEMG_Project）
SKILLS_DIR = Path(__file__).parent.parent

# 子 Skill 脚本路径
FIRMWARE_UPLOAD_SCRIPT = SKILLS_DIR / "firmware-upload" / "upload_and_monitor.py"
MINIPROGRAM_PREVIEW_SCRIPT = SKILLS_DIR / "miniprogram-preview" / "preview.py"
FIRMWARE_DEBUG_SCRIPT = SKILLS_DIR / "firmware-debug" / "analyze_log.py"
MINIPROGRAM_DEBUG_SCRIPT = SKILLS_DIR / "miniprogram-debug" / "analyze_mini_log.py"


def deploy():
    """
    工作流1：上传并编译（部署类）
    执行顺序：firmware-upload -> miniprogram-preview
    """
    print("=" * 60)
    print("🚀 工作流1：上传并编译 (Deploy)")
    print("=" * 60)

    # 步骤1：上传固件
    print("\n【步骤 1/2】上传固件...")
    print(f"  脚本：{FIRMWARE_UPLOAD_SCRIPT}")
    
    result1 = subprocess.run(
        ["python", str(FIRMWARE_UPLOAD_SCRIPT)],
        cwd=str(SKILLS_DIR / "firmware-upload"),
        capture_output=False  # 允许 GUI 窗口弹出
    )
    
    if result1.returncode != 0:
        print("❌ 固件上传失败")
        print(f"   返回码：{result1.returncode}")
        sys.exit(1)
    
    print("✅ 固件上传成功")
    print("   - 串口监控 GUI 已启动")
    print("   - 固件正在重启...")

    # 等待固件重启（给点时间）
    print("   - 等待固件重启（3 秒）...")
    time.sleep(3)

    # 步骤2：编译小程序
    print("\n【步骤 2/2】编译小程序...")
    print(f"  脚本：{MINIPROGRAM_PREVIEW_SCRIPT}")
    
    result2 = subprocess.run(
        ["python", str(MINIPROGRAM_PREVIEW_SCRIPT)],
        cwd=str(SKILLS_DIR / "miniprogram-preview"),
        capture_output=False  # 允许 GUI 窗口弹出
    )
    
    if result2.returncode != 0:
        print("❌ 小程序编译失败")
        print(f"   返回码：{result2.returncode}")
        sys.exit(1)
    
    print("✅ 小程序编译成功")
    print("   - 日志服务器 GUI 已启动")
    print("   - 预览码已推送到手机")

    # 完成
    print("\n" + "=" * 60)
    print("🎉 工作流完成！")
    print("=" * 60)
    print("\n📊 运行状态：")
    print("  - 串口监控 GUI 运行中（固件日志）")
    print("  - 小程序日志服务器 GUI 运行中（小程序日志）")
    print("\n💡 提示：")
    print("  - 查看固件日志：串口监控 GUI 窗口")
    print("  - 查看小程序日志：日志服务器 GUI 窗口")
    print("  - 预览码已推送到手机微信")


def analyze():
    """
    工作流2：分析所有日志（调试类）
    执行顺序：firmware-debug -> miniprogram-debug
    """
    print("=" * 60)
    print("🔍 工作流2：分析所有日志 (Analyze)")
    print("=" * 60)

    # 步骤1：分析固件日志
    print("\n【步骤 1/2】分析固件日志...")
    print(f"  脚本：{FIRMWARE_DEBUG_SCRIPT}")
    
    result1 = subprocess.run(
        ["python", str(FIRMWARE_DEBUG_SCRIPT)],
        cwd=str(SKILLS_DIR / "firmware-debug"),
        capture_output=False  # 允许交互
    )
    
    if result1.returncode != 0:
        print("⚠️ 固件日志分析失败（可能无日志文件）")
        print(f"    返回码：{result1.returncode}")
        # 不退出，继续分析小程序日志
    else:
        print("✅ 固件日志分析完成")

    # 步骤2：分析小程序日志
    print("\n【步骤 2/2】分析小程序日志...")
    print(f"  脚本：{MINIPROGRAM_DEBUG_SCRIPT}")
    
    result2 = subprocess.run(
        ["python", str(MINIPROGRAM_DEBUG_SCRIPT)],
        cwd=str(SKILLS_DIR / "miniprogram-debug"),
        capture_output=False  # 允许交互
    )
    
    if result2.returncode != 0:
        print("⚠️ 小程序日志分析失败（可能无日志文件）")
        print(f"    返回码：{result2.returncode}")
    else:
        print("✅ 小程序日志分析完成")

    # 完成
    print("\n" + "=" * 60)
    print("📊 联合分析报告已生成")
    print("=" * 60)
    print("\n💡 提示：")
    print("  - 固件日志分析：见上方输出")
    print("  - 小程序日志分析：见上方输出")
    print("  - 完整日志文件：")
    print(f"    - 固件：{SKILLS_DIR.parent}\\logs\\serial\\")
    print(f"    - 小程序：{SKILLS_DIR.parent}\\logs\\mini\\")


def main():
    """
    主函数：解析命令行参数，调用对应工作流
    """
    if len(sys.argv) < 2:
        print("用法：python workflow.py [deploy|analyze]")
        print("\n可用工作流：")
        print("  1. deploy   - 上传并编译（部署类）")
        print("  2. analyze  - 分析所有日志（调试类）")
        sys.exit(1)

    action = sys.argv[1].lower()
    
    if action == "deploy":
        deploy()
    elif action == "analyze":
        analyze()
    else:
        print(f"❌ 未知工作流：{action}")
        print("\n支持的工作流：")
        print("  - deploy  : 上传并编译")
        print("  - analyze : 分析所有日志")
        sys.exit(1)


if __name__ == "__main__":
    main()
