"""
避障算法模块
基于单目深度估计的障碍物检测与路径规划
支持多种避障策略：VFH (Vector Field Histogram)、APF (人工势场法)
"""

import numpy as np
import time
import threading
from dataclasses import dataclass, field
from typing import List, Tuple, Optional, Dict
from enum import Enum
import logging

logger = logging.getLogger(__name__)


class AvoidanceStrategy(Enum):
    """避障策略枚举"""
    VFH = "vector_field_histogram"
    APF = "artificial_potential_field"
    SIMPLE = "simple_threshold"
    HYBRID = "hybrid"


@dataclass
class Obstacle:
    """障碍物数据结构"""
    x: float                    # 相对x坐标 (米)
    y: float                    # 相对y坐标 (米)
    z: float                    # 相对z坐标 (米)
    distance: float             # 距离 (米)
    angle: float                # 角度 (弧度)
    size: float                 # 估计大小
    confidence: float           # 置信度
    bbox: Optional[Tuple] = None  # 图像中的边界框
    class_name: str = "unknown"   # 障碍物类别


@dataclass
class NavigationCommand:
    """导航指令"""
    linear_velocity: float = 0.0    # 线速度 m/s
    angular_velocity: float = 0.0   # 角速度 rad/s
    steering_angle: float = 0.0     # 转向角 rad
    emergency_stop: bool = False    # 紧急停止
    confidence: float = 0.0         # 指令置信度
    strategy_used: str = ""         # 使用的策略
    timestamp: float = 0.0


@dataclass
class SafetyZone:
    """安全区域定义"""
    critical_distance: float = 0.3    # 紧急停止距离 (米)
    danger_distance: float = 0.8      # 危险距离
    warning_distance: float = 1.5     # 警告距离
    safe_distance: float = 3.0        # 安全距离


class VectorFieldHistogram:
    """
    向量场直方图 (VFH) 避障算法
    将深度图转换为极坐标直方图，找到最佳通行方向
    """

    def __init__(self, num_sectors: int = 72, threshold: float = 0.5):
        """
        Args:
            num_sectors: 直方图扇区数量 (360/num_sectors = 每扇区角度)
            threshold: 障碍物密度阈值
        """
        self.num_sectors = num_sectors
        self.sector_angle = 2 * np.pi / num_sectors
        self.threshold = threshold
        self.histogram = np.zeros(num_sectors)
        self.smoothing_window = 5

        logger.info(f"VFH初始化: {num_sectors}扇区, 阈值={threshold}")

    def update_histogram(self, depth_map: np.ndarray,
                         fov_h: float = 1.2) -> np.ndarray:
        """
        根据深度图更新极坐标直方图

        Args:
            depth_map: 深度图 (H, W)
            fov_h: 水平视场角 (弧度)

        Returns:
            更新后的直方图
        """
        h, w = depth_map.shape[:2]
        self.histogram = np.zeros(self.num_sectors)

        # 将深度图的每列映射到角度
        for col in range(w):
            # 列索引 -> 角度偏移 (-fov/2 ~ +fov/2)
            angle = (col / w - 0.5) * fov_h

            # 映射到扇区索引 (以正前方0度为中心)
            sector_idx = int((angle + np.pi) / self.sector_angle) % self.num_sectors

            # 该列的最小深度（最近障碍物）
            col_depths = depth_map[:, col]
            valid_depths = col_depths[col_depths > 0]

            if len(valid_depths) > 0:
                min_depth = np.min(valid_depths)
                # 障碍物越近，直方图值越大
                obstacle_density = 1.0 / (min_depth + 0.01)
                self.histogram[sector_idx] += obstacle_density

        # 平滑直方图
        kernel = np.ones(self.smoothing_window) / self.smoothing_window
        self.histogram = np.convolve(self.histogram, kernel, mode='same')

        return self.histogram

    def find_best_direction(self, target_angle: float = 0.0,
                            min_gap_width: int = 3) -> Tuple[float, float]:
        """
        找到最佳通行方向

        Args:
            target_angle: 目标方向 (弧度)
            min_gap_width: 最小间隙宽度 (扇区数)

        Returns:
            (最佳角度, 置信度)
        """
        # 找到所有"开放"扇区 (低于阈值)
        open_sectors = self.histogram < self.threshold

        if not np.any(open_sectors):
            logger.warning("VFH: 没有找到开放方向!")
            return 0.0, 0.0

        # 找到连续开放区域（间隙）
        gaps = self._find_gaps(open_sectors, min_gap_width)

        if not gaps:
            logger.warning("VFH: 没有足够宽的间隙")
            return 0.0, 0.0

        # 选择最接近目标方向的间隙
        target_sector = int((target_angle + np.pi) / self.sector_angle) % self.num_sectors
        best_gap = None
        best_cost = float('inf')

        for gap_start, gap_end in gaps:
            gap_center = (gap_start + gap_end) // 2
            # 代价 = 与目标方向的偏差
            cost = min(
                abs(gap_center - target_sector),
                self.num_sectors - abs(gap_center - target_sector)
            )
            if cost < best_cost:
                best_cost = cost
                best_gap = (gap_start, gap_end)

        if best_gap is None:
            return 0.0, 0.0

        gap_center = (best_gap[0] + best_gap[1]) // 2
        best_angle = gap_center * self.sector_angle - np.pi
        gap_width = best_gap[1] - best_gap[0]
        confidence = min(1.0, gap_width / (self.num_sectors * 0.3))

        return best_angle, confidence

    def _find_gaps(self, open_sectors: np.ndarray,
                   min_width: int) -> List[Tuple[int, int]]:
        """找到连续的开放区域"""
        gaps = []
        start = None

        # 处理环形数组
        extended = np.concatenate([open_sectors, open_sectors])

        for i in range(len(extended)):
            if extended[i]:
                if start is None:
                    start = i
            else:
                if start is not None:
                    width = i - start
                    if width >= min_width:
                        gaps.append((start % self.num_sectors,
                                     (i - 1) % self.num_sectors))
                    start = None

        if start is not None:
            width = len(extended) - start
            if width >= min_width:
                gaps.append((start % self.num_sectors,
                             (len(extended) - 1) % self.num_sectors))

        return gaps

    def get_histogram_visualization(self) -> np.ndarray:
        """获取直方图可视化数据"""
        return self.histogram.copy()


class ArtificialPotentialField:
    """
    人工势场法 (APF) 避障
    目标产生引力，障碍物产生斥力，合力决定运动方向
    """

    def __init__(self, k_att: float = 1.0, k_rep: float = 100.0,
                 rep_range: float = 2.0):
        """
        Args:
            k_att: 引力系数
            k_rep: 斥力系数
            rep_range: 斥力影响范围 (米)
        """
        self.k_att = k_att
        self.k_rep = k_rep
        self.rep_range = rep_range

        logger.info(f"APF初始化: k_att={k_att}, k_rep={k_rep}, "
                     f"rep_range={rep_range}m")

    def compute_attractive_force(self, current_pos: np.ndarray,
                                  target_pos: np.ndarray) -> np.ndarray:
        """计算目标引力"""
        direction = target_pos - current_pos
        distance = np.linalg.norm(direction)

        if distance < 0.01:
            return np.zeros(2)

        # 线性引力
        force = self.k_att * direction / distance
        return force

    def compute_repulsive_force(self, current_pos: np.ndarray,
                                 obstacles: List[Obstacle]) -> np.ndarray:
        """计算障碍物斥力"""
        total_force = np.zeros(2)

        for obs in obstacles:
            obs_pos = np.array([obs.x, obs.y])
            direction = current_pos - obs_pos  # 远离障碍物
            distance = np.linalg.norm(direction)

            if distance < 0.01:
                distance = 0.01

            if distance < self.rep_range:
                # 斥力公式: F = k_rep * (1/d - 1/d0) * (1/d^2)
                magnitude = self.k_rep * (
                    1.0 / distance - 1.0 / self.rep_range
                ) * (1.0 / distance ** 2)

                force = magnitude * direction / distance
                total_force += force * obs.confidence

        return total_force

    def compute_total_force(self, current_pos: np.ndarray,
                            target_pos: np.ndarray,
                            obstacles: List[Obstacle]) -> Tuple[np.ndarray, float]:
        """
        计算合力

        Returns:
            (合力向量, 合力大小)
        """
        f_att = self.compute_attractive_force(current_pos, target_pos)
        f_rep = self.compute_repulsive_force(current_pos, obstacles)
        total = f_att + f_rep

        magnitude = np.linalg.norm(total)
        return total, magnitude


class ObstacleAvoidance:
    """
    障碍物避障主控制器
    整合深度感知、目标检测和路径规划
    """

    def __init__(self, config: Optional[Dict] = None):
        """
        Args:
            config: 配置字典
        """
        self.config = config or {}
        self._setup_from_config()

        # 避障算法实例
        self.vfh = VectorFieldHistogram(
            num_sectors=self.config.get('vfh_sectors', 72),
            threshold=self.config.get('vfh_threshold', 0.5)
        )
        self.apf = ArtificialPotentialField(
            k_att=self.config.get('apf_k_att', 1.0),
            k_rep=self.config.get('apf_k_rep', 100.0),
            rep_range=self.config.get('apf_rep_range', 2.0)
        )

        # 安全区域
        self.safety_zone = SafetyZone(
            critical_distance=self.config.get('critical_distance', 0.3),
            danger_distance=self.config.get('danger_distance', 0.8),
            warning_distance=self.config.get('warning_distance', 1.5),
            safe_distance=self.config.get('safe_distance', 3.0)
        )

        # 状态
        self.current_obstacles: List[Obstacle] = []
        self.last_command = NavigationCommand()
        self.emergency_stop_active = False
        self._lock = threading.Lock()

        # 历史命令 (用于平滑)
        self.command_history: List[NavigationCommand] = []
        self.max_history = 10

        # 策略选择
        self.strategy = AvoidanceStrategy(
            self.config.get('strategy', 'hybrid')
        )

        logger.info(f"避障控制器初始化完成, 策略: {self.strategy.value}")

    def _setup_from_config(self):
        """从配置初始化参数"""
        self.max_linear_speed = self.config.get('max_linear_speed', 0.5)
        self.max_angular_speed = self.config.get('max_angular_speed', 1.0)
        self.smoothing_factor = self.config.get('smoothing_factor', 0.7)
        self.fov_horizontal = self.config.get('fov_horizontal', 1.2)  # 弧度

    def update_obstacles_from_depth(self, depth_map: np.ndarray,
                                     detections: Optional[List] = None) -> List[Obstacle]:
        """
        从深度图和检测结果更新障碍物列表

        Args:
            depth_map: 深度图
            detections: YOLO检测结果列表

        Returns:
            障碍物列表
        """
        obstacles = []
        h, w = depth_map.shape[:2]

        # 1. 从深度图中提取通用障碍物 (网格扫描)
        grid_rows, grid_cols = 4, 8
        cell_h, cell_w = h // grid_rows, w // grid_cols

        for r in range(grid_rows):
            for c in range(grid_cols):
                cell = depth_map[
                    r * cell_h: (r + 1) * cell_h,
                    c * cell_w: (c + 1) * cell_w
                ]
                valid = cell[cell > 0]

                if len(valid) == 0:
                    continue

                min_depth = np.min(valid)
                mean_depth = np.mean(valid)

                if min_depth < self.safety_zone.safe_distance:
                    # 计算障碍物在图像中的角度
                    center_x = (c + 0.5) * cell_w
                    angle = (center_x / w - 0.5) * self.fov_horizontal

                    # 估计3D位置
                    x = min_depth * np.sin(angle)
                    y = 0.0  # 单目无法精确获取y
                    z = min_depth * np.cos(angle)

                    # 置信度基于深度一致性
                    depth_std = np.std(valid)
                    confidence = max(0.3, 1.0 - depth_std / mean_depth)

                    obs = Obstacle(
                        x=x, y=y, z=z,
                        distance=min_depth,
                        angle=angle,
                        size=float(cell_w * cell_h),
                        confidence=confidence,
                        bbox=(c * cell_w, r * cell_h,
                              (c + 1) * cell_w, (r + 1) * cell_h),
                        class_name="obstacle"
                    )
                    obstacles.append(obs)

        # 2. 从检测结果中提取特定障碍物
        if detections:
            for det in detections:
                bbox = det.get('bbox', None)
                class_name = det.get('class', 'unknown')

                if bbox is None:
                    continue

                x1, y1, x2, y2 = [int(v) for v in bbox]
                # 限制在图像范围内
                x1 = max(0, min(x1, w - 1))
                x2 = max(0, min(x2, w - 1))
                y1 = max(0, min(y1, h - 1))
                y2 = max(0, min(y2, h - 1))

                if x2 <= x1 or y2 <= y1:
                    continue

                roi_depth = depth_map[y1:y2, x1:x2]
                valid = roi_depth[roi_depth > 0]

                if len(valid) == 0:
                    continue

                distance = np.median(valid)
                center_x = (x1 + x2) / 2.0
                angle = (center_x / w - 0.5) * self.fov_horizontal

                obs = Obstacle(
                    x=distance * np.sin(angle),
                    y=0.0,
                    z=distance * np.cos(angle),
                    distance=distance,
                    angle=angle,
                    size=float((x2 - x1) * (y2 - y1)),
                    confidence=det.get('confidence', 0.5),
                    bbox=(x1, y1, x2, y2),
                    class_name=class_name
                )
                obstacles.append(obs)

        with self._lock:
            self.current_obstacles = obstacles

        logger.debug(f"检测到 {len(obstacles)} 个障碍物")
        return obstacles

    def compute_navigation_command(self, depth_map: np.ndarray,
                                    target_angle: float = 0.0,
                                    target_distance: float = 5.0,
                                    detections: Optional[List] = None
                                    ) -> NavigationCommand:
        """
        计算导航指令

        Args:
            depth_map: 深度图
            target_angle: 目标方向角度 (弧度, 0=正前方)
            target_distance: 目标距离 (米)
            detections: 检测结果

        Returns:
            导航指令
        """
        timestamp = time.time()

        # 更新障碍物
        obstacles = self.update_obstacles_from_depth(depth_map, detections)

        # 检查紧急停止条件
        if self._check_emergency_stop(obstacles):
            cmd = NavigationCommand(
                linear_velocity=0.0,
                angular_velocity=0.0,
                emergency_stop=True,
                confidence=1.0,
                strategy_used="emergency_stop",
                timestamp=timestamp
            )
            self.last_command = cmd
            logger.warning("紧急停止！前方障碍物过近")
            return cmd

        # 根据策略计算
        if self.strategy == AvoidanceStrategy.VFH:
            cmd = self._compute_vfh(depth_map, target_angle, timestamp)
        elif self.strategy == AvoidanceStrategy.APF:
            cmd = self._compute_apf(obstacles, target_angle,
                                     target_distance, timestamp)
        elif self.strategy == AvoidanceStrategy.SIMPLE:
            cmd = self._compute_simple(depth_map, obstacles, timestamp)
        elif self.strategy == AvoidanceStrategy.HYBRID:
            cmd = self._compute_hybrid(depth_map, obstacles,
                                        target_angle, target_distance, timestamp)
        else:
            cmd = NavigationCommand(timestamp=timestamp)

        # 平滑输出
        cmd = self._smooth_command(cmd)

        self.last_command = cmd
        self.command_history.append(cmd)
        if len(self.command_history) > self.max_history:
            self.command_history.pop(0)

        return cmd

    def _check_emergency_stop(self, obstacles: List[Obstacle]) -> bool:
        """检查是否需要紧急停止"""
        for obs in obstacles:
            if (obs.distance < self.safety_zone.critical_distance
                    and obs.confidence > 0.5
                    and abs(obs.angle) < np.pi / 4):  # 前方90度内
                self.emergency_stop_active = True
                return True

        self.emergency_stop_active = False
        return False

    def _compute_vfh(self, depth_map: np.ndarray,
                      target_angle: float,
                      timestamp: float) -> NavigationCommand:
        """VFH策略"""
        self.vfh.update_histogram(depth_map, self.fov_horizontal)
        best_angle, confidence = self.vfh.find_best_direction(
            target_angle, min_gap_width=3
        )

        # 根据最佳方向计算速度
        angle_diff = best_angle - target_angle
        angular_vel = np.clip(
            angle_diff * 2.0, -self.max_angular_speed, self.max_angular_speed
        )

        # 前方越空旷，线速度越大
        front_sectors = self.vfh.histogram[
            self.vfh.num_sectors // 2 - 5: self.vfh.num_sectors // 2 + 5
        ]
        front_clearance = 1.0 - np.mean(front_sectors) / (
            np.max(self.vfh.histogram) + 0.01
        )
        linear_vel = self.max_linear_speed * front_clearance * confidence

        return NavigationCommand(
            linear_velocity=linear_vel,
            angular_velocity=angular_vel,
            steering_angle=best_angle,
            confidence=confidence,
            strategy_used="VFH",
            timestamp=timestamp
        )

    def _compute_apf(self, obstacles: List[Obstacle],
                      target_angle: float, target_distance: float,
                      timestamp: float) -> NavigationCommand:
        """APF策略"""
        current_pos = np.array([0.0, 0.0])
        target_pos = np.array([
            target_distance * np.sin(target_angle),
            target_distance * np.cos(target_angle)
        ])

        total_force, magnitude = self.apf.compute_total_force(
            current_pos, target_pos, obstacles
        )

        if magnitude < 0.01:
            return NavigationCommand(
                linear_velocity=0.0,
                angular_velocity=0.0,
                confidence=0.5,
                strategy_used="APF",
                timestamp=timestamp
            )

        # 合力方向 -> 转向, 合力大小 -> 速度
        direction = np.arctan2(total_force[0], total_force[1])
        angular_vel = np.clip(
            direction * 2.0, -self.max_angular_speed, self.max_angular_speed
        )

        # 速度与合力大小成正比，但受限
        linear_vel = min(
            self.max_linear_speed,
            magnitude * 0.1
        )

        # 如果转角过大，降低线速度
        if abs(direction) > np.pi / 4:
            linear_vel *= 0.5

        confidence = min(1.0, magnitude / 10.0)

        return NavigationCommand(
            linear_velocity=linear_vel,
            angular_velocity=angular_vel,
            steering_angle=direction,
            confidence=confidence,
            strategy_used="APF",
            timestamp=timestamp
        )

    def _compute_simple(self, depth_map: np.ndarray,
                         obstacles: List[Obstacle],
                         timestamp: float) -> NavigationCommand:
        """简单阈值避障"""
        h, w = depth_map.shape[:2]

        # 将前方区域分为左中右三部分
        third = w // 3
        left_region = depth_map[:, :third]
        center_region = depth_map[:, third:2 * third]
        right_region = depth_map[:, 2 * third:]

        def safe_min_depth(region):
            valid = region[region > 0]
            return np.min(valid) if len(valid) > 0 else float('inf')

        left_min = safe_min_depth(left_region)
        center_min = safe_min_depth(center_region)
        right_min = safe_min_depth(right_region)

        # 决策逻辑
        if center_min > self.safety_zone.safe_distance:
            # 前方安全，直行
            return NavigationCommand(
                linear_velocity=self.max_linear_speed,
                angular_velocity=0.0,
                confidence=0.9,
                strategy_used="SIMPLE_forward",
                timestamp=timestamp
            )
        elif center_min > self.safety_zone.warning_distance:
            # 前方有障碍但较远，减速
            speed_ratio = (center_min - self.safety_zone.warning_distance) / (
                self.safety_zone.safe_distance - self.safety_zone.warning_distance
            )
            return NavigationCommand(
                linear_velocity=self.max_linear_speed * speed_ratio,
                angular_velocity=0.0,
                confidence=0.7,
                strategy_used="SIMPLE_slow",
                timestamp=timestamp
            )
        else:
            # 需要转向，选择更空旷的方向
            if left_min > right_min:
                angular_vel = self.max_angular_speed * 0.5
            else:
                angular_vel = -self.max_angular_speed * 0.5

            return NavigationCommand(
                linear_velocity=self.max_linear_speed * 0.3,
                angular_velocity=angular_vel,
                steering_angle=angular_vel,
                confidence=0.6,
                strategy_used="SIMPLE_turn",
                timestamp=timestamp
            )

    def _compute_hybrid(self, depth_map: np.ndarray,
                         obstacles: List[Obstacle],
                         target_angle: float,
                         target_distance: float,
                         timestamp: float) -> NavigationCommand:
        """
        混合策略：远距离用APF，近距离用VFH
        """
        nearest_distance = float('inf')
        for obs in obstacles:
            if obs.distance < nearest_distance:
                nearest_distance = obs.distance

        if nearest_distance < self.safety_zone.danger_distance:
            # 近距离用VFH (反应更快)
            cmd = self._compute_vfh(depth_map, target_angle, timestamp)
            cmd.strategy_used = "HYBRID_VFH"
        elif nearest_distance < self.safety_zone.warning_distance:
            # 中距离混合
            cmd_vfh = self._compute_vfh(depth_map, target_angle, timestamp)
            cmd_apf = self._compute_apf(obstacles, target_angle,
                                         target_distance, timestamp)
            # 加权混合
            alpha = (nearest_distance - self.safety_zone.danger_distance) / (
                self.safety_zone.warning_distance - self.safety_zone.danger_distance
            )
            cmd = NavigationCommand(
                linear_velocity=(1 - alpha) * cmd_vfh.linear_velocity + alpha * cmd_apf.linear_velocity,
                angular_velocity=(1 - alpha) * cmd_vfh.angular_velocity + alpha * cmd_apf.angular_velocity,
                steering_angle=(1 - alpha) * cmd_vfh.steering_angle + alpha * cmd_apf.steering_angle,
                confidence=(cmd_vfh.confidence + cmd_apf.confidence) / 2,
                strategy_used="HYBRID_blend",
                timestamp=timestamp
            )
        else:
            # 远距离用APF (更平滑)
            cmd = self._compute_apf(obstacles, target_angle,
                                     target_distance, timestamp)
            cmd.strategy_used = "HYBRID_APF"

        return cmd

    def _smooth_command(self, cmd: NavigationCommand) -> NavigationCommand:
        """平滑输出指令"""
        if not self.command_history:
            return cmd

        prev = self.command_history[-1]
        alpha = self.smoothing_factor

        cmd.linear_velocity = (
            alpha * cmd.linear_velocity + (1 - alpha) * prev.linear_velocity
        )
        cmd.angular_velocity = (
            alpha * cmd.angular_velocity + (1 - alpha) * prev.angular_velocity
        )

        return cmd

    def get_obstacle_map(self, width: int = 200,
                          height: int = 200,
                          scale: float = 50.0) -> np.ndarray:
        """
        生成俯视障碍物地图 (用于可视化)

        Args:
            width: 地图宽度像素
            height: 地图高度像素
            scale: 缩放系数 (像素/米)

        Returns:
            BGR图像
        """
        import cv2

        map_img = np.zeros((height, width, 3), dtype=np.uint8)
        center_x, center_y = width // 2, height - 20

        # 画自身位置
        cv2.circle(map_img, (center_x, center_y), 5, (0, 255, 0), -1)

        # 画安全区域
        for dist, color in [
            (self.safety_zone.critical_distance, (0, 0, 255)),
            (self.safety_zone.danger_distance, (0, 165, 255)),
            (self.safety_zone.warning_distance, (0, 255, 255)),
        ]:
            radius = int(dist * scale)
            cv2.ellipse(map_img, (center_x, center_y), (radius, radius),
                        0, -180, 0, color, 1)

        # 画障碍物
        with self._lock:
            for obs in self.current_obstacles:
                ox = center_x + int(obs.x * scale)
                oy = center_y - int(obs.z * scale)

                if 0 <= ox < width and 0 <= oy < height:
                    if obs.distance < self.safety_zone.danger_distance:
                        color = (0, 0, 255)
                    elif obs.distance < self.safety_zone.warning_distance:
                        color = (0, 165, 255)
                    else:
                        color = (255, 255, 0)

                    radius = max(2, int(obs.size / 1000))
                    cv2.circle(map_img, (ox, oy), radius, color, -1)

        # 画导航方向
        if self.last_command.confidence > 0:
            angle = self.last_command.steering_angle
            length = int(self.last_command.linear_velocity * scale * 2)
            end_x = center_x + int(length * np.sin(angle))
            end_y = center_y - int(length * np.cos(angle))
            cv2.arrowedLine(map_img, (center_x, center_y),
                            (end_x, end_y), (0, 255, 0), 2)

        return map_img

    def reset(self):
        """重置避障器状态"""
        with self._lock:
            self.current_obstacles.clear()
        self.command_history.clear()
        self.emergency_stop_active = False
        self.last_command = NavigationCommand()
        logger.info("避障控制器已重置")