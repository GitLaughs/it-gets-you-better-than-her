"""
HDR模式控制器
在光线剧烈变化时自动切换HDR模式
基于多帧合成或传感器内置HDR功能
"""

import numpy as np
import time
import threading
import logging
from typing import Optional, Dict, Tuple
from enum import Enum

logger = logging.getLogger(__name__)


class HDRMode(Enum):
    """HDR模式"""
    OFF = "off"
    SOFTWARE = "software_hdr"     # 软件多帧合成HDR
    HARDWARE = "hardware_hdr"     # 硬件传感器HDR (SC132GS支持)
    AUTO = "auto"


class HDRController:
    """
    HDR控制器
    监测光线变化，自动切换HDR模式
    """

    def __init__(self, config: Optional[Dict] = None):
        config = config or {}

        self.mode = HDRMode(config.get('hdr_mode', 'auto'))
        self.enabled = config.get('hdr_enabled', True)

        # 阈值参数
        self.brightness_low_threshold = config.get('brightness_low', 30)
        self.brightness_high_threshold = config.get('brightness_high', 220)
        self.variance_threshold = config.get('variance_threshold', 40)
        self.switch_cooldown = config.get('switch_cooldown', 2.0)  # 切换冷却时间(秒)

        # 软件HDR参数
        self.num_exposures = config.get('num_exposures', 3)
        self.exposure_ratios = config.get(
            'exposure_ratios', [0.25, 1.0, 4.0]
        )

        # 状态
        self._is_hdr_active = False
        self._last_switch_time = 0.0
        self._frame_buffer = []
        self._lock = threading.Lock()

        # 监测统计
        self._brightness_history = []
        self._history_max = 60

        logger.info(f"HDR控制器初始化: 模式={self.mode.value}, 启用={self.enabled}")

    def should_enable_hdr(self, frame: np.ndarray) -> bool:
        """
        判断是否应启用HDR

        Args:
            frame: 当前帧

        Returns:
            是否需要HDR
        """
        if not self.enabled:
            return False

        brightness = float(np.mean(frame))
        self._brightness_history.append(brightness)
        if len(self._brightness_history) > self._history_max:
            self._brightness_history.pop(0)

        # 检查冷却时间
        if time.time() - self._last_switch_time < self.switch_cooldown:
            return self._is_hdr_active

        # 判断条件
        needs_hdr = False

        # 条件1: 整体过暗或过亮
        if brightness < self.brightness_low_threshold:
            needs_hdr = True
            logger.debug(f"亮度过低: {brightness:.1f}")
        elif brightness > self.brightness_high_threshold:
            needs_hdr = True
            logger.debug(f"亮度过高: {brightness:.1f}")

        # 条件2: 亮度变化剧烈
        if len(self._brightness_history) >= 10:
            recent = np.array(self._brightness_history[-10:])
            variance = np.std(recent)
            if variance > self.variance_threshold:
                needs_hdr = True
                logger.debug(f"亮度波动大: std={variance:.1f}")

        # 条件3: 帧内动态范围大 (高对比度场景)
        if len(frame.shape) == 3:
            gray = np.mean(frame, axis=2)
        else:
            gray = frame.astype(float)

        # 计算图像直方图的动态范围
        p5 = np.percentile(gray, 5)
        p95 = np.percentile(gray, 95)
        dynamic_range = p95 - p5

        if dynamic_range > 180:  # 接近满量程的动态范围
            needs_hdr = True
            logger.debug(f"高动态范围场景: DR={dynamic_range:.1f}")

        # 状态切换
        if needs_hdr != self._is_hdr_active:
            self._is_hdr_active = needs_hdr
            self._last_switch_time = time.time()
            state_str = "启用" if needs_hdr else "禁用"
            logger.info(f"HDR模式{state_str}")

        return self._is_hdr_active

    def process_frame_hdr(self, frame: np.ndarray,
                           camera_manager=None) -> np.ndarray:
        """
        对帧进行HDR处理

        Args:
            frame: 输入帧
            camera_manager: 摄像头管理器 (用于硬件HDR控制曝光)

        Returns:
            HDR处理后的帧
        """
        if not self._is_hdr_active:
            return frame

        if self.mode == HDRMode.HARDWARE:
            return self._hardware_hdr(frame, camera_manager)
        elif self.mode == HDRMode.SOFTWARE:
            return self._software_hdr(frame)
        elif self.mode == HDRMode.AUTO:
            # 优先硬件HDR, 回退到软件
            if camera_manager and hasattr(camera_manager, '_sdk_handle'):
                return self._hardware_hdr(frame, camera_manager)
            return self._software_hdr(frame)

        return frame

    def _hardware_hdr(self, frame: np.ndarray,
                       camera_manager) -> np.ndarray:
        """
        硬件HDR模式
        利用SC132GS传感器的HDR功能
        """
        if camera_manager and camera_manager._sdk_handle:
            # 实际SDK调用
            # camera_manager._sdk_handle.enable_hdr(True)
            # return camera_manager._sdk_handle.get_hdr_frame()
            pass

        # 无硬件支持时回退到软件HDR
        return self._software_hdr(frame)

    def _software_hdr(self, frame: np.ndarray) -> np.ndarray:
        """
        软件HDR模式
        使用多帧曝光合成（Exposure Fusion / Tone Mapping）
        在单帧模式下使用局部色调映射模拟HDR效果
        """
        try:
            import cv2

            # 单帧HDR增强 (无法控制多帧曝光时)
            return self._single_frame_hdr(frame)

        except ImportError:
            return self._simple_hdr_fallback(frame)

    def _single_frame_hdr(self, frame: np.ndarray) -> np.ndarray:
        """
        单帧HDR增强
        使用CLAHE (对比度受限自适应直方图均衡化)
        """
        import cv2

        if len(frame.shape) == 2:
            # 灰度图
            clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
            return clahe.apply(frame)

        # 彩色图 - 在LAB空间处理
        lab = cv2.cvtColor(frame, cv2.COLOR_BGR2LAB)
        l_channel, a_channel, b_channel = cv2.split(lab)

        # 对亮度通道应用CLAHE
        clahe = cv2.createCLAHE(clipLimit=3.0, tileGridSize=(8, 8))
        l_enhanced = clahe.apply(l_channel)

        # 合并通道
        lab_enhanced = cv2.merge([l_enhanced, a_channel, b_channel])
        result = cv2.cvtColor(lab_enhanced, cv2.COLOR_LAB2BGR)

        return result

    def multi_exposure_hdr(self, frames: list,
                            exposure_times: list) -> np.ndarray:
        """
        多帧曝光HDR合成

        Args:
            frames: 不同曝光的帧列表
            exposure_times: 对应的曝光时间列表

        Returns:
            HDR合成结果
        """
        import cv2

        if len(frames) < 2:
            return frames[0] if frames else np.zeros((100, 100, 3), dtype=np.uint8)

        exposure_times_np = np.array(exposure_times, dtype=np.float32)

        # 方法1: Mertens Exposure Fusion (无需相机响应曲线)
        merge_mertens = cv2.createMergeMertens()
        hdr = merge_mertens.process(frames)

        # 归一化到 0-255
        hdr = np.clip(hdr * 255, 0, 255).astype(np.uint8)

        return hdr

    def _simple_hdr_fallback(self, frame: np.ndarray) -> np.ndarray:
        """无OpenCV时的简单HDR回退"""
        frame_float = frame.astype(np.float32) / 255.0

        # 简单的Gamma校正
        brightness = np.mean(frame_float)

        if brightness < 0.3:
            gamma = 0.6  # 提亮
        elif brightness > 0.7:
            gamma = 1.5  # 压暗
        else:
            gamma = 1.0

        corrected = np.power(frame_float, gamma)
        return (corrected * 255).astype(np.uint8)

    @property
    def is_active(self) -> bool:
        """HDR是否激活"""
        return self._is_hdr_active

    def get_status(self) -> Dict:
        """获取状态信息"""
        return {
            'mode': self.mode.value,
            'active': self._is_hdr_active,
            'enabled': self.enabled,
            'brightness_history_len': len(self._brightness_history),
            'last_brightness': self._brightness_history[-1] if self._brightness_history else 0,
        }

    def reset(self):
        """重置"""
        self._is_hdr_active = False
        self._brightness_history.clear()
        self._frame_buffer.clear()
        self._last_switch_time = 0.0
        logger.info("HDR控制器已重置")