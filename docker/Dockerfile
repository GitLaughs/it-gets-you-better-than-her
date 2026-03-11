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

WORKDIR /app

COPY requirements.txt .

RUN pip install --no-cache-dir -r requirements.txt 2>/dev/null || \
    pip3 install --no-cache-dir -r requirements.txt 2>/dev/null || \
    echo "WARNING: pip not found, skip python deps"

COPY src/ ./src/

WORKDIR /app/src

# 环境变量
ENV PYTHONPATH=/app/src
ENV A1_SDK_PATH=/app/smartsens_sdk
ENV PYTHONUNBUFFERED=1

CMD ["python", "main.py"]