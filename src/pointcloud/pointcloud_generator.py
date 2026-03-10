"""
3D 点云生成器与可视化器

将深度图 + 相机内参 → 3D 点云
为外部显示提供实时渲染可视化
"""


import time
import numpy as np
from typing import Optional, Tuple
from dataclasses import dataclass

from src.utils.logger import logger

try:
    import cv2
except ImportError:
    cv2 = None

try:
    import open3d as o3d
    _O3D_AVAILABLE = True
except ImportError:
    _O3D_AVAILABLE = False


@dataclass
class CameraIntrinsics:
    """相机内参参数"""
    fx: float  # 焦距 x
    fy: float  # 焦距 y
    cx: float  # 主点 x
    cy: float  # 主点 y
    width: int = 1280  # 图像宽度
    height: int = 720  # 图像高度



class PointCloudGenerator:
    """
    从深度图生成和可视化 3D 点云

    处理流程:
    1. 深度图 → 使用相机内参（针孔模型）生成 3D 点
    2. 可选：体素下采样以提高性能
    3. 可选：从 RGB 帧进行颜色化
    4. 渲染为 2D 图像用于外部显示输出
    """

    def __init__(self, config: dict):
        """初始化点云生成器"""
        self._cfg = config
        self._enabled = config.get("enabled", True)  # 是否启用
        self._voxel_size = config.get("voxel_size", 0.05)  # 体素大小
        self._max_points = config.get("max_points", 50000)  # 最大点数
        self._colorize = config.get("colorize", True)  # 是否颜色化
        self._update_interval_ms = config.get("update_interval_ms", 100)  # 更新间隔（毫秒）

        # 相机内参
        intrinsic_cfg = config.get("camera_intrinsics", {})
        self._intrinsics = CameraIntrinsics(
            fx=intrinsic_cfg.get("fx", 600.0),
            fy=intrinsic_cfg.get("fy", 600.0),
            cx=intrinsic_cfg.get("cx", 640.0),
            cy=intrinsic_cfg.get("cy", 360.0),
        )

        self._last_update_time = 0.0  # 上次更新时间
        self._last_cloud: Optional[np.ndarray] = None  # 上次生成的点云
        self._last_colors: Optional[np.ndarray] = None  # 上次的颜色

        # Open3D 可视化器（离屏渲染）
        self._visualizer = None  # 可视化器
        self._pcd = None  # 点云对象

        logger.info(
            f"PointCloudGenerator: voxel={self._voxel_size} "
            f"max_points={self._max_points}"
        )


    def initialize(self) -> bool:
        """初始化点云生成器"""
        if not self._enabled:
            logger.info("PointCloud 已禁用")
            return True

        if _O3D_AVAILABLE:
            try:
                self._pcd = o3d.geometry.PointCloud()
                logger.info("Open3D 点云已初始化")
            except Exception as e:
                logger.warning(f"Open3D 初始化失败: {e}")

        return True


    def generate(
        self,
        depth_map: np.ndarray,
        color_frame: Optional[np.ndarray] = None,
    ) -> Optional[np.ndarray]:
        """
        从深度图生成 3D 点云

        参数:
            depth_map: 深度图，单位为米 (HxW float)
            color_frame: 可选的 BGR 帧，用于颜色化

        返回:
            点数组 (Nx3) 或 None
        """

        if not self._enabled:
            return None

        now = time.time() * 1000
        if now - self._last_update_time < self._update_interval_ms:
            return self._last_cloud

        t0 = time.time()

        h, w = depth_map.shape[:2]
        fx, fy = self._intrinsics.fx, self._intrinsics.fy
        cx, cy = self._intrinsics.cx, self._intrinsics.cy

        # Create pixel coordinate grid
        u = np.arange(w)
        v = np.arange(h)
        u, v = np.meshgrid(u, v)

        # Backproject to 3D
        z = depth_map
        x = (u - cx) * z / fx
        y = (v - cy) * z / fy

        # Stack to Nx3
        points = np.stack([x, y, z], axis=-1).reshape(-1, 3)

        # Filter invalid points
        valid_mask = (z.reshape(-1) > 0.01) & (z.reshape(-1) < 100.0)
        points = points[valid_mask]

        # Colors
        colors = None
        if self._colorize and color_frame is not None:
            if len(color_frame.shape) == 3:
                c = color_frame.reshape(-1, 3)[valid_mask].astype(np.float64) / 255.0
                colors = c[:, ::-1]  # BGR → RGB
            self._last_colors = colors

        # Subsample if too many points
        if len(points) > self._max_points:
            indices = np.random.choice(len(points), self._max_points, replace=False)
            points = points[indices]
            if colors is not None:
                colors = colors[indices]

        self._last_cloud = points
        self._last_update_time = now

        elapsed = (time.time() - t0) * 1000
        logger.debug(f"PointCloud: {len(points)} points in {elapsed:.1f}ms")

        return points

    def render_to_image(
        self,
        points: np.ndarray,
        colors: Optional[np.ndarray] = None,
        image_size: Tuple[int, int] = (640, 480),
        view_angle: float = 0.0,
    ) -> np.ndarray:
        """
        将点云渲染为 2D 图像用于显示

        带有旋转的简单正交投影

        参数:
            points: Nx3 数组
            colors: 可选的 Nx3 RGB (0-1 范围)
            image_size: 输出图像 (宽度, 高度)
            view_angle: 旋转角度（度）

        返回:
            BGR 图像
        """

        w, h = image_size
        img = np.zeros((h, w, 3), dtype=np.uint8)

        if points is None or len(points) == 0:
            return img

        # Apply rotation around Y axis
        angle_rad = np.radians(view_angle)
        cos_a, sin_a = np.cos(angle_rad), np.sin(angle_rad)
        rot_points = points.copy()
        rot_points[:, 0] = points[:, 0] * cos_a + points[:, 2] * sin_a
        rot_points[:, 2] = -points[:, 0] * sin_a + points[:, 2] * cos_a

        # Project to 2D (simple perspective)
        x = rot_points[:, 0]
        y = rot_points[:, 1]
        z = rot_points[:, 2]

        # Normalize to image coordinates
        x_min, x_max = x.min(), x.max()
        y_min, y_max = y.min(), y.max()

        x_range = max(x_max - x_min, 0.01)
        y_range = max(y_max - y_min, 0.01)

        margin = 20
        px = ((x - x_min) / x_range * (w - 2 * margin) + margin).astype(int)
        py = ((y - y_min) / y_range * (h - 2 * margin) + margin).astype(int)

        # Clip
        valid = (px >= 0) & (px < w) & (py >= 0) & (py < h)
        px = px[valid]
        py = py[valid]

        if colors is not None:
            point_colors = (colors[valid] * 255).astype(np.uint8)
        else:
            # Color by depth
            z_valid = z[valid]
            z_norm = (z_valid - z_valid.min()) / (z_valid.max() - z_valid.min() + 1e-6)
            point_colors = np.zeros((len(px), 3), dtype=np.uint8)
            point_colors[:, 0] = (255 * (1 - z_norm)).astype(np.uint8)  # B
            point_colors[:, 1] = (128 * z_norm).astype(np.uint8)         # G
            point_colors[:, 2] = (255 * z_norm).astype(np.uint8)         # R

        # Draw points
        for i in range(len(px)):
            img[py[i], px[i]] = point_colors[i]

        # Draw border and label
        if cv2:
            cv2.rectangle(img, (0, 0), (w - 1, h - 1), (100, 100, 100), 1)
            cv2.putText(
                img, f"Points: {len(px)}", (10, 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (200, 200, 200), 1,
            )

        return img

    def update_intrinsics(self, fx: float, fy: float, cx: float, cy: float):
        """更新相机内参（例如，校准后）"""
        self._intrinsics = CameraIntrinsics(fx=fx, fy=fy, cx=cx, cy=cy)
        logger.info(f"内参已更新: fx={fx} fy={fy} cx={cx} cy={cy}")

    def release(self):
        """释放资源"""
        self._last_cloud = None
        self._last_colors = None
        if self._visualizer:
            self._visualizer = None
        logger.info("PointCloud 生成器已释放")
