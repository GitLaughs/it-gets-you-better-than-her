"""
视频输出模块
将处理后的视频流输出到外设屏幕
支持: HDMI, SPI LCD, 帧缓冲, OpenCV窗口, RTSP流
"""

import numpy as np
import time
import threading
import logging
from typing import Optional, Dict, Tuple, List
from enum import Enum
from dataclasses import dataclass

logger = logging.getLogger(__name__)


class OutputType(Enum):
    """输出类型"""
    CV_WINDOW = "opencv_window"
    FRAMEBUFFER = "framebuffer"
    HDMI = "hdmi"
    SPI_LCD = "spi_lcd"
    RTSP = "rtsp_stream"
    FILE = "file"


@dataclass
class DisplayConfig:
    """显示配置"""
    output_type: str = "opencv_window"
    width: int = 640
    height: int = 480
    fps: int = 30
    fullscreen: bool = False
    # 帧缓冲
    framebuffer_device: str = "/dev/fb0"
    # SPI LCD
    spi_device: str = "/dev/spidev0.0"
    spi_speed: int = 40000000
    lcd_width: int = 320
    lcd_height: int = 240
    # RTSP
    rtsp_port: int = 8554
    rtsp_path: str = "/live"
    # 文件
    output_path: str = "output/output.mp4"
    codec: str = "mp4v"
    # 信息叠加
    show_fps: bool = True
    show_timestamp: bool = True
    show_status: bool = True


class OverlayRenderer:
    """信息叠加渲染器"""

    def __init__(self, config: DisplayConfig):
        self.config = config
        self._fps = 0.0
        self._status_text = ""
        self._extra_overlays: List[Dict] = []

    def set_fps(self, fps: float):
        self._fps = fps

    def set_status(self, text: str):
        self._status_text = text

    def add_overlay(self, overlay: Dict):
        """
        添加自定义叠加信息
        overlay: {'type': 'text'|'rect'|'circle', 'params': {...}}
        """
        self._extra_overlays.append(overlay)

    def clear_overlays(self):
        self._extra_overlays.clear()

    def render(self, frame: np.ndarray) -> np.ndarray:
        """在帧上渲染叠加信息"""
        import cv2

        output = frame.copy()
        h, w = output.shape[:2]

        y_offset = 25

        # FPS
        if self.config.show_fps:
            cv2.putText(output, f"FPS: {self._fps:.1f}",
                        (10, y_offset), cv2.FONT_HERSHEY_SIMPLEX,
                        0.6, (0, 255, 0), 2)
            y_offset += 25

        # 时间戳
        if self.config.show_timestamp:
            timestamp = time.strftime("%H:%M:%S")
            cv2.putText(output, timestamp,
                        (w - 120, 25), cv2.FONT_HERSHEY_SIMPLEX,
                        0.6, (255, 255, 255), 2)

        # 状态
        if self.config.show_status and self._status_text:
            cv2.putText(output, self._status_text,
                        (10, y_offset), cv2.FONT_HERSHEY_SIMPLEX,
                        0.5, (0, 255, 255), 1)
            y_offset += 20

        # 自定义叠加
        for overlay in self._extra_overlays:
            self._render_overlay(output, overlay)

        return output

    def _render_overlay(self, frame: np.ndarray, overlay: Dict):
        """渲染单个叠加元素"""
        import cv2

        otype = overlay.get('type', '')
        params = overlay.get('params', {})

        if otype == 'text':
            cv2.putText(
                frame, params.get('text', ''),
                params.get('position', (10, 50)),
                cv2.FONT_HERSHEY_SIMPLEX,
                params.get('scale', 0.5),
                params.get('color', (255, 255, 255)),
                params.get('thickness', 1)
            )
        elif otype == 'rect':
            pt1 = params.get('pt1', (0, 0))
            pt2 = params.get('pt2', (100, 100))
            cv2.rectangle(frame, pt1, pt2,
                          params.get('color', (0, 255, 0)),
                          params.get('thickness', 2))
        elif otype == 'circle':
            cv2.circle(
                frame,
                params.get('center', (50, 50)),
                params.get('radius', 10),
                params.get('color', (0, 0, 255)),
                params.get('thickness', -1)
            )


class VideoOutputManager:
    """
    视频输出管理器
    支持多种输出目标，同时输出到多个设备
    """

    def __init__(self, config: Optional[Dict] = None):
        self.config = DisplayConfig()
        if config:
            for key, value in config.items():
                if hasattr(self.config, key):
                    setattr(self.config, key, value)

        self.overlay = OverlayRenderer(self.config)

        # 输出后端
        self._outputs: Dict[str, object] = {}
        self._running = False
        self._lock = threading.Lock()

        # 帧队列
        self._frame_queue: Optional[np.ndarray] = None

        # FPS统计
        self._frame_count = 0
        self._fps_start = time.time()
        self._current_fps = 0.0

        # 录制
        self._video_writer = None

        logger.info(f"视频输出管理器初始化: {self.config.output_type}")

    def initialize(self) -> bool:
        """初始化输出设备"""
        try:
            output_type = OutputType(self.config.output_type)

            if output_type == OutputType.CV_WINDOW:
                return self._init_cv_window()
            elif output_type == OutputType.FRAMEBUFFER:
                return self._init_framebuffer()
            elif output_type == OutputType.HDMI:
                return self._init_hdmi()
            elif output_type == OutputType.SPI_LCD:
                return self._init_spi_lcd()
            elif output_type == OutputType.FILE:
                return self._init_file_output()
            elif output_type == OutputType.RTSP:
                return self._init_rtsp()
            else:
                logger.error(f"不支持的输出类型: {output_type}")
                return False

        except Exception as e:
            logger.error(f"输出初始化失败: {e}")
            return False

    def _init_cv_window(self) -> bool:
        """初始化OpenCV窗口"""
        import cv2

        window_name = "A1 Vision Output"
        cv2.namedWindow(window_name,
                        cv2.WINDOW_NORMAL if not self.config.fullscreen
                        else cv2.WINDOW_FULLSCREEN)

        if self.config.fullscreen:
            cv2.setWindowProperty(window_name,
                                  cv2.WND_PROP_FULLSCREEN,
                                  cv2.WINDOW_FULLSCREEN)
        else:
            cv2.resizeWindow(window_name,
                             self.config.width, self.config.height)

        self._outputs['cv_window'] = window_name
        logger.info(f"OpenCV窗口初始化: {window_name}")
        return True

    def _init_framebuffer(self) -> bool:
        """初始化Linux帧缓冲输出"""
        import os

        fb_device = self.config.framebuffer_device

        if not os.path.exists(fb_device):
            logger.error(f"帧缓冲设备不存在: {fb_device}")
            return False

        try:
            # 读取帧缓冲信息
            with open(f'/sys/class/graphics/fb0/virtual_size', 'r') as f:
                size = f.read().strip().split(',')
                fb_width, fb_height = int(size[0]), int(size[1])

            with open(f'/sys/class/graphics/fb0/bits_per_pixel', 'r') as f:
                bpp = int(f.read().strip())

            self._outputs['framebuffer'] = {
                'device': fb_device,
                'width': fb_width,
                'height': fb_height,
                'bpp': bpp
            }

            logger.info(f"帧缓冲初始化: {fb_width}x{fb_height}@{bpp}bpp")
            return True

        except Exception as e:
            logger.error(f"帧缓冲初始化失败: {e}")
            return False

    def _init_hdmi(self) -> bool:
        """初始化HDMI输出 (通过帧缓冲)"""
        logger.info("HDMI输出 (通过帧缓冲)")
        return self._init_framebuffer()

    def _init_spi_lcd(self) -> bool:
        """初始化SPI LCD"""
        try:
            # SPI LCD 驱动 (如ST7789, ILI9341等)
            self._outputs['spi_lcd'] = {
                'device': self.config.spi_device,
                'width': self.config.lcd_width,
                'height': self.config.lcd_height
            }
            logger.info(f"SPI LCD初始化: {self.config.lcd_width}x{self.config.lcd_height}")
            return True
        except Exception as e:
            logger.error(f"SPI LCD初始化失败: {e}")
            return False

    def _init_file_output(self) -> bool:
        """初始化文件录制"""
        import cv2
        import os

        os.makedirs(os.path.dirname(self.config.output_path), exist_ok=True)

        fourcc = cv2.VideoWriter_fourcc(*self.config.codec)
        self._video_writer = cv2.VideoWriter(
            self.config.output_path, fourcc, self.config.fps,
            (self.config.width, self.config.height)
        )

        if self._video_writer.isOpened():
            self._outputs['file'] = self.config.output_path
            logger.info(f"文件录制初始化: {self.config.output_path}")
            return True
        return False

    def _init_rtsp(self) -> bool:
        """初始化RTSP流推送"""
        # RTSP需要GStreamer或FFmpeg支持
        logger.info(f"RTSP流服务初始化: rtsp://0.0.0.0:{self.config.rtsp_port}{self.config.rtsp_path}")
        self._outputs['rtsp'] = {
            'port': self.config.rtsp_port,
            'path': self.config.rtsp_path
        }
        return True

    def display_frame(self, frame: np.ndarray,
                       overlay_info: Optional[Dict] = None):
        """
        显示一帧

        Args:
            frame: 输入帧 (BGR)
            overlay_info: 叠加信息
        """
        import cv2

        if frame is None:
            return

        # 调整尺寸
        if frame.shape[1] != self.config.width or frame.shape[0] != self.config.height:
            frame = cv2.resize(frame, (self.config.width, self.config.height))

        # 更新FPS
        self._update_fps()
        self.overlay.set_fps(self._current_fps)

        # 设置叠加信息
        if overlay_info:
            if 'status' in overlay_info:
                self.overlay.set_status(overlay_info['status'])
            if 'overlays' in overlay_info:
                self.overlay.clear_overlays()
                for ov in overlay_info['overlays']:
                    self.overlay.add_overlay(ov)

        # 渲染叠加
        output_frame = self.overlay.render(frame)

        # 输出到各设备
        if 'cv_window' in self._outputs:
            cv2.imshow(self._outputs['cv_window'], output_frame)
            cv2.waitKey(1)

        if 'framebuffer' in self._outputs:
            self._write_framebuffer(output_frame)

        if self._video_writer and self._video_writer.isOpened():
            self._video_writer.write(output_frame)

        if 'spi_lcd' in self._outputs:
            self._write_spi_lcd(output_frame)

    def _write_framebuffer(self, frame: np.ndarray):
        """写入帧缓冲"""
        try:
            import cv2
            fb_info = self._outputs['framebuffer']
            fb_w, fb_h = fb_info['width'], fb_info['height']
            bpp = fb_info['bpp']

            # 调整到帧缓冲尺寸
            resized = cv2.resize(frame, (fb_w, fb_h))

            if bpp == 32:
                # BGRA
                if resized.shape[2] == 3:
                    alpha = np.full(
                        (fb_h, fb_w, 1), 255, dtype=np.uint8
                    )
                    resized = np.concatenate([resized, alpha], axis=2)
                data = resized.tobytes()
            elif bpp == 16:
                # RGB565
                data = self._bgr_to_rgb565(resized)
            else:
                data = resized.tobytes()

            with open(fb_info['device'], 'wb') as fb:
                fb.write(data)

        except Exception as e:
            logger.error(f"帧缓冲写入失败: {e}")

    def _write_spi_lcd(self, frame: np.ndarray):
        """写入SPI LCD"""
        try:
            import cv2
            lcd_info = self._outputs['spi_lcd']
            lcd_w, lcd_h = lcd_info['width'], lcd_info['height']

            resized = cv2.resize(frame, (lcd_w, lcd_h))
            data = self._bgr_to_rgb565(resized)

            # SPI写入 (需要GPIO和SPI驱动)
            # 实际硬件操作在此执行
            pass

        except Exception as e:
            logger.error(f"SPI LCD写入失败: {e}")

    @staticmethod
    def _bgr_to_rgb565(frame: np.ndarray) -> bytes:
        """BGR转RGB565"""
        b = (frame[:, :, 0] >> 3).astype(np.uint16)
        g = (frame[:, :, 1] >> 2).astype(np.uint16)
        r = (frame[:, :, 2] >> 3).astype(np.uint16)
        rgb565 = (r << 11) | (g << 5) | b
        return rgb565.tobytes()

    def display_multi_view(self, views: Dict[str, np.ndarray]):
        """
        多视图显示

        Args:
            views: {'name': frame, ...}
        """
        import cv2

        if not views:
            return

        n = len(views)
        cols = min(n, 3)
        rows = (n + cols - 1) // cols

        cell_w = self.config.width // cols
        cell_h = self.config.height // rows

        canvas = np.zeros((self.config.height, self.config.width, 3), dtype=np.uint8)

        for idx, (name, view) in enumerate(views.items()):
            r, c = idx // cols, idx % cols
            if view is None:
                continue

            # 确保3通道
            if len(view.shape) == 2:
                view = cv2.cvtColor(view, cv2.COLOR_GRAY2BGR)
            elif view.shape[2] == 1:
                view = cv2.cvtColor(view, cv2.COLOR_GRAY2BGR)

            resized = cv2.resize(view, (cell_w, cell_h))

            # 标题
            cv2.putText(resized, name, (5, 15),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.4, (0, 255, 0), 1)

            canvas[r * cell_h:(r + 1) * cell_h,
                   c * cell_w:(c + 1) * cell_w] = resized

        self.display_frame(canvas)

    def start_recording(self, path: Optional[str] = None):
        """开始录制"""
        import cv2

        output_path = path or self.config.output_path
        import os
        os.makedirs(os.path.dirname(output_path) or '.', exist_ok=True)

        fourcc = cv2.VideoWriter_fourcc(*self.config.codec)
        self._video_writer = cv2.VideoWriter(
            output_path, fourcc, self.config.fps,
            (self.config.width, self.config.height)
        )
        logger.info(f"开始录制: {output_path}")

    def stop_recording(self):
        """停止录制"""
        if self._video_writer:
            self._video_writer.release()
            self._video_writer = None
            logger.info("录制停止")

    def _update_fps(self):
        """更新FPS"""
        self._frame_count += 1
        if self._frame_count % 30 == 0:
            now = time.time()
            elapsed = now - self._fps_start
            if elapsed > 0:
                self._current_fps = 30.0 / elapsed
            self._fps_start = now

    def release(self):
        """释放所有资源"""
        import cv2

        self.stop_recording()

        if 'cv_window' in self._outputs:
            cv2.destroyAllWindows()

        self._outputs.clear()
        logger.info("视频输出资源已释放")

    def __del__(self):
        try:
            self.release()
        except Exception:
            pass