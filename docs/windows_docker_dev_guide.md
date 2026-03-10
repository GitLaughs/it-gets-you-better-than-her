# Windows Docker 开发环境搭建指南

> 项目: **A1_Builder** — A1 Vision Pi + SC132GS 智能视觉系统  
> 仓库: `GitLaughs/it-gets-you-better-than-her`

---

## 目录

1. [前置条件](#1-前置条件)
2. [安装 Docker Desktop](#2-安装-docker-desktop)
3. [克隆项目与 SDK](#3-克隆项目与-sdk)
4. [一键初始化](#4-一键初始化)
5. [手动启动 Docker](#5-手动启动-docker)
6. [日常开发工作流](#6-日常开发工作流)
7. [Git 分支协作](#7-git-分支协作)
8. [常见问题排查](#8-常见问题排查)

---

## 1. 前置条件

| 工具 | 版本要求 | 下载地址 |
|------|---------|---------|
| Windows 10/11 | 21H2+ | — |
| WSL2 | 最新版 | `wsl --install` |
| Docker Desktop | 4.x+ | [下载](https://docs.docker.com/desktop/install/windows-install/) |
| Git | 2.40+ | [下载](https://git-scm.com/download/win) |
| VS Code | 最新版 | [下载](https://code.visualstudio.com/) |

### 推荐 VS Code 扩展
- Remote - Containers
- Docker
- Python
- GitLens

---

## 2. 安装 Docker Desktop

```powershell
# 1. 启用 WSL2
wsl --install

# 2. 安装 Docker Desktop 后，确认设置中启用:
#    Settings → General → Use the WSL 2 based engine ✓
#    Settings → Resources → WSL Integration → 启用你的 Linux 发行版

# 3. 验证
docker --version
docker compose version
```

## 3. 克隆项目与 SDK

# 推荐在 WSL2 内操作以获得更好的文件系统性能
```bash
wsl

# 克隆主项目
git clone https://github.com/GitLaughs/it-gets-you-better-than-her.git
cd it-gets-you-better-than-her

# 克隆 A1 SDK (赛题二)
git clone --depth 1 https://git.smartsenstech.ai/Smartsens/A1_SDK_SC132GS.git
```

## 4. 一键初始化

# 赋予执行权限
```bash
chmod +x scripts/setup_dev.sh

# 运行初始化脚本
bash scripts/setup_dev.sh
```

该脚本将自动完成：

✅ 检查 Docker 环境
✅ 检查/克隆 SDK
✅ 创建必要目录 (models/, data/, output/, logs/)
✅ 构建 Docker 镜像 a1_builder:latest
✅ 启动开发容器 A1_Builder

## 5. 手动启动 Docker

如果不使用一键脚本，可手动操作：

# 构建镜像
```bash
docker compose -f docker/docker-compose.yml build

# 启动容器（后台）
docker compose -f docker/docker-compose.yml up -d

# 进入容器
docker compose -f docker/docker-compose.yml exec dev bash

# 停止容器
docker compose -f docker/docker-compose.yml down
```

### 容器内验证

# 进入容器后
```bash
python3 --version
python3 -c "import cv2; print(cv2.__version__)"
python3 -c "from ultralytics import YOLO; print('YOLOv8 OK')"

# 检查 SDK
ls /workspace/A1_Builder/smartsens_sdk/smart_software/

# 运行主程序
python3 src/main.py

# 运行测试
python3 -m pytest tests/ -v
```

## 6. 日常开发工作流

### 工作流程图

```
[VS Code] ──编辑代码──→ [宿主机文件系统]
                              │
                         (volume 挂载)
                              │
                              ▼
                     [Docker: A1_Builder]
                              │
                    python3 src/main.py
                    python3 -m pytest tests/
```

### 步骤

1. 在 VS Code 中编辑代码（宿主机目录 src/）

2. 在容器中运行/测试：
   ```bash
   docker compose -f docker/docker-compose.yml exec dev bash
   python3 src/main.py
   ```

3. 提交代码（在宿主机）：
   ```bash
   git add .
   git commit -m "feat(detection): 完成 YOLOv8 推理"
   git push origin feature/detection
   ```

## 7. Git 分支协作

### 分支结构

```
main
├── feature/detection       ← 成员A
├── feature/tracking        ← 成员A
├── feature/depth           ← 成员A
├── feature/pointcloud      ← 成员A
├── feature/localization    ← 成员B
├── feature/navigation      ← 成员B
├── feature/camera          ← 成员B
├── feature/hand-interface  ← 成员B
├── feature/integration     ← 成员C
├── feature/display         ← 成员C
├── feature/exception-handling ← 成员C
├── feature/devops          ← 成员C
└── feature/logging         ← 成员C
```

### 创建分支

```bash
bash scripts/setup_branches.sh
```

### 合并流程

```bash
# 1. 在功能分支上完成开发
git checkout feature/detection
# ... 开发 ...
git push origin feature/detection

# 2. 在 GitHub 上创建 Pull Request → main

# 3. Code Review 后合并
```

## 8. 常见问题排查

### Q: Docker 构建失败 — 网络超时

```bash
# 使用镜像源
docker compose -f docker/docker-compose.yml build --build-arg HTTP_PROXY=http://xxx
```

### Q: 容器内找不到 SDK

确认 docker-compose.yml 中 volume 路径正确：

```yaml
volumes:
  - ../A1_SDK_SC132GS:/workspace/A1_Builder/smartsens_sdk
```

### Q: WSL2 内 USB 设备无法访问

```bash
# 安装 usbipd-win
winget install usbipd

# 列出设备
usbipd list

# 绑定到 WSL
usbipd bind --busid <busid>
usbipd attach --wsl --busid <busid>
```

### Q: 性能问题 / 文件系统慢

- 代码放在 WSL2 文件系统内（/home/user/），而非 /mnt/c/
- Docker Desktop → Settings → Resources → 分配足够 CPU/内存

### Q: Git 权限问题

```bash
git config --global --add safe.directory /workspace/A1_Builder
```

---

## 9. 项目文件说明

### `models/.gitkeep`

_(空文件)_

### `data/.gitkeep`

_(空文件)_

### `output/.gitkeep`

_(空文件)_

### `smartsens_sdk/.gitkeep`

_(空文件)_

### `requirements.txt`

```txt
# =============================================================================
# A1_Builder Python 依赖
# =============================================================================

# ── 深度学习推理 ─────────────────────────────────────────────────────────
ultralytics>=8.0.0          # YOLOv8
onnxruntime>=1.15.0         # ONNX Runtime (CPU); A1 上可能用 onnxruntime-arm
# torch>=2.0.0              # PyTorch (可选，PC 开发用)
# torchvision>=0.15.0       # (可选)

# ── 计算机视觉 ───────────────────────────────────────────────────────────
opencv-python-headless>=4.8.0  # 无 GUI 依赖的 OpenCV
opencv-contrib-python-headless>=4.8.0

# ── 目标跟踪 ─────────────────────────────────────────────────────────────
# deep-sort-realtime>=1.3.2  # DeepSORT (可选)
# bytetracker>=0.3           # ByteTrack (可选)
filterpy>=1.4.5              # 卡尔曼滤波
scipy>=1.10.0
lap>=0.4.0                   # 线性分配

# ── 深度估计 ─────────────────────────────────────────────────────────────
# timm>=0.9.0               # MiDaS 依赖 (可选)

# ── 三维点云 ─────────────────────────────────────────────────────────────
open3d>=0.17.0               # 点云处理/可视化
numpy>=1.24.0

# ── 配置 & 工具 ──────────────────────────────────────────────────────────
pyyaml>=6.0                  # YAML 配置解析
Pillow>=10.0.0               # 图像处理

# ── 串口通信 (灵巧手) ───────────────────────────────────────────────────
pyserial>=3.5

# ── 日志 & 监控 ──────────────────────────────────────────────────────────
colorlog>=6.7.0              # 彩色日志
psutil>=5.9.0                # 系统资源监控

# ── 测试 ─────────────────────────────────────────────────────────────────
pytest>=7.4.0
pytest-timeout>=2.1.0

# ── API (可选) ───────────────────────────────────────────────────────────
# requests>=2.31.0