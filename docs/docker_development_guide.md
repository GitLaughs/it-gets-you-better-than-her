# Docker 开发环境使用指南

## 📋 目录

- [📋 目录](#-目录)
- [🎯 目标](#-目标)
- [🔧 前置条件](#-前置条件)
- [🚀 启动 Docker 容器](#-启动-docker-容器)
  - [方法一：使用 Docker Compose（推荐）](#方法一使用-docker-compose推荐)
  - [方法二：使用 Docker 命令](#方法二使用-docker-命令)
- [💻 在 Docker 中编辑核心代码](#-在-docker-中编辑核心代码)
  - [方式一：VS Code + Dev Containers 插件（推荐）](#方式一vs-code--dev-containers-插件推荐)
  - [方式二：在容器内使用 Vim/ Nano](#方式二在容器内使用-vim--nano)
  - [方式三：在宿主机编辑，实时同步](#方式三在宿主机编辑实时同步)
- [🔄 编译和测试](#-编译和测试)
  - [编译主程序](#编译主程序)
  - [运行测试](#运行测试)
- [📁 代码同步机制](#-代码同步机制)
- [🔧 容器管理](#-容器管理)
  - [常用命令](#常用命令)
  - [故障排查](#故障排查)
- [🎨 开发工作流](#-开发工作流)
- [📝 注意事项](#-注意事项)

## 🎯 目标

本文档旨在指导开发者如何：
1. 启动 Docker 容器并正确挂载项目代码
2. 在容器中编辑核心代码
3. 编译和测试程序
4. 管理容器生命周期

## 🔧 前置条件

- [Docker Desktop](https://www.docker.com/products/docker-desktop/) 已安装并运行
- [Git](https://git-scm.com/) 已安装
- [VS Code](https://code.visualstudio.com/)（推荐，用于远程编辑）
- VS Code 的 **Dev Containers** 插件（微软官方）
- `a1-sdk-builder-latest.tar` 镜像文件已加载
- 官方 SDK 已克隆到 `data/` 目录

## 🚀 启动 Docker 容器

### 方法一：使用 Docker Compose（推荐）

1. **进入 docker 目录**
   ```powershell
   cd it-gets-you-better-than-her\docker
   ```

2. **启动容器**
   ```powershell
   docker-compose up -d
   ```

3. **验证容器状态**
   ```powershell
   docker ps
   # 应看到 A1_Builder 容器正在运行
   ```

### 方法二：使用 Docker 命令

1. **直接运行容器**
   ```powershell
   # Windows PowerShell 中执行
   docker run -itd --name A1_Builder `
     -v "${PWD}\data:/app/smartsens_sdk" `
     -v "${PWD}\src:/app/src" `
     -v "${PWD}\models:/app/models" `
     -v "${PWD}\output:/app/output" `
     -p 8080:8080 `
     --working-dir /app/src `
     a1-sdk-builder
   ```

2. **验证挂载**
   ```powershell
   docker exec -it A1_Builder ls -la /app/src
   # 应看到 src 目录下的文件
   ```

## 💻 在 Docker 中编辑核心代码

### 方式一：VS Code + Dev Containers 插件（推荐）

1. **安装插件**
   - 在 VS Code 中搜索并安装 "Dev Containers" 插件（微软官方）

2. **连接到容器**
   - VS Code 左下角点击 **`><`** 图标
   - 选择 **Attach to Running Container**
   - 选择 **/A1_Builder**

3. **打开项目目录**
   - 新窗口打开后：**File → Open Folder →** **`/app/src`**

4. **开始编辑**
   - 现在可以在 VS Code 中直接编辑代码
   - 所有更改会实时同步到容器内
   - 底部终端已在容器环境中，可以直接执行命令

### 方式二：在容器内使用 Vim/ Nano

1. **进入容器**
   ```powershell
   docker exec -it A1_Builder bash
   ```

2. **使用 Vim 编辑**
   ```bash
   cd /app/src
   vim src/core/vision_system.cpp
   ```

3. **使用 Nano 编辑**（更简单）
   ```bash
   cd /app/src
   nano src/core/vision_system.cpp
   ```

### 方式三：在宿主机编辑，实时同步

1. **在宿主机打开项目**
   - 使用任何编辑器打开 `E:\it-gets-you-better-than-her\src` 目录

2. **编辑代码**
   - 直接编辑文件，保存后会自动同步到容器内

3. **验证同步**
   ```bash
   # 在容器内执行
   ls -la /app/src/src/core/
   # 检查文件修改时间是否更新
   ```

## 🔄 编译和测试

### 编译主程序

1. **进入容器**
   ```powershell
   docker exec -it A1_Builder bash
   ```

2. **使用 Docker 编译脚本**
   ```bash
   cd /app/src
   ./scripts/build_docker.sh
   ```

3. **或使用 Makefile**
   ```bash
   cd /app/src
   ./scripts/build.sh --type release
   ```

4. **或使用 CMake**
   ```bash
   cd /app/src
   mkdir -p build
   cd build
   cmake ..
   make -j4
   ```

### 运行测试

1. **运行配置测试**
   ```bash
   cd /app/src
   ./build/test_config
   ```

2. **运行跟踪测试**
   ```bash
   cd /app/src
   ./build/test_tracker
   ```

3. **运行避障测试**
   ```bash
   cd /app/src
   ./build/test_obstacle
   ```

## 📁 代码同步机制

- **双向实时同步**：宿主机的 `src/` 目录与容器内的 `/app/src` 目录是实时双向同步的
- **无需手动复制**：在宿主机或容器内编辑代码，都会自动同步到另一端
- **权限注意**：容器内创建的文件可能会有不同的权限，如需修改权限可执行：
  ```bash
  docker exec -it A1_Builder chmod -R 777 /app/src
  ```

## 🔧 容器管理

### 常用命令

| 命令 | 说明 |
|------|------|
| `docker start A1_Builder` | 启动容器（电脑重启后需要） |
| `docker stop A1_Builder` | 停止容器 |
| `docker restart A1_Builder` | 重启容器 |
| `docker exec -it A1_Builder bash` | 进入容器 |
| `docker ps` | 查看运行中的容器 |
| `docker ps -a` | 查看所有容器（含已停止） |
| `docker logs A1_Builder` | 查看容器日志 |
| `docker rm A1_Builder` | 删除容器（代码不会丢失） |

### 故障排查

| 问题 | 原因 | 解决方法 |
|------|------|----------|
| 容器内看不到代码 | 路径挂载错误 | 重新创建容器，使用绝对路径 `${PWD}\src` |
| 编译失败 | 依赖缺失 | 检查 Dockerfile 中的依赖安装 |
| 权限错误 | 文件权限问题 | 执行 `chmod -R 777 /app/src` |
| 容器启动失败 | 端口被占用 | 修改 docker-compose.yml 中的端口映射 |

## 🎨 开发工作流

1. **启动容器**
   ```powershell
   cd it-gets-you-better-than-her\docker
   docker-compose up -d
   ```

2. **连接 VS Code**
   - 使用 Dev Containers 插件连接到 A1_Builder 容器
   - 打开 `/app/src` 目录

3. **编辑代码**
   - 在 VS Code 中修改核心代码
   - 保存后自动同步到容器

4. **编译测试**
   - 在 VS Code 终端中执行编译命令
   - 运行测试验证功能

5. **提交代码**
   - 在宿主机执行 Git 命令提交更改
   - 推送到 GitHub

6. **停止容器**（可选）
   ```powershell
   docker-compose down
   ```

## 📝 注意事项

1. **保留原有 Demo**：原有的 Demo 编译程序位于 `/app/smartsens_sdk/scripts/` 目录，请勿修改
2. **镜像文件**：`a1-sdk-builder-latest.tar` 不需要提交到 GitHub，`.gitignore` 已配置
3. **SDK 目录**：`data/` 目录包含官方 SDK，不需要提交到 GitHub
4. **编译产物**：`build/` 和 `output/` 目录会被 `.gitignore` 忽略
5. **端口映射**：默认映射 8080 端口，如需其他端口请修改 docker-compose.yml
6. **环境变量**：容器内已设置必要的环境变量，如 `PYTHONPATH` 和 `A1_SDK_PATH`

---

**🎉 现在您已经掌握了在 Docker 中开发和编辑核心代码的完整流程！**
