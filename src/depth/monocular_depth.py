"""
单目深度估计器

支持:
  - 轻量级深度估计（为 NPU 定制的小型模型）
  - 基于 ONNX 的推理
  - 时间和空间滤波以获得稳定的深度图
"""


import time
import numpy as np
from typing import Optional, Tuple
from collections import deque

from src.utils.logger import logger

try:
    import cv2
except ImportError:
    cv2 = None

try:
    import onnxruntime as ort
    _ORT_AVAILABLE = True
except ImportError:
    _ORT_AVAILABLE = False


class MonocularDepthEstimator:
    """
    从单个单目相机图像估计每个像素的深度

    在 A1 平台上，使用为 NPU 量化的轻量级深度模型
    如果没有可用模型，则回退到简单的视差启发式方法
    """

    def __init__(self, config: dict):
        """初始化单目深度估计器"""
        self._cfg = config
        self._model_path = config.get("model_path", "models/depth_anything_small.onnx")  # 模型路径
        self._input_size = tuple(config.get("input_size", [256, 256]))  # 输入尺寸
        self._device = config.get("device", "cpu")  # 运行设备
        self._max_depth = config.get("max_depth_m", 10.0)  # 最大深度（米）
        self._min_depth = config.get("min_depth_m", 0.1)  # 最小深度（米）

        # 滤波器设置
        self._temporal_filter = config.get("temporal_filter", True)  # 时间滤波
        self._temporal_alpha = config.get("temporal_alpha", 0.4)  # 时间滤波权重
        self._spatial_filter = config.get("spatial_filter", True)  # 空间滤波
        self._spatial_kernel = config.get("spatial_kernel", 5)  # 空间滤波核大小

        self._session = None  # 推理会话
        self._input_name = None  # 输入名称
        self._prev_depth: Optional[np.ndarray] = None  # 上一帧深度图
        self._initialized = False  # 初始化状态

        logger.info(
            f"DepthEstimator: model={self._model_path} "
            f"input={self._input_size} device={self._device}"
        )


    def initialize(self) -> bool:
        """加载深度模型"""
        try:
            if _ORT_AVAILABLE:
                return self._init_onnx()
            else:
                logger.warning("ONNX 不可用，使用启发式深度估计")
                self._initialized = True
                return True
        except Exception as e:
            logger.warning(f"深度模型加载失败: {e}, 使用启发式方法")
            self._initialized = True
            return True


    def _init_onnx(self) -> bool:
        """初始化 ONNX Runtime 深度模型"""
        try:
            providers = ["CPUExecutionProvider"]
            self._session = ort.InferenceSession(self._model_path, providers=providers)
            self._input_name = self._session.get_inputs()[0].name
            self._initialized = True
            logger.info("深度模型已通过 ONNX 加载")
            return True
        except Exception as e:
            logger.warning(f"加载深度模型失败: {e}")
            self._initialized = True
            return True


    def estimate(self, frame: np.ndarray) -> Optional[np.ndarray]:
        """
        从 RGB/灰度帧估计深度图

        参数:
            frame: 输入图像 (HxWx3 或 HxW)

        返回:
            深度图 (HxW)，单位为米，失败返回 None
            值范围从 min_depth 到 max_depth
        """

        if not self._initialized:
            logger.warning("Depth estimator not initialized")
            return None

        t0 = time.time()

        if self._session is not None:
            depth = self._infer_model(frame)
        else:
            depth = self._heuristic_depth(frame)

        if depth is None:
            return None

        # Apply filters
        if self._spatial_filter and cv2 is not None:
            depth = cv2.GaussianBlur(
                depth, (self._spatial_kernel, self._spatial_kernel), 0
            )

        if self._temporal_filter and self._prev_depth is not None:
            if self._prev_depth.shape == depth.shape:
                alpha = self._temporal_alpha
                depth = alpha * depth + (1 - alpha) * self._prev_depth

        self._prev_depth = depth.copy()

        elapsed = (time.time() - t0) * 1000
        logger.debug(f"Depth estimation: {elapsed:.1f}ms")

        return depth

    def _infer_model(self, frame: np.ndarray) -> Optional[np.ndarray]:
        """运行深度模型推理"""
        try:
            orig_h, orig_w = frame.shape[:2]

            # Preprocess
            if len(frame.shape) == 2:
                img = cv2.cvtColor(frame, cv2.COLOR_GRAY2RGB)
            else:
                img = cv2.cvtColor(frame, cv2.COLOR_BGR2RGB)

            inp_w, inp_h = self._input_size
            img_resized = cv2.resize(img, (inp_w, inp_h), interpolation=cv2.INTER_LINEAR)
            img_norm = img_resized.astype(np.float32) / 255.0

            # Normalize with ImageNet stats
            mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
            std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
            img_norm = (img_norm - mean) / std

            # HWC → NCHW
            blob = img_norm.transpose(2, 0, 1)[np.newaxis, ...]

            # Inference
            outputs = self._session.run(None, {self._input_name: blob})
            raw_depth = outputs[0]

            # Squeeze to 2D
            if raw_depth.ndim == 4:
                raw_depth = raw_depth[0, 0]
            elif raw_depth.ndim == 3:
                raw_depth = raw_depth[0]

            # Resize to original resolution
            depth = cv2.resize(
                raw_depth, (orig_w, orig_h), interpolation=cv2.INTER_LINEAR
            )

            # Normalize to meter range
            d_min, d_max = depth.min(), depth.max()
            if d_max - d_min > 1e-6:
                depth = (depth - d_min) / (d_max - d_min)
            depth = self._min_depth + depth * (self._max_depth - self._min_depth)

            return depth.astype(np.float32)

        except Exception as e:
            logger.error(f"Depth inference failed: {e}")
            return self._heuristic_depth(frame)

    def _heuristic_depth(self, frame: np.ndarray) -> np.ndarray:
        """
        使用图像梯度的简单启发式深度估计
        当没有可用模型时用作回退方法
        """

        if len(frame.shape) == 3:
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY) if cv2 else np.mean(frame, axis=2)
        else:
            gray = frame.astype(np.float32)

        h, w = gray.shape[:2]

        # Gradient-based depth heuristic:
        # Areas with more detail (high gradient) tend to be closer
        if cv2:
            grad_x = cv2.Sobel(gray, cv2.CV_32F, 1, 0, ksize=3)
            grad_y = cv2.Sobel(gray, cv2.CV_32F, 0, 1, ksize=3)
            gradient = np.sqrt(grad_x**2 + grad_y**2)
        else:
            gradient = np.abs(np.diff(gray.astype(float), axis=1, prepend=0)) + \
                       np.abs(np.diff(gray.astype(float), axis=0, prepend=0))

        # Invert: high gradient → near (small depth)
        g_max = gradient.max() + 1e-6
        depth = 1.0 - (gradient / g_max)

        # Scale to depth range
        depth = self._min_depth + depth * (self._max_depth - self._min_depth)

        # Add vertical gradient bias (bottom = near, top = far)
        v_grad = np.linspace(0.3, 1.0, h).reshape(-1, 1)
        v_grad = np.tile(v_grad, (1, w))
        depth = depth * v_grad

        return depth.astype(np.float32)

    # ── Visualization ─────────────────────────────────────

    def colorize_depth(self, depth: np.ndarray) -> np.ndarray:
        """将深度图转换为彩色可视化"""

        if cv2 is None:
            return depth.astype(np.uint8)

        # Normalize to 0-255
        d_min, d_max = depth.min(), depth.max()
        if d_max - d_min > 1e-6:
            normalized = ((depth - d_min) / (d_max - d_min) * 255).astype(np.uint8)
        else:
            normalized = np.zeros_like(depth, dtype=np.uint8)

        # Apply colormap (TURBO for better depth visualization)
        colored = cv2.applyColorMap(normalized, cv2.COLORMAP_TURBO)
        return colored

    def release(self):
        """释放深度估计器资源"""
        self._session = None
        self._prev_depth = None
        logger.info("深度估计器已释放")
