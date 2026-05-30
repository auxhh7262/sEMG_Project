#!/usr/bin/env python3
"""sEMG Project Git Push Tool

Workflow: set proxy -> detect changes -> add -> commit -> push -> clear proxy
Usage:
    python git_push.py                  # auto commit message
    python git_push.py -m "feat: xxx"   # custom message
    python git_push.py -f "path"        # specific files
    python git_push.py --dry-run         # show changes only
"""

import subprocess
import sys
import os
import argparse
from datetime import datetime
from pathlib import Path

# --- Config ---
PROJECT_DIR = Path(r"E:\Personal\sEMG_Project")
GIT_EXE = r"C:\Git\cmd\git.exe"
PROXY = "http://shproxy.asrmicro.com:80"
REMOTE = "origin"
BRANCH = "main"


def run_git(*args, check=True, capture=True):
    """Run a git command and return result."""
    cmd = [GIT_EXE] + list(args)
    cwd = str(PROJECT_DIR)
    env = os.environ.copy()
    if capture:
        result = subprocess.run(
            cmd, cwd=cwd, env=env, capture_output=True,
            text=True, encoding="utf-8", errors="replace"
        )
        if check and result.returncode != 0:
            return result
        return result
    else:
        result = subprocess.run(
            cmd, cwd=cwd, env=env
        )
        return result


def set_proxy():
    """Set temporary HTTP proxy for GitHub access."""
    run_git("config", "--global", "http.proxy", PROXY, check=False)
    print(f"[proxy] set http.proxy = {PROXY}")


def clear_proxy():
    """Clear proxy settings after push."""
    run_git("config", "--global", "--unset", "http.proxy", check=False)
    run_git("config", "--global", "--unset", "https.proxy", check=False)
    print("[proxy] cleared http.proxy and https.proxy")


def git_status():
    """Get git status output."""
    return run_git("status", "--porcelain", "--short")


def git_add(paths):
    """Stage files."""
    if paths:
        for p in paths:
            result = run_git("add", p)
            if result.returncode == 0:
                print(f"[add] {p}")
    else:
        result = run_git("add", "-A")
        if result.returncode == 0:
            print("[add] all changes")


def git_commit(message):
    """Commit staged changes."""
    result = run_git("commit", "-m", message)
    if result.returncode == 0:
        print(f"[commit] {message}")
        return True
    else:
        print(f"[commit ERROR] {result.stderr.strip()}")
        return False


def git_push():
    """Push to remote."""
    result = run_git("push", REMOTE, BRANCH, check=False)
    if result.returncode == 0:
        print(f"[push] {REMOTE}/{BRANCH} success")
        return True
    else:
        print(f"[push ERROR] {result.stderr.strip()}")
        return False


def auto_message():
    """Generate auto commit message based on timestamp."""
    now = datetime.now().strftime("%Y-%m-%d %H:%M")
    return f"chore: {now} auto commit"


def resolve_paths(file_args):
    """Resolve file arguments relative to project dir."""
    if not file_args:
        return None
    paths = []
    for f in file_args:
        p = Path(f)
        if p.is_absolute():
            paths.append(str(p))
        else:
            paths.append(str(PROJECT_DIR / f))
    return paths


def main():
    parser = argparse.ArgumentParser(description="sEMG Git Push Tool")
    parser.add_argument("-m", "--message", help="Commit message")
    parser.add_argument("-f", "--files", nargs="+", help="Files to commit (relative or absolute)")
    parser.add_argument("--dry-run", action="store_true", help="Show changes only, no commit/push")
    args = parser.parse_args()

    # Verify git exists
    if not os.path.isfile(GIT_EXE):
        print(f"[ERROR] Git not found at {GIT_EXE}")
        print(f"Please ensure the junction C:\\Git exists and points to GitHub Desktop's git.")
        sys.exit(1)

    # Check project dir
    if not PROJECT_DIR.exists():
        print(f"[ERROR] Project dir not found: {PROJECT_DIR}")
        sys.exit(1)

    # Show git version
    ver = run_git("--version")
    print(f"[git] {ver.stdout.strip()}")

    # Check changes
    status = git_status()
    if status.returncode != 0:
        print(f"[ERROR] git status failed: {status.stderr.strip()}")
        sys.exit(1)

    lines = [l for l in status.stdout.strip().split("\n") if l.strip()]
    if not lines:
        print("[info] No changes detected. Nothing to commit.")
        sys.exit(0)

    # Show changes
    print(f"\n[changes] {len(lines)} file(s) changed:")
    for line in lines:
        status_char = line[:2]
        filepath = line[3:]
        icon = {"??": "?", "M ": "~", " M": "~", "A ": "+", "D ": "-",
                "AM": "~", "MM": "~"}.get(status_char, "*")
        print(f"  {icon} {filepath}")

    if args.dry_run:
        print("\n[dry-run] Done. No changes committed.")
        sys.exit(0)

    # Resolve files
    paths = resolve_paths(args.files)

    # Set proxy
    print()
    set_proxy()

    # Add
    git_add(paths if paths else None)

    # Commit
    message = args.message or auto_message()
    success = git_commit(message)
    if not success:
        clear_proxy()
        sys.exit(1)

    # Push
    print()
    success = git_push()

    # Clear proxy (always)
    clear_proxy()

    if success:
        print(f"\n[done] Committed and pushed to {REMOTE}/{BRANCH}")
    else:
        print(f"\n[done] Committed locally but push failed. Proxy cleared.")
        print("Retry: python git_push.py (will only push, nothing new to commit)")
        sys.exit(1)


if __name__ == "__main__":
    main()
