# 🤖 IT-Gets-You-Better-Than-Her

> A1 Vision Pi 端侧智能视觉系统 — 基于思特威SC132GS + 飞凌微A1开发套件

[!\[GitHub Issues\](https://img.shields.io/github/issues/GitLaughs/it-gets-you-better-than-her null)](https://github.com/GitLaughs/it-gets-you-better-than-her/issues)
[!\[Docker\](https://img.shields.io/badge/Docker-Supported-blue null)](./docker/)

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
├── tests/                           ← ✅ 测试代码（git 管理）
├── docker/                          ← ✅ Docker 配置（git 管理）
│   ├── Dockerfile
│   └── docker-compose.yml
├── .gitignore
├── README.md
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
  -v "${PWD}\data:/home/smartsens_flying_chip_a1_sdk" `
  -v "${PWD}\src:/home/my_project" `
  a1-sdk-builder

# 验证挂载是否成功
docker exec -it A1_Builder /bin/bash
ls /home/my_project                      # 应看到 src/ 下的代码
ls /home/smartsens_flying_chip_a1_sdk    # 应看到 a1_sdk_sc132gs/
exit

#或者
# 使用 Docker Compose 启动（推荐多容器场景）
cd docker
docker-compose up -d
```

> **路径说明：**
> Windows 的 `E:\...\src` → 容器内的 `/home/my_project`（实时双向同步，不是拷贝）

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
│  /home/my_project/        │  ← 挂载自 src/（你的代码）
│  /home/.../a1_sdk_sc132gs/│  ← 挂载自 data/（官方SDK）
└───────────────────────────┘
          ↕ 实时同步（非拷贝）
┌───────────────────────────┐
│ Windows 宿主机             │
│  src/        （写代码）    │
│  data/       （SDK）      │
└───────────────────────────┘
```

***

## 💻 日常开发流程（VS Code + Dev Containers）

### 连接容器（推荐方式）

1. 确认容器正在运行：`docker ps`（看到 `A1_Builder`）
2. VS Code 左下角点击 **`><`** 图标
3. 选择 **Attach to Running Container**
4. 选择 **/A1\_Builder**
5. 新窗口打开后：**File → Open Folder →** **`/home/my_project`**

现在 VS Code 的编辑器和底部终端**都在容器环境内**，可以直接编译运行。

### 每天的工作循环

```bash
# ① 开始工作前，拉取队友最新代码（在宿主机终端或 VS Code 终端）
git pull

# ② 用 VS Code（连接容器后）写代码，保存即实时同步到容器

# ③ 在容器内终端编译运行
cd /home/my_project
make                        # 或 python src/main.py，取决于项目

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

## 👥 团队分工

| 成员  | 负责模块                   | 分支                             |
| --- | ---------------------- | ------------------------------ |
| 成员A | 检测 + 跟踪 + 摄像头管理        | `feature/perception-pipeline`  |
| 成员B | 深度 + 点云 + 定位 + 导航      | `feature/spatial-intelligence` |
| 成员C | 显示 + 灵巧手 + 系统集成 + 异常处理 | `feature/system-integration`   |

### 分支协作规范

```bash
# 开始新功能前，从 main 创建分支
git checkout main
git pull
git checkout -b feature/你的功能名

# 开发完成后，推送并在 GitHub 发起 Pull Request
git push origin feature/你的功能名
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
