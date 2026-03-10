# 🤖 IT-Gets-You-Better-Than-Her

> A1 Vision Pi 端侧智能视觉系统 — 基于思特威SC132GS + 飞凌微A1开发套件

[![GitHub Issues](https://img.shields.io/github/issues/GitLaughs/it-gets-you-better-than-her)](https://github.com/GitLaughs/it-gets-you-better-than-her/issues)
[![Docker](https://img.shields.io/badge/Docker-Supported-blue)](./docker/)

## 📋 功能列表

| # | 功能模块 | 说明 | 优先级 |
|---|---------|------|--------|
| 1 | YOLOv8 目标检测 | 基于NPU加速的实时目标检测 | ⭐⭐⭐ |
| 2 | 目标跟踪 | ByteTrack 多目标跟踪 | ⭐⭐⭐ |
| 3 | 单目深度估计 | 轻量级单目深度感知 | ⭐⭐⭐ |
| 4 | 三维点云可视化 | 深度图转点云 + 外设屏幕输出 | ⭐⭐ |
| 5 | 自身定位与目标坐标 | 视觉里程计 + 目标3D坐标估计 | ⭐⭐⭐ |
| 6 | 避障算法 | 基于深度图的实时避障 | ⭐⭐⭐ |
| 7 | 灵巧手接口 | UART/I2C 外设控制接口 | ⭐⭐ |
| 8 | HDR模式切换 | 光线自适应 HDR 控制 | ⭐⭐ |
| 9 | 异常处理系统 | 摄像头/推理/资源 三级异常保护 | ⭐⭐ |



## 🚀 快速开始

### 环境准备

```bash
# 克隆仓库
git clone https://github.com/GitLaughs/it-gets-you-better-than-her.git
cd it-gets-you-better-than-her

# 克隆 SDK (作为子目录)
git clone --depth 1 https://git.smartsenstech.ai/Smartsens/A1_SDK_SC132GS.git smartsens_sdk
```

### Docker 一键启动
```bash
cd docker
docker-compose up --build
```

### 本地开发
```bash
pip install -r requirements.txt
python src/main.py --config src/config/config.yaml
```

## 🚀 快速开始（团队成员）

### 前置条件
- [Docker Desktop](https://www.docker.com/products/docker-desktop/) 已安装
- Git 已安装

### 团队成员首次使用:

1. **克隆项目**
   ```bash
   git clone https://github.com/GitLaughs/it-gets-you-better-than-her.git
   cd it-gets-you-better-than-her
   ```

2. **导入官方基础镜像**
   ```bash
   docker load -i a1-sdk-builder-latest.tar
   ```

3. **构建并启动容器**
   ```bash
   cd docker && docker compose up -d --build
   ```

### 实际构建流程:

```
┌───────────────────────────┐
│ a1-sdk-builder:latest     │  ← 官方SDK（tar导入）
│  - 编译工具链              │
│  - buildroot              │
│  - 交叉编译器              │
└─────────┬─────────────────┘
         │  FROM （Dockerfile 第1行）
         ▼
┌───────────────────────────┐
│ a1-builder-dev:latest     │  ← 你的扩展层（Dockerfile构建）
│  + Python 依赖            │
│  + 环境变量               │
│  + 工作目录配置            │
└─────────┬─────────────────┘
         │  docker compose up -d
         ▼
┌───────────────────────────┐
│ 容器: A1_Builder          │  ← 运行中
│  + 挂载 src/ tests/ ...   │
└───────────────────────────┘
```

### 进入开发环境:

```bash
cd docker && docker compose exec dev bash
```

### 日常开发

- **改代码**: 在宿主机用 VS Code 编辑 src/ 目录
- **跑程序**: 在容器内 `python src/main.py`
- **跑测试**: 在容器内 `python -m pytest tests/ -v`
- **提交代码**: 在宿主机 `git add` / `commit` / `push`

## 👥 团队分工

| 成员 | 负责模块 | 分支 |
|------|---------|------|
| 成员A | 检测 + 跟踪 + 摄像头管理 | `feature/perception-pipeline` |
| 成员B | 深度 + 点云 + 定位 + 导航 | `feature/spatial-intelligence` |
| 成员C | 显示 + 灵巧手 + 系统集成 + 异常处理 | `feature/system-integration` |

## 📖 文档

- Windows Docker 开发指南

## 📄 License

MIT License