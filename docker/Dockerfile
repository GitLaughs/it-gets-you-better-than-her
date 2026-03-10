# E:\it-gets-you-better-than-her\docker\Dockerfile

###############################################################################
# 基于思特威官方 A1 SDK 镜像，添加项目所需的 Python 依赖
#
# 前置条件: 先执行 docker load -i a1-sdk-builder-latest.tar
#
# 构建方法: docker compose build
###############################################################################

# 以官方导入的镜像为基础
FROM a1-sdk-builder:latest

# 设置工作目录
WORKDIR /workspace/project

# 安装项目 Python 依赖（如果容器里有 pip）
COPY requirements.txt /tmp/requirements.txt
RUN pip install -r /tmp/requirements.txt 2>/dev/null || \
    pip3 install -r /tmp/requirements.txt 2>/dev/null || \
    echo "WARNING: pip not found, skip python deps"

# 环境变量
ENV PYTHONPATH=/workspace/project/src
ENV A1_SDK_PATH=/home/smartsens_flying_chip_a1_sdk
ENV PYTHONUNBUFFERED=1