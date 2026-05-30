# git-push 使用说明

Git 提交推送工具（支持公司网络代理）。

---

## 📋 触发词

- "提交代码"
- "git push"
- "推送代码"
- "git commit"
- "保存到 GitHub"
- "同步到仓库"
- "提交更改"

---

## 🚀 功能说明

### 1️⃣ 自动设置公司代理

**问题：** 公司网络封禁 `github.com:443`，直接 push 会失败。

**解决：** Skill 自动设置代理：
```powershell
git config --global http.proxy http://shproxy.asrmicro.com:80
```

**完成后清除：**
```powershell
git config --global --unset http.proxy
```

**只影响本次操作**，不会残留代理配置。

---

### 2️⃣ 检测改动

**执行命令：**
```powershell
git status
```

**输出：**
- 显示已修改的文件
- 显示未跟踪的文件
- 显示待提交的文件

---

### 3️⃣ 提交 + 推送

**执行命令：**
```powershell
git add .
git commit -m "chore: YYYY-MM-DD HH:MM 提交"
git push origin main
```

**完成后：**
- 清除代理配置
- 输出 push 结果

---

## 📦 使用方式

### 方式1：语音指令（推荐）

```
说："提交代码" 或 "git push"
→ 自动执行完整流程
```

---

### 方式2：命令行

```powershell
# 默认模式（自动生成 commit message）
python "E:\Personal\sEMG_Project\skills\git-push\push.py"

# 自定义 commit message
python "E:\Personal\sEMG_Project\skills\git-push\push.py" -m "feat: 添加XXX功能"

# 只提交指定文件
python "E:\Personal\sEMG_Project\skills\git-push\push.py" -f "firmware/src/"

# 只看改动，不提交
python "E:\Personal\sEMG_Project\skills\git-push\push.py" --dry-run
```

---

## 📊 输出示例

### 默认模式（自动生成 commit message）

```
============================================================
  sEMG Git Push Tool
============================================================
  [1/5] Setting proxy...
        Proxy: http://shproxy.asrmicro.com:80

  [2/5] Checking changes...
        Modified:   firmware/src/ProtocolHandler.cpp
        Modified:   mini_program/pages/realtime/index.js
        Untracked:  skills/semg-workflow/workflow.py

  [3/5] Adding files...
        Added: firmware/src/ProtocolHandler.cpp
        Added: mini_program/pages/realtime/index.js
        Added: skills/semg-workflow/workflow.py

  [4/5] Committing...
        Commit: chore: 2026-05-30 23:05 提交

  [5/5] Pushing to origin/main...
        Push: 600248a..38d3ddb  main -> main

  [DONE] Git push completed successfully!
        Clearing proxy...
        Proxy cleared.
============================================================
```

---

### 自定义 commit message

```powershell
python "E:\Personal\sEMG_Project\skills\git-push\push.py" -m "fix: 修复校准超时问题"
```

**输出：**
```
...
  [4/5] Committing...
        Commit: fix: 修复校准超时问题
...
```

---

### 只提交指定文件

```powershell
python "E:\Personal\sEMG_Project\skills\git-push\push.py" -f "firmware/src/" -m "fix: 修复命令处理"
```

**输出：**
```
...
  [2/5] Checking changes...
        Specified: firmware/src/ProtocolHandler.cpp
        Specified: firmware/src/AppController.cpp
...
```

---

### dry-run 模式（只看改动，不提交）

```powershell
python "E:\Personal\sEMG_Project\skills\git-push\push.py" --dry-run
```

**输出：**
```
============================================================
  sEMG Git Push Tool (dry-run mode)
============================================================
  [1/5] Checking changes...
        Modified:   firmware/src/ProtocolHandler.cpp
        Modified:   mini_program/pages/realtime/index.js
        Untracked:  skills/semg-workflow/workflow.py

  [DRY-RUN] No changes committed.
============================================================
```

---

## ⚙️ 配置说明

### 路径配置

| 配置项 | 值 |
|--------|-----|
| 项目根目录 | `E:\Personal\sEMG_Project\` |
| Git 可执行文件 | `C:\Git\cmd\git.exe`（junction 指向 GitHub Desktop 内置 Git） |
| GitHub 代理 | `http://shproxy.asrmicro.com:80` |
| 远程仓库 | `origin` |
| 默认分支 | `main` |

### Commit Message 规范

| 前缀 | 用途 |
|------|------|
| `feat:` | 新功能 |
| `fix:` | Bug 修复 |
| `chore:` | 构建/工具/文档等非业务改动 |
| `refactor:` | 代码重构（不改功能） |
| `docs:` | 文档更新 |
| `style:` | 格式调整（不影响逻辑） |
| `perf:` | 性能优化 |

---

## 🚨 常见问题

### 1. Push 失败（公司网络）

**现象：**
```
fatal: unable to access 'https://github.com/auxhh7262/sEMG_Project.git': Failed to connect to github.com port 443
```

**原因：** 公司网络封禁 `github.com:443`，需要代理。

**解决：**
- 说 "提交代码"，Skill 会自动设置代理
- 或手动设置代理：
  ```powershell
  git config --global http.proxy http://shproxy.asrmicro.com:80
  git push origin main
  git config --global --unset http.proxy
  ```

---

### 2. 代理设置失败

**现象：**
```
error: could not lock config file .git/config
```

**原因：** 另一个 Git 进程正在运行，占用了配置文件。

**解决：**
- 关闭所有 Git 进程（GitHub Desktop、Git Bash 等）
- 重试 "提交代码"

---

### 3. Push 成功后代理没清除

**现象：** Push 成功后，代理配置还在。

**原因：** Push 过程中脚本异常退出，没执行到清除代理的步骤。

**解决：**
```powershell
# 手动清除代理
git config --global --unset http.proxy

# 检查代理配置
git config --global --get http.proxy
```

---

### 4. 想回退上一次提交

**现象：** 提交后发现错了，想撤销。

**解决：**
```powershell
# 回退提交（保留改动）
git reset --soft HEAD~1

# 回退提交（不保留改动）
git reset --hard HEAD~1

# 强制推送（慎用）
git push origin main --force
```

---

## 📚 详细文档

- **Skill 体系概览：** [`../README.md`](../README.md)
- **组合工作流：** [`../semg-workflow/USAGE.md`](../semg-workflow/USAGE.md)

---

## 🤝 贡献

欢迎提交 Issue 和 Pull Request！

**新的想法？**
- 在 GitHub Issues 提出你的想法
- 或直接修改 `push.py` 添加新功能

---

## 📄 许可证

MIT License
