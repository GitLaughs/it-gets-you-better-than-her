"""
位置估计器 — 自身定位与目标 3D 坐标估计

方法:
  - 视觉里程计（ORB 特征）用于自身位姿
  - 基于深度的 3D 位置用于跟踪目标
"""


import time
import numpy as np
from typing import Optional, Tuple, List, Dict
from dataclasses import dataclass, field

from src.utils.logger import logger
from src.tracking.tracker import Track

try:
    import cv2
except ImportError:
    cv2 = None


@dataclass
class Pose3D:
    """3D 位姿（位置 + 旋转）"""
    x: float = 0.0  # x 位置
    y: float = 0.0  # y 位置
    z: float = 0.0  # z 位置
    roll: float = 0.0  # 横滚角
    pitch: float = 0.0  # 俯仰角
    yaw: float = 0.0  # 偏航角

    def to_array(self) -> np.ndarray:
        """转换为数组"""
        return np.array([self.x, self.y, self.z, self.roll, self.pitch, self.yaw])

    def position(self) -> np.ndarray:
        """获取位置向量"""
        return np.array([self.x, self.y, self.z])



@dataclass
class TargetPosition:
    """被跟踪目标的 3D 位置"""
    track_id: int  # 跟踪 ID
    x: float  # x 位置（米）
    y: float  # y 位置（米）
    z: float  # z 位置（深度，米）
    class_name: str = ""  # 类别名称
    confidence: float = 0.0  # 置信度
    pixel_x: int = 0  # 像素 x 坐标
    pixel_y: int = 0  # 像素 y 坐标

    def distance(self) -> float:
        """计算到目标的距离"""
        return float(np.sqrt(self.x**2 + self.y**2 + self.z**2))



class PositionEstimator:
    """
    估计:
    1. 通过视觉里程计估计自身位置
    2. 通过结合跟踪和深度估计目标 3D 坐标
    """

    def __init__(self, config: dict, camera_intrinsics: dict):
        """初始化位置估计器"""
        self._cfg = config
        self._method = config.get("method", "visual_odometry")  # 定位方法
        self._feature_type = config.get("feature_detector", "ORB")  # 特征检测器类型
        self._max_features = config.get("max_features", 500)  # 最大特征数量
        self._use_depth = config.get("use_depth_for_3d", True)  # 是否使用深度进行 3D 估计

        # 相机内参
        self._fx = camera_intrinsics.get("fx", 600.0)  # 焦距 x
        self._fy = camera_intrinsics.get("fy", 600.0)  # 焦距 y
        self._cx = camera_intrinsics.get("cx", 640.0)  # 主点 x
        self._cy = camera_intrinsics.get("cy", 360.0)  # 主点 y

        # 相机矩阵
        self._K = np.array([
            [self._fx, 0, self._cx],
            [0, self._fy, self._cy],
            [0, 0, 1],
        ], dtype=np.float64)

        # 自身位姿
        self._current_pose = Pose3D()  # 当前位姿
        self._cumulative_R = np.eye(3, dtype=np.float64)  # 累积旋转矩阵
        self._cumulative_t = np.zeros((3, 1), dtype=np.float64)  # 累积平移向量

        # 视觉里程计的前一帧数据
        self._prev_gray = None  # 前一帧灰度图
        self._prev_kp = None  # 前一帧关键点
        self._prev_des = None  # 前一帧描述子

        # 特征检测器
        self._detector = None  # 特征检测器
        self._matcher = None  # 特征匹配器
        self._init_feature_detector()  # 初始化特征检测器

        # 轨迹历史
        self._trajectory: List[np.ndarray] = []  # 轨迹点列表

        logger.info(
            f"PositionEstimator: method={self._method} "
            f"features={self._feature_type}({self._max_features})"
        )


    def _init_feature_detector(self):
        """初始化特征检测器和匹配器"""
        if cv2 is None:
            return

        if self._feature_type == "ORB":
            self._detector = cv2.ORB_create(nfeatures=self._max_features)
            self._matcher = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=False)
        elif self._feature_type == "FAST":
            self._detector = cv2.FastFeatureDetector_create()
            # FAST 不计算描述子，使用 ORB 计算描述子
            self._orb_for_desc = cv2.ORB_create(nfeatures=self._max_features)
            self._matcher = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=False)


    def update_self_pose(self, frame: np.ndarray) -> Pose3D:
        """
        使用视觉里程计更新自身位置

        参数:
            frame: 当前相机帧

        返回:
            更新后的 Pose3D
        """

        if cv2 is None or self._detector is None:
            return self._current_pose

        # Convert to grayscale
        if len(frame.shape) == 3:
            gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
        else:
            gray = frame

        # Detect features
        kp, des = self._detector.detectAndCompute(gray, None)

        if des is None or self._prev_des is None:
            self._prev_gray = gray
            self._prev_kp = kp
            self._prev_des = des
            return self._current_pose

        # Match features
        try:
            matches = self._matcher.knnMatch(self._prev_des, des, k=2)
        except Exception:
            self._prev_gray = gray
            self._prev_kp = kp
            self._prev_des = des
            return self._current_pose

        # Lowe's ratio test
        good_matches = []
        for m_n in matches:
            if len(m_n) == 2:
                m, n = m_n
                if m.distance < 0.75 * n.distance:
                    good_matches.append(m)

        if len(good_matches) < 8:
            self._prev_gray = gray
            self._prev_kp = kp
            self._prev_des = des
            return self._current_pose

        # Get matched point coordinates
        pts1 = np.float32([self._prev_kp[m.queryIdx].pt for m in good_matches])
        pts2 = np.float32([kp[m.trainIdx].pt for m in good_matches])

        # Compute Essential Matrix
        E, mask = cv2.findEssentialMat(pts1, pts2, self._K, method=cv2.RANSAC, prob=0.999, threshold=1.0)

        if E is None:
            self._prev_gray = gray
            self._prev_kp = kp
            self._prev_des = des
            return self._current_pose

        # Recover pose
        _, R, t, _ = cv2.recoverPose(E, pts1, pts2, self._K, mask=mask)

        # Accumulate transformation
        self._cumulative_t += self._cumulative_R @ t
        self._cumulative_R = R @ self._cumulative_R

        # Update pose
        self._current_pose.x = float(self._cumulative_t[0])
        self._current_pose.y = float(self._cumulative_t[1])
        self._current_pose.z = float(self._cumulative_t[2])

        # Extract Euler angles
        sy = np.sqrt(self._cumulative_R[0, 0]**2 + self._cumulative_R[1, 0]**2)
        if sy > 1e-6:
            self._current_pose.roll = float(np.arctan2(self._cumulative_R[2, 1], self._cumulative_R[2, 2]))
            self._current_pose.pitch = float(np.arctan2(-self._cumulative_R[2, 0], sy))
            self._current_pose.yaw = float(np.arctan2(self._cumulative_R[1, 0], self._cumulative_R[0, 0]))

        # Save trajectory
        self._trajectory.append(self._current_pose.position().copy())

        # Update previous frame
        self._prev_gray = gray
        self._prev_kp = kp
        self._prev_des = des

        return self._current_pose

    def estimate_target_positions(
        self,
        tracks: List[Track],
        depth_map: Optional[np.ndarray] = None,
    ) -> List[TargetPosition]:
        """
        估计被跟踪目标的 3D 位置

        参数:
            tracks: 活跃的跟踪对象
            depth_map: 深度图，单位为米 (HxW)

        返回:
            每个跟踪对象的 TargetPosition 列表
        """

        positions = []

        for track in tracks:
            cx, cy = track.center
            x1, y1, x2, y2 = track.bbox

            if depth_map is not None and self._use_depth:
                # Get depth at target center (use median of bbox region for robustness)
                h, w = depth_map.shape[:2]
                rx1 = max(0, min(x1, w - 1))
                ry1 = max(0, min(y1, h - 1))
                rx2 = max(0, min(x2, w - 1))
                ry2 = max(0, min(y2, h - 1))

                if rx2 > rx1 and ry2 > ry1:
                    region = depth_map[ry1:ry2, rx1:rx2]
                    z = float(np.median(region))
                else:
                    z = float(depth_map[
                        min(cy, h - 1), min(cx, w - 1)
                    ])

                # Backproject to 3D
                x_3d = (cx - self._cx) * z / self._fx
                y_3d = (cy - self._cy) * z / self._fy
            else:
                # Without depth, estimate using bbox size heuristic
                bbox_h = y2 - y1
                z = max(0.5, 500.0 / (bbox_h + 1))  # rough estimate
                x_3d = (cx - self._cx) * z / self._fx
                y_3d = (cy - self._cy) * z / self._fy

            pos = TargetPosition(
                track_id=track.track_id,
                x=float(x_3d),
                y=float(y_3d),
                z=float(z),
                class_name=track.class_name,
                confidence=track.confidence,
                pixel_x=cx,
                pixel_y=cy,
            )
            positions.append(pos)

        return positions

    def get_pose(self) -> Pose3D:
        """获取当前位姿"""
        return self._current_pose

    def get_trajectory(self) -> List[np.ndarray]:
        """获取轨迹历史"""
        return self._trajectory.copy()

    def draw_info(
        self,
        frame: np.ndarray,
        targets: List[TargetPosition],
    ) -> np.ndarray:
        """在帧上绘制位姿和目标位置信息"""
        if cv2 is None:
            return frame

        vis = frame.copy()
        h, w = vis.shape[:2]

        # 自身位姿
        pose = self._current_pose
        info_lines = [
            f"Self: ({pose.x:.2f}, {pose.y:.2f}, {pose.z:.2f})m",
            f"Yaw: {np.degrees(pose.yaw):.1f}deg",
        ]

        for i, line in enumerate(info_lines):
            cv2.putText(
                vis, line, (10, h - 60 + i * 20),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 255), 1,
            )

        # 目标位置
        for target in targets:
            cv2.putText(
                vis,
                f"ID{target.track_id}: ({target.x:.1f},{target.y:.1f},{target.z:.1f})m "
                f"D={target.distance():.1f}m",
                (target.pixel_x + 10, target.pixel_y - 10),
                cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1,
            )

        return vis

    def reset(self):
        """重置位姿估计"""
        self._current_pose = Pose3D()
        self._cumulative_R = np.eye(3, dtype=np.float64)
        self._cumulative_t = np.zeros((3, 1), dtype=np.float64)
        self._prev_gray = None
        self._prev_kp = None
        self._prev_des = None
        self._trajectory.clear()
        logger.info("位置估计器已重置")
