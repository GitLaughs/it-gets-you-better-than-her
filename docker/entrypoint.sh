#!/usr/bin/env bash
###############################################################################
# A1_Builder 容器入口脚本
###############################################################################
set -e

echo "============================================="
echo "  A1_Builder 开发环境"
echo "  项目: it-gets-you-better-than-her"
echo "  芯片: Flyingchip A1 + SC132GS"
echo "============================================="

# ── 检查 SDK ──────────────────────────────────────────────────────────────
if [ -d "/workspace/A1_Builder/smartsens_sdk/smart_software" ]; then
    echo "[✓] A1 SDK (SC132GS) 已挂载"
    export A1_SDK_PATH="/workspace/A1_Builder/smartsens_sdk"

    # 设置交叉编译工具链路径（如果 SDK 自带）
    SDK_TOOLCHAIN="${A1_SDK_PATH}/smart_software/toolchain/glibc-ssp-cpp/arm-smartsens-linux-gnueabihf_sdk-buildroot"
    if [ -d "${SDK_TOOLCHAIN}" ]; then
        export PATH="${SDK_TOOLCHAIN}/bin:${PATH}"
        echo "[✓] SDK 交叉编译工具链已加入 PATH"
    fi
else
    echo "[!] 警告: A1 SDK 未挂载，请检查 docker-compose.yml 中的 volumes 配置"
    echo "    预期路径: /workspace/A1_Builder/smartsens_sdk/smart_software"
fi

# ── 检查 Python 环境 ─────────────────────────────────────────────────────
echo ""
echo "Python: $(python3 --version 2>&1)"
echo "pip:    $(pip3 --version 2>&1 | head -1)"

# ── 检查关键 Python 包 ──────────────────────────────────────────────────
python3 -c "import cv2; print(f'[✓] OpenCV {cv2.__version__}')" 2>/dev/null || echo "[!] OpenCV 未安装"
python3 -c "import torch; print(f'[✓] PyTorch {torch.__version__}')" 2>/dev/null || echo "[!] PyTorch 未安装 (可选)"
python3 -c "from ultralytics import YOLO; print('[✓] Ultralytics (YOLOv8)')" 2>/dev/null || echo "[!] Ultralytics 未安装"
python3 -c "import onnxruntime; print(f'[✓] ONNXRuntime {onnxruntime.__version__}')" 2>/dev/null || echo "[!] ONNXRuntime 未安装"

# ── 创建必要目录 ─────────────────────────────────────────────────────────
mkdir -p /workspace/A1_Builder/{models,data,output,logs}

echo ""
echo "============================================="
echo "  环境就绪 — 执行: python3 src/main.py"
echo "============================================="

# 执行传入的命令
exec "$@"