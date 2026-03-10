"""
摄像头管理模块
适配 SmartSens SC132GS 单目全局快门摄像头
通过 A1_SDK_SC132GS 接口管理摄像头采集、参数设置和帧获取
"""

import numpy as np
import time
import threading
import logging
from typing import Optional, Tuple, Callable, Dict
from enum import Enum
from dataclasses import dataclass

logger = logging.getLogger(__name__)


class CameraState(Enum):
    """摄像头状态"""
    DISCONNECTED = "disconnected"
    INITIALIZING = "initializing"
    READY = "ready"
    STREAMING = "streaming"
    ERROR = "error"
    HDR_MODE = "hdr_mode"


@dataclass
class CameraConfig:
    """摄像头配置"""
    width: int = 1080
    height: int = 1280
    fps: int = 30
    exposure_us: int = 10000         # 曝光时间 (微秒)
    gain: float = 1.0                # 增益
    auto_exposure: bool = True       # 自动曝光
    auto_white_balance: bool = True  # 自动白平衡
    hdr_enabled: bool = False        # HDR模式
    flip_horizontal: bool = False
    flip_vertical: bool = False
    rotation: int = 0                # 旋转角度 (0, 90, 180, 270)


@dataclass
class FrameInfo:
    """帧信息"""
    frame_id: int = 0
    timestamp: float = 0.0
    exposure_us: int = 0
    gain: float = 0.0
    brightness_mean: float = 0.0
    is_hdr: bool = False


class CameraManager:
    """
    SC132GS 摄像头管理器
    封装SDK调用，提供线程安全的帧获取接口
    """

    def __init__(self, config: Optional[Dict] = None):
        self.config = CameraConfig()
        if config:
            self._apply_config(config)

        self.state = CameraState.DISCONNECTED
        self._frame: Optional[np.ndarray] = None
        self._frame_info = FrameInfo()
        self._lock = threading.Lock()
        self._capture_thread: Optional[threading.Thread] = None
        self._running = False

        # 帧回调
        self._frame_callbacks: list = []

        # SDK句柄 (实际硬件操作时使用)
        self._sdk_handle = None

        # 帧计数和性能统计
        self._frame_count = 0
        self._fps_start_time = time.time()
        self._current_fps = 0.0

        # 亮度监测 (用于HDR切换)
        self._brightness_history: list = []
        self._brightness_window = 30

        logger.info(f"CameraManager初始化: {self.config.width}x{self.config.height}@{self.config.fps}fps")

    def _apply_config(self, config: Dict):
        """应用配置"""
        for key, value in config.items():
            if hasattr(self.config, key):
                setattr(self.config, key, value)

    def initialize(self) -> bool:
        """
        初始化摄像头，连接SDK

        Returns:
            是否成功
        """
        self.state = CameraState.INITIALIZING
        logger.info("正在初始化摄像头...")

        try:
            # 尝试加载 SmartSens SDK
            if self._init_smartsens_sdk():
                logger.info("SmartSens SDK 初始化成功")
            else:
                # 回退到 OpenCV 采集
                logger.warning("SDK初始化失败，尝试OpenCV回退模式")
                if not self._init_opencv_fallback():
                    raise RuntimeError("无法初始化摄像头")

            self.state = CameraState.READY
            logger.info("摄像头初始化完成")
            return True

        except Exception as e:
            self.state = CameraState.ERROR
            logger.error(f"摄像头初始化失败: {e}")
            return False

    def _init_smartsens_sdk(self) -> bool:
        """
        初始化 SmartSens A1 SDK
        SDK路径: smartsens_sdk/A1_SDK_SC132GS
        """
        try:
            # 尝试导入SDK python绑定
            import sys
            sys.path.insert(0, 'smartsens_sdk')

            # 实际SDK调用 - 依赖硬件
            # from smartsens import A1Camera
            # self._sdk_handle = A1Camera()
            # self._sdk_handle.set_resolution(self.config.width, self.config.height)
            # self._sdk_handle.set_fps(self.config.fps)
            # self._sdk_handle.set_exposure(self.config.exposure_us)
            # self._sdk_handle.initialize()

            logger.info("SmartSens SDK 加载 (模拟模式)")
            return False  # 非硬件环境返回False以使用回退

        except ImportError:
            logger.warning("SmartSens SDK 未找到")
            return False

    def _init_opencv_fallback(self) -> bool:
        """OpenCV回退模式"""
        try:
            import cv2
            self._cap = cv2.VideoCapture(0)

            if not self._cap.isOpened():
                # 尝试其他索引
                for idx in [1, 2, -1]:
                    self._cap = cv2.VideoCapture(idx)
                    if self._cap.isOpened():
                        break

            if not self._cap.isOpened():
                logger.warning("未找到摄像头设备，使用合成帧模式")
                self._cap = None
                self._use_synthetic = True
                return True

            self._cap.set(cv2.CAP_PROP_FRAME_WIDTH, self.config.width)
            self._cap.set(cv2.CAP_PROP_FRAME_HEIGHT, self.config.height)
            self._cap.set(cv2.CAP_PROP_FPS, self.config.fps)

            self._use_synthetic = False
            logger.info("OpenCV摄像头初始化成功")
            return True

        except Exception as e:
            logger.error(f"OpenCV初始化失败: {e}")
            self._use_synthetic = True
            return True  # 合成模式始终可用

    def start_capture(self):
        """开始捕获"""
        if self.state not in (CameraState.READY, CameraState.STREAMING):
            logger.error(f"无法开始捕获，当前状态: {self.state}")
            return

        self._running = True
        self._capture_thread = threading.Thread(
            target=self._capture_loop, daemon=True, name="CameraCapture"
        )
        self._capture_thread.start()
        self.state = CameraState.STREAMING
        logger.info("摄像头开始采集")

    def stop_capture(self):
        """停止捕获"""
        self._running = False
        if self._capture_thread and self._capture_thread.is_alive():
            self._capture_thread.join(timeout=3.0)
        self.state = CameraState.READY
        logger.info("摄像头停止采集")

    def _capture_loop(self):
        """采集主循环"""
        logger.info("采集线程启动")
        frame_interval = 1.0 / self.config.fps

        while self._running:
            start_time = time.time()

            try:
                frame = self._capture_frame()
                if frame is not None:
                    # 预处理
                    frame = self._preprocess_frame(frame)

                    # 更新帧信息
                    brightness = float(np.mean(frame))
                    self._update_brightness(brightness)

                    with self._lock:
                        self._frame = frame
                        self._frame_count += 1
                        self._frame_info = FrameInfo(
                            frame_id=self._frame_count,
                            timestamp=time.time(),
                            exposure_us=self.config.exposure_us,
                            gain=self.config.gain,
                            brightness_mean=brightness,
                            is_hdr=self.config.hdr_enabled
                        )

                    # 触发回调
                    for callback in self._frame_callbacks:
                        try:
                            callback(frame, self._frame_info)
                        except Exception as e:
                            logger.error(f"帧回调异常: {e}")

                    # 更新FPS
                    self._update_fps()

            except Exception as e:
                logger.error(f"采集异常: {e}")
                time.sleep(0.1)

            # 帧率控制
            elapsed = time.time() - start_time
            sleep_time = frame_interval - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)

        logger.info("采集线程结束")

    def _capture_frame(self) -> Optional[np.ndarray]:
        """获取一帧"""
        if self._sdk_handle:
            # SDK模式
            # return self._sdk_handle.get_frame()
            pass

        if hasattr(self, '_cap') and self._cap is not None:
            ret, frame = self._cap.read()
            return frame if ret else None

        if hasattr(self, '_use_synthetic') and self._use_synthetic:
            return self._generate_synthetic_frame()

        return None

    def _generate_synthetic_frame(self) -> np.ndarray:
        """生成合成测试帧"""
        frame = np.random.randint(
            50, 200,
            (self.config.height, self.config.width, 3),
            dtype=np.uint8
        )
        # 添加时间文本
        import cv2
        cv2.putText(
            frame, f"SYNTHETIC #{self._frame_count}",
            (10, 30), cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2
        )
        return frame

    def _preprocess_frame(self, frame: np.ndarray) -> np.ndarray:
        """帧预处理"""
        import cv2

        # 翻转
        if self.config.flip_horizontal:
            frame = cv2.flip(frame, 1)
        if self.config.flip_vertical:
            frame = cv2.flip(frame, 0)

        # 旋转
        if self.config.rotation == 90:
            frame = cv2.rotate(frame, cv2.ROTATE_90_CLOCKWISE)
        elif self.config.rotation == 180:
            frame = cv2.rotate(frame, cv2.ROTATE_180)
        elif self.config.rotation == 270:
            frame = cv2.rotate(frame, cv2.ROTATE_90_COUNTERCLOCKWISE)

        return frame

    def get_frame(self) -> Tuple[Optional[np.ndarray], FrameInfo]:
        """
        获取当前帧 (线程安全)

        Returns:
            (帧图像, 帧信息)
        """
        with self._lock:
            if self._frame is not None:
                return self._frame.copy(), self._frame_info
            return None, self._frame_info

    def register_callback(self, callback: Callable):
        """注册帧回调"""
        self._frame_callbacks.append(callback)

    def unregister_callback(self, callback: Callable):
        """取消帧回调"""
        if callback in self._frame_callbacks:
            self._frame_callbacks.remove(callback)

    def set_exposure(self, exposure_us: int):
        """设置曝光时间"""
        self.config.exposure_us = exposure_us
        if self._sdk_handle:
            pass  # self._sdk_handle.set_exposure(exposure_us)
        elif hasattr(self, '_cap') and self._cap:
            self._cap.set(15, exposure_us / 1000.0)  # CV_CAP_PROP_EXPOSURE
        logger.info(f"曝光时间设置为: {exposure_us}us")

    def set_gain(self, gain: float):
        """设置增益"""
        self.config.gain = gain
        logger.info(f"增益设置为: {gain}")

    def _update_brightness(self, brightness: float):
        """更新亮度历史"""
        self._brightness_history.append(brightness)
        if len(self._brightness_history) > self._brightness_window:
            self._brightness_history.pop(0)

    def get_brightness_stats(self) -> Dict:
        """获取亮度统计"""
        if not self._brightness_history:
            return {'mean': 0, 'std': 0, 'min': 0, 'max': 0}

        arr = np.array(self._brightness_history)
        return {
            'mean': float(np.mean(arr)),
            'std': float(np.std(arr)),
            'min': float(np.min(arr)),
            'max': float(np.max(arr))
        }

    def needs_hdr(self) -> bool:
        """判断是否需要切换到HDR模式"""
        stats = self.get_brightness_stats()
        # 亮度变化大 或 整体过暗/过亮
        if stats['std'] > 40:
            return True
        if stats['mean'] < 30 or stats['mean'] > 220:
            return True
        return False

    def _update_fps(self):
        """更新FPS统计"""
        if self._frame_count % 30 == 0:
            now = time.time()
            elapsed = now - self._fps_start_time
            if elapsed > 0:
                self._current_fps = 30.0 / elapsed
            self._fps_start_time = now

    def get_fps(self) -> float:
        """获取当前FPS"""
        return self._current_fps

    def release(self):
        """释放资源"""
        self.stop_capture()

        if self._sdk_handle:
            # self._sdk_handle.release()
            self._sdk_handle = None

        if hasattr(self, '_cap') and self._cap:
            self._cap.release()
            self._cap = None

        self.state = CameraState.DISCONNECTED
        logger.info("摄像头资源已释放")

    def __del__(self):
        self.release()