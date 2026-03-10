#!/bin/bash
###############################################################################
# 新成员一键初始化脚本
# 用法: bash scripts/setup_dev.sh
###############################################################################
set -e

echo "========================================="
echo "  A1_Builder 开发环境初始化"
echo "========================================="

PROJECT_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$PROJECT_ROOT"

# 1. 检查 Docker
echo "[1/5] 检查 Docker..."
if ! command -v docker &> /dev/null; then
    echo "❌ 请先安装 Docker Desktop: https://www.docker.com/products/docker-desktop/"
    exit 1
fi
echo "✅ Docker 已安装: $(docker --version)"

# 2. 检查镜像
echo "[2/5] 检查 Docker 镜像..."
if ! docker image inspect a1-sdk-builder:latest &> /dev/null; then
    if [ -f "a1-sdk-builder-latest.tar" ]; then
        echo "📦 正在导入镜像（可能需要几分钟）..."
        docker load -i a1-sdk-builder-latest.tar
    else
        echo "❌ 缺少镜像文件！"
        echo "   请从团队网盘下载 a1-sdk-builder-latest.tar 到项目根目录"
        echo "   网盘地址: <你的网盘链接>"
        exit 1
    fi
fi
echo "✅ 镜像就绪"

# 3. 创建必要目录
echo "[3/5] 创建目录..."
mkdir -p data models output
echo "✅ 目录已创建"

# 4. 启动容器
echo "[4/5] 启动 Docker 容器..."
cd docker
docker compose down 2>/dev/null || true
docker compose up -d
cd ..
echo "✅ 容器已启动"

# 5. 安装 Python 依赖
echo "[5/5] 安装 Python 依赖..."
docker compose -f docker/docker-compose.yml exec dev \
    pip install -r requirements.txt 2>/dev/null || \
    echo "⚠️  pip install 可能需要手动进入容器执行"

echo ""
echo "========================================="
echo "  ✅ 初始化完成！"
echo "========================================="
echo ""
echo "  进入开发环境:  cd docker && docker compose exec dev bash"
echo "  运行主程序:    python src/main.py"
echo "  运行测试:      python -m pytest tests/ -v"
echo ""