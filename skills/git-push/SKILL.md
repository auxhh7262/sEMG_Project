---
name: git-push
description: sEMG项目Git提交推送工具。当用户说"提交代码"、"git push"、"推送代码"、"git commit"、"保存到GitHub"、"同步到仓库"、"提交更改"时触发此skill。自动设置公司代理、检测改动、add/commit/push、推送完成后清除代理。支持自定义commit message。
---

# git-push

sEMG项目一键Git提交推送skill。解决公司网络需临时代理才能访问GitHub的痛点。

## 工作流程

```
1. 设置临时HTTP代理（公司网络访问GitHub需要）
2. git status 检测当前改动
3. git add 全部改动（或用户指定的文件）
4. git commit（自动生成message或用户指定）
5. git push origin main
6. 清除代理设置（避免影响其他网络访问）
```

## 代理机制

公司网络封禁 github.com:443，需要通过 `http://shproxy.asrmicro.com:80` 代理访问。

- push前设置：`git config --global http.proxy http://shproxy.asrmicro.com:80`
- push后清除：`git config --global --unset http.proxy`
- **只影响本次操作**，不会残留代理配置

## 脚本说明

| 脚本 | 作用 |
|------|------|
| `git_push.py` | 完整流程：设代理 → 检测改动 → add → commit → push → 清代理 |

## 路径配置

| 配置项 | 值 |
|--------|-----|
| 项目根目录 | `E:\Personal\sEMG_Project\` |
| Git可执行文件 | `C:\Git\cmd\git.exe`（junction指向GitHub Desktop内置Git） |
| GitHub代理 | `http://shproxy.asrmicro.com:80` |
| 远程仓库 | `origin` |
| 默认分支 | `main` |

## 使用方法

### 默认模式（自动生成commit message）
```bash
python git_push.py
```
自动检测改动，生成 `chore: YYYY-MM-DD HH:MM 提交` 格式的message。

### 自定义commit message
```bash
python git_push.py -m "feat: 添加XXX功能"
python git_push.py -m "fix: 修复XXX bug"
```

### 只提交指定文件
```bash
python git_push.py -f "firmware/src/ProtocolHandler.cpp" -m "fix: 修复命令处理"
python git_push.py -f "mini_program/" -m "feat: 小程序UI更新"
```

### 指定文件+自动message
```bash
python git_push.py -f "firmware/src/"
```

### 只看改动，不提交
```bash
python git_push.py --dry-run
```

## Commit Message 规范

| 前缀 | 用途 |
|------|------|
| `feat:` | 新功能 |
| `fix:` | Bug修复 |
| `chore:` | 构建/工具/文档等非业务改动 |
| `refactor:` | 代码重构（不改功能） |
| `docs:` | 文档更新 |
| `style:` | 格式调整（不影响逻辑） |
| `perf:` | 性能优化 |

## 注意事项

1. Git路径通过junction `C:\Git` 指向GitHub Desktop内置Git（v2.53.0）
2. 如果PATH未更新，脚本会直接使用 `C:\Git\cmd\git.exe`
3. 代理只在push期间生效，完成后立即清除
4. 如果push失败，代理仍会被清除（安全优先）
5
