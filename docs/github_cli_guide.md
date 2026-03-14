# GitHub CLI 使用指南

## 什么是 GitHub CLI？

GitHub CLI 是 GitHub 官方提供的命令行工具，允许你直接从终端与 GitHub 交互，无需打开浏览器。它可以帮助你更高效地管理仓库、创建 issues、提交 pull requests 等。

## 安装 GitHub CLI

### Windows

1. 访问 [GitHub CLI 官方下载页面](https://cli.github.com/download/)
2. 下载并安装 Windows 版本
3. 验证安装：
   ```powershell
   gh --version
   ```

### 在 Docker 容器中

本项目的 Docker 镜像已包含 GitHub CLI，你可以直接在容器内使用。

## 登录 GitHub

### 在 Windows 中

```powershell
# 打开 GitHub CLI 终端
E:\it-gets-you-better-than-her\scripts\open_github_cli.bat

# 然后执行登录命令
gh auth login
```

### 在 Docker 容器中

```bash
# 进入容器
docker exec -it A1_Builder bash

# 执行登录命令
gh auth login
```

## 常用 GitHub CLI 命令

### 仓库操作

```bash
# 查看当前仓库信息
gh repo view

# 克隆仓库
gh repo clone owner/repo

# 查看仓库状态
gh status
```

### Issues 管理

```bash
# 创建新 issue
gh issue create

# 查看 issue 列表
gh issue list

# 查看特定 issue
gh issue view ISSUE_NUMBER

# 关闭 issue
gh issue close ISSUE_NUMBER
```

### Pull Requests 管理

```bash
# 创建新 PR
gh pr create

# 查看 PR 列表
gh pr list

# 检查 PR
gh pr checkout PR_NUMBER

# 合并 PR
gh pr merge PR_NUMBER
```

### 分支管理

```bash
# 查看分支
gh branch

# 创建新分支
gh branch BRANCH_NAME

# 切换分支
gh checkout BRANCH_NAME
```

## 使用自然语言描述 Bug 修复

GitHub CLI 支持使用自然语言描述来创建 issues 和 PRs，这使得描述 bug 修复更加直观和高效。

### 示例：创建一个关于 bug 修复的 issue

```bash
gh issue create --title "修复避障算法中的边界情况" --body "当障碍物距离小于0.5米时，避障算法会出现误判，需要修复这个边界情况。"
```

### 示例：创建一个包含 bug 修复的 PR

```bash
gh pr create --title "fix: 解决避障算法边界情况问题" --body "- 修复了障碍物距离小于0.5米时的误判问题
- 优化了距离计算逻辑
- 添加了边界情况的单元测试"
```

### 示例：使用自然语言描述复杂的 bug 修复

```bash
gh issue create --title "修复目标跟踪在低光照环境下的性能问题" --body "在低光照环境下，目标跟踪算法的准确率显著下降，主要表现为：
1. 目标丢失率增加
2. 跟踪框抖动严重
3. 处理速度变慢

建议的修复方向：
- 优化图像预处理步骤，提高低光照下的图像质量
- 调整跟踪算法的参数，使其更适应低光照环境
- 考虑添加光照检测机制，根据环境自动调整算法参数"
```

## 一键启动脚本

本项目提供了一个 Windows 下的一键启动脚本，用于在 src 目录下打开 GitHub CLI 终端：

```powershell
# 运行脚本
E:\it-gets-you-better-than-her\scripts\open_github_cli.bat
```

脚本会自动导航到 src 目录，检查 GitHub CLI 是否安装，并显示常用命令示例。

## 集成到开发流程

在日常开发中，你可以使用 GitHub CLI 来：

1. **快速创建 issues**：当发现 bug 或需要新功能时
2. **管理 PRs**：创建、查看和合并 pull requests
3. **同步仓库**：拉取最新代码，推送本地更改
4. **查看仓库状态**：了解当前仓库的最新动态
5. **协作管理**：查看团队成员的贡献和活动

通过 GitHub CLI，你可以更高效地与 GitHub 交互，减少在浏览器和终端之间切换的次数，提高开发效率。

## 高级功能

### 使用 GitHub Copilot CLI（预览）

GitHub CLI 集成了 GitHub Copilot，允许你使用自然语言来生成命令：

```bash
# 启动 Copilot CLI
gh copilot

# 示例：使用自然语言生成 git 命令
> 如何查看最近 5 次提交
git log -n 5

# 示例：使用自然语言生成 GitHub 命令
> 创建一个新的 issue 关于性能优化
gh issue create --title "性能优化建议" --body "建议优化系统的启动时间和内存使用情况"
```

### 自定义命令别名

你可以为常用的命令创建别名，提高工作效率：

```bash
# 创建别名
gh alias set co pr checkout

# 使用别名
gh co 123  # 等同于 gh pr checkout 123
```

## 故障排除

### 登录问题

如果登录失败，可以尝试：

```bash
# 清除现有认证
gh auth logout

# 重新登录
gh auth login
```

### 权限问题

如果遇到权限问题，可以检查当前认证状态：

```bash
# 查看当前认证状态
gh auth status

# 查看当前用户
gh api user | jq '.login'
```

## 总结

GitHub CLI 是一个强大的工具，可以帮助你更高效地与 GitHub 交互。通过本文档的介绍，你应该已经了解了如何安装、登录和使用 GitHub CLI，以及如何将其集成到你的开发流程中。

开始使用 GitHub CLI 吧，它会让你的开发工作更加高效和愉快！