# 🤖 IT-Gets-You-Better-Than-Her

> A1 Vision Pi 端侧智能视觉系统 — 基于思特威SC132GS + 飞凌微A1开发套件

[![GitHub Issues](https://img.shields.io/github/issues/GitLaughs/it-gets-you-better-than-her "查看项目Issues")](https://github.com/GitLaughs/it-gets-you-better-than-her/issues)
[![Docker](https://img.shields.io/badge/Docker-Supported-blue "查看Docker相关配置")](./docker/)

***

## 📋 功能列表

| # | 功能模块        | 说明               | 优先级 |
| - | ----------- | ---------------- | --- |
| 1 | YOLOv8 目标检测 | 基于NPU加速的实时目标检测   | ⭐⭐⭐ |
| 2 | 目标跟踪        | ByteTrack 多目标跟踪  | ⭐⭐⭐ |
| 3 | 单目深度估计      | 轻量级单目深度感知        | ⭐⭐⭐ |
| 4 | 三维点云可视化     | 深度图转点云 + 外设屏幕输出  | ⭐⭐  |
| 5 | 自身定位与目标坐标   | 视觉里程计 + 目标3D坐标估计 | ⭐⭐⭐ |
| 6 | 避障算法        | 基于深度图的实时避障       | ⭐⭐⭐ |
| 7 | 灵巧手接口       | UART/I2C 外设控制接口  | ⭐⭐  |
| 8 | HDR模式切换     | 光线自适应 HDR 控制     | ⭐⭐  |
| 9 | 异常处理系统      | 摄像头/推理/资源 三级异常保护 | ⭐⭐  |

***

## 📁 项目目录结构

```
it-gets-you-better-than-her/         ← GitHub 仓库根目录
├── src/                             ← ✅ 项目源代码（git 管理）
│   ├── scripts/                     ← 构建和刷写脚本
│   │   ├── build.sh                 ← 编译脚本
│   │   └── flash.sh                 ← 刷写脚本
│   ├── src/                         ← 核心源代码
│   │   ├── config/                  ← 配置管理
│   │   ├── core/                    ← 核心功能模块
│   │   ├── utils/                   ← 工具类
│   │   └── main.cpp                 ← 主入口
│   ├── tests/                       ← 单元测试
│   ├── CMakeLists.txt               ← CMake 构建配置
│   ├── Makefile                     ← Make 构建配置
│   └── config.yaml                  ← 系统配置文件
├── tests/                           ← 集成测试（git 管理）
├── docker/                          ← ✅ Docker 配置（git 管理）
│   ├── Dockerfile
│   ├── docker-compose.yml
│   └── entrypoint.sh
├── docs/                            ← 文档（git 管理）
│   └── windows_docker_dev_guide.md  ← Windows Docker 开发指南
├── models/                          ← 模型目录（git 管理）
├── scripts/                         ← 辅助脚本（git 管理）
├── .devcontainer/                   ← VS Code 开发容器配置
├── .github/                         ← GitHub 配置
├── .gitignore
├── README.md
├── requirements.txt                 ← Python 依赖（如需）
│
├── a1-sdk-builder-latest.tar        ← ❌ 不提交（需手动分发，见下方说明）
└── data/                            ← ❌ 不提交（.gitignore 已排除）
    └── a1_sdk_sc132gs/              ← 官方 SDK（需手动 clone，见下方说明）
```

> **说明：**
> 
> - `data/` 是 Docker 的挂载点，存放官方 SDK，体积大且属于第三方，不放入 Git
> - `a1-sdk-builder-latest.tar` 是 Docker 镜像文件，需通过网盘或内网单独分发
> - `src/` 下的所有代码实时同步到容器内，无需手动复制
> - `models/` 目录用于存放模型文件（如 YOLOv8、Midas 等）

***

## 🚀 首次环境搭建（每人只做一次）

### 前置条件

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) 已安装并运行
- [Git](https://git-scm.com/) 已安装
- [VS Code](https://code.visualstudio.com/) + **Dev Containers** 插件（微软官方）

### 第一步：克隆仓库

```bash
git clone https://github.com/GitLaughs/it-gets-you-better-than-her.git
cd it-gets-you-better-than-her
```

### 第二步：获取并放置 Docker 镜像文件

`a1-sdk-builder-latest.tar` 需要从钉钉/论坛获取，放到仓库根目录：

```
it-gets-you-better-than-her/
└── a1-sdk-builder-latest.tar    ← 放这里
```

加载镜像：

```powershell
# Windows PowerShell 中执行
docker load -i "a1-sdk-builder-latest.tar"

# 验证是否加载成功
docker images
# 应该能看到 a1-sdk-builder 这一行
```

### 第三步：克隆官方 SDK

```powershell
mkdir data
cd data
git clone --depth 1  https://git.smartsenstech.ai/Smartsens/A1_SDK_SC132GS.git
cd ..
```

### 第四步：启动容器

```powershell
# Windows PowerShell — 使用绝对路径（./相对路径在 Windows 下可能失效）
docker run -itd --name A1_Builder `
  -v "${PWD}\data:/app/smartsens_sdk" `
  -v "${PWD}\src:/app/src" `
  a1-sdk-builder

# 验证挂载是否成功
docker exec -it A1_Builder /bin/bash
ls /app/src                      # 应看到 src/ 下的代码
ls /app/smartsens_sdk            # 应看到 a1_sdk_sc132gs/
exit

#或者
# 使用 Docker Compose 启动（推荐多容器场景）
cd docker
docker-compose up -d
```

> **路径说明：**
> Windows 的 `E:\...\src` → 容器内的 `/app/src`（实时双向同步，不是拷贝）

***

## 🔄 实际构建流程

```
┌───────────────────────────┐
│ a1-sdk-builder:latest     │  ← 官方镜像（tar 文件导入）
│  - 编译工具链              │
│  - buildroot              │
│  - 交叉编译器              │
└─────────┬─────────────────┘
          │  docker run 挂载
          ▼
┌───────────────────────────┐
│ 容器: A1_Builder          │  ← 运行中
│  /app/src/               │  ← 挂载自 src/（你的代码）
│  /app/smartsens_sdk/     │  ← 挂载自 data/（官方SDK）
│  /app/models/            │  ← 挂载自 models/（模型）
│  /app/output/            │  ← 挂载自 output/（输出）
└───────────────────────────┘
          ↕ 实时同步（非拷贝）
┌───────────────────────────┐
│ Windows 宿主机             │
│  src/        （写代码）    │
│  data/       （SDK）      │
│  models/     （模型）      │
│  output/     （输出）      │
└───────────────────────────┘
```

### 构建命令

```bash
# 在容器内编译视觉系统
cd /app/src

# 使用 Makefile 构建
./scripts/build.sh --type release

# 或使用 CMake 构建
./scripts/build.sh --cmake --type release

# 刷写到目标设备
./scripts/flash.sh --host 192.168.1.100 --method ssh
```

***

## 💻 日常开发流程（VS Code + Dev Containers）

### 连接容器（推荐方式）

1. 确认容器正在运行：`docker ps`（看到 `A1_Builder`）
2. VS Code 左下角点击 **`><`** 图标
3. 选择 **Attach to Running Container**
4. 选择 **/A1\_Builder**
5. 新窗口打开后：**File → Open Folder →** **`/app/src`**

现在 VS Code 的编辑器和底部终端**都在容器环境内**，可以直接编译运行。

### 每天的工作循环

```bash
# ① 开始工作前，拉取队友最新代码（在宿主机终端或 VS Code 终端）
git pull

# ② 用 VS Code（连接容器后）写代码，保存即实时同步到容器

# ③ 在容器内终端编译运行
cd /app/src
./scripts/build.sh --type release  # 编译

# 运行测试
cd /app/src
./tests/test_config                   # 运行配置测试
./tests/test_obstacle                 # 运行避障测试
./tests/test_tracker                  # 运行跟踪测试

# ④ 完成后提交（在宿主机 Git 提交）
git add .
git commit -m "feat: 描述你做了什么"
git push
```

### 容器管理常用命令

```bash
docker start A1_Builder          # 启动已有容器（电脑重启后需要）
docker stop A1_Builder           # 停止容器
docker exec -it A1_Builder bash  # 进入正在运行的容器
docker ps                        # 查看运行中的容器
docker ps -a                     # 查看所有容器（含已停止）
```

***

## 🚀 如何在 Docker 里运行（完整步骤）

### 1. 进入项目目录
```bash
cd it-gets-you-better-than-her
```

### 2. 创建 .env 文件（填入你的 Discord Bot Token）
```bash
cp .env.example .env
# 然后编辑 .env，把 your_discord_token_here 替换为真实 token
nano .env   # 或用任何编辑器
```

### 3. 构建并启动（首次运行）
```bash
docker-compose up -d --build
```

### 4. 查看日志（确认 bot 正常运行）
```bash
docker-compose logs -f
```

### 5. 停止
```bash
docker-compose down
```

### 日常开发流程（编辑代码后）
```bash
# 修改了 src/ 里的代码后，只需重启容器（不需要重新 build）
docker-compose restart

# 如果修改了 requirements.txt（加了新依赖），才需要重新 build
docker-compose up -d --build
```

### 常用命令速查
```bash
docker-compose logs -f       # 实时看日志
docker-compose ps            # 查看容器状态
docker-compose down          # 停止并删除容器
docker-compose restart       # 重启容器
docker-compose up -d --build # 重新构建并启动
```

***

## 👥 团队分工

| 角色  | 成员  | 负责模块                   | 分支                             |
| --- | --- | ---------------------- | ------------------------------ |
| **队长** | 成员A | 核心系统架构 + 检测 + 跟踪 + 摄像头管理 + 系统集成 | `feature/core-system`           |
| 队友  | 成员B | 深度估计 + 点云处理 + 定位导航 + 避障算法 | `feature/spatial-intelligence` |
| 队友  | 成员C | 视频输出 + 灵巧手接口 + HDR控制 + 异常处理 | `feature/peripheral-integration` |

### 分支协作规范

```bash
# 开始新功能前，从 main 创建分支
git checkout main
git pull
git checkout -b feature/你的功能名

# 开发完成后，推送并在 GitHub 发起 Pull Request
git push origin feature/你的功能名

# 代码审查流程
# 1. 队长负责审查所有 PR
# 2. 合并到 main 分支前需队长批准
# 3. 定期同步 main 分支到各自 feature 分支
```

***

## ⚠️ 常见问题

| 问题                    | 原因                   | 解决方法                                            |
| --------------------- | -------------------- | ----------------------------------------------- |
| 容器内看不到代码              | 路径用了 `./` 相对路径       | 改用 `${PWD}\src` 绝对路径重建容器                        |
| Permission denied     | 文件权限问题               | 容器内执行 `chmod -R 777 /home/my_project`           |
| 容器不见了                 | 电脑重启后容器停止            | `docker start A1_Builder`                       |
| 容器删了代码还在吗             | 代码在 Windows `src/` 里 | 放心，重建容器代码不会丢失                                   |
| `docker images` 看不到镜像 | tar 未加载或加载失败         | 重新执行 `docker load -i a1-sdk-builder-latest.tar` |

***

## 📦 镜像文件分发说明

`a1-sdk-builder-latest.tar` 体积较大，**不上传 GitHub**，请通过以下方式获取：

- 向项目负责人索取网盘链接
- 或通过内网共享获取

获取后放置到仓库根目录即可，`.gitignore` 已配置忽略该文件。

***

## 📄 License

MIT License

***

## 🐙 GitHub CLI 指南

### 什么是 GitHub CLI？

GitHub CLI 是 GitHub 官方提供的命令行工具，允许你直接从终端与 GitHub 交互，无需打开浏览器。它可以帮助你更高效地管理仓库、创建 issues、提交 pull requests 等。

### 安装 GitHub CLI

#### Windows

1. 访问 [GitHub CLI 官方下载页面](https://cli.github.com/download/)
2. 下载并安装 Windows 版本
3. 验证安装：
   ```powershell
   gh --version
   ```

#### 在 Docker 容器中

本项目的 Docker 镜像已包含 GitHub CLI，你可以直接在容器内使用。

### 登录 GitHub

#### 在 Windows 中

```powershell
# 打开 GitHub CLI 终端
E:\it-gets-you-better-than-her\scripts\open_github_cli.bat

# 然后执行登录命令
gh auth login
```

#### 在 Docker 容器中

```bash
# 进入容器
docker exec -it A1_Builder bash

# 执行登录命令
gh auth login
```

### 常用 GitHub CLI 命令

#### 仓库操作

```bash
# 查看当前仓库信息
gh repo view

# 克隆仓库
gh repo clone owner/repo
```

#### Issues 管理

```bash
# 创建新 issue
gh issue create

# 查看 issue 列表
gh issue list

# 查看特定 issue
gh issue view ISSUE_NUMBER
```

#### Pull Requests 管理

```bash
# 创建新 PR
gh pr create

# 查看 PR 列表
gh pr list

# 检查 PR
gh pr checkout PR_NUMBER
```

### 使用自然语言描述 Bug 修复

GitHub CLI 支持使用自然语言描述来创建 issues 和 PRs，这使得描述 bug 修复更加直观和高效。

#### 示例：创建一个关于 bug 修复的 issue

```bash
gh issue create --title "修复避障算法中的边界情况" --body "当障碍物距离小于0.5米时，避障算法会出现误判，需要修复这个边界情况。"
```

#### 示例：创建一个包含 bug 修复的 PR

```bash
gh pr create --title "fix: 解决避障算法边界情况问题" --body "- 修复了障碍物距离小于0.5米时的误判问题
- 优化了距离计算逻辑
- 添加了边界情况的单元测试"
```

### 一键启动脚本

本项目提供了一个 Windows 下的一键启动脚本，用于在 src 目录下打开 GitHub CLI 终端：

```powershell
# 运行脚本
E:\it-gets-you-better-than-her\scripts\open_github_cli.bat
```

脚本会自动导航到 src 目录，检查 GitHub CLI 是否安装，并显示常用命令示例。

### 集成到开发流程

在日常开发中，你可以使用 GitHub CLI 来：

1. **快速创建 issues**：当发现 bug 或需要新功能时
2. **管理 PRs**：创建、查看和合并 pull requests
3. **同步仓库**：拉取最新代码，推送本地更改
4. **查看仓库状态**：了解当前仓库的最新动态

通过 GitHub CLI，你可以更高效地与 GitHub 交互，减少在浏览器和终端之间切换的次数，提高开发效率。
刷绿墙