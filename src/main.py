"""
A1 视觉系统主程序入口
整合所有模块：检测、跟踪、深度、点云、定位、避障、灵巧手、显示
"""

import sys
import os
import time
import signal
import threading
import logging
import argparse
from typing import Optional, Dict

# 添加src到路径
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from config.config_loader import load_config, get_config
from utils.logger import setup_logger
from utils.exception_handler import ExceptionHandler, safe_execute, get_global_handler
from camera.camera_manager import CameraManager
from camera.hdr_controller import HDRController
from detection.yolov8_detector import YOLOv8Detector
from tracking.tracker import MultiObjectTracker
from depth.monocular_depth import MonocularDepthEstimator
from pointcloud.pointcloud_generator import PointCloudGenerator
from localization.position_estimator import PositionEstimator
from navigation.obstacle_avoidance import ObstacleAvoidance
from hand_interface.dexterous_hand import DexterousHandInterface
from display.video_output import VideoOutputManager


class A1VisionSystem:
    """A1视觉系统主控制器"""

    def __init__(self, config_path: str = "src/config/config.yaml"):
        # 加载配置
        self.config = load_config(config_path)

        # 初始化日志
        sys_conf = self.config.get('system', {})
        self.logger = setup_logger(
            name="a1_vision",
            level=sys_conf.get('log_level', 'INFO'),
            log_dir=sys_conf.get('log_dir', 'logs')
        )
        self.logger.info("=" * 60)
        self.logger.info("A1 视觉系统启动")
        self.logger.info("=" * 60)

        # 异常处理器
        self.exception_handler = get_global_handler()
        self.exception_handler.set_emergency_stop_callback(self.emergency_stop)

        # 初始化各模块
        self._init_modules()

        # 控制标志
        self._running = False
        self._paused = False
        self._lock = threading.Lock()

        # 注册信号处理
        signal.signal(signal.SIGINT, self._signal_handler)
        signal.signal(signal.SIGTERM, self._signal_handler)

    def _init_modules(self):
        """初始化所有模块"""
        self.logger.info("初始化模块...")

        # 摄像头
        self.camera = CameraManager(self.config.get('camera', {}))

        # HDR
        self.hdr = HDRController(self.config.get('hdr', {}))

        # 检测
        self.detector = YOLOv8Detector(self.config.get('detection', {}))

        # 跟踪
        self.tracker = MultiObjectTracker(self.config.get('tracking', {}))

        # 深度
        self.depth_estimator = MonocularDepthEstimator(self.config.get('depth', {}))

        # 点云
        self.pointcloud = PointCloudGenerator(self.config.get('pointcloud', {}))

        # 定位
        self.localizer = PositionEstimator(self.config.get('localization', {}))

        # 避障
        self.navigator = ObstacleAvoidance(self.config.get('navigation', {}))

        # 灵巧手
        self.hand = DexterousHandInterface(self.config.get('hand', {}))

        # 显示
        self.display = VideoOutputManager(self.config.get('display', {}))

        self.logger.info("所有模块初始化完成")

    def start(self):
        """启动系统"""
        self.logger.info("正在启动系统...")

        try:
            # 1. 初始化摄像头
            if not self.camera.initialize():
                self.logger.error("摄像头初始化失败")
                return

            # 2. 加载检测模型
            self.detector.load_model()

            # 3. 加载深度模型
            self.depth_estimator.load_model()

            # 4. 初始化显示
            self.display.initialize()

            # 5. 连接灵巧手 (非阻塞)
            threading.Thread(
                target=self._connect_hand, daemon=True
            ).start()

            # 6. 开始采集
            self.camera.start_capture()

            # 7. 主循环
            self._running = True
            self.logger.info("系统启动完成，进入主循环")
            self._main_loop()

        except Exception as e:
            self.exception_handler.handle(e, module="main")
        finally:
            self.shutdown()

    def _main_loop(self):
        """主处理循环"""
        frame_count = 0
        fps_start = time.time()

        while self._running:
            if self._paused:
                time.sleep(0.1)
                continue

            loop_start = time.time()

            try:
                # 获取帧
                frame, frame_info = self.camera.get_frame()
                if frame is None:
                    time.sleep(0.01)
                    continue

                # HDR处理
                if self.hdr.should_enable_hdr(frame):
                    frame = self.hdr.process_frame_hdr(frame, self.camera)

                # 目标检测
                detections = self.detector.detect(frame)

                # 目标跟踪
                tracks = self.tracker.update(detections, frame)

                # 深度估计
                depth_map = self.depth_estimator.estimate(frame)

                # 定位更新
                if depth_map is not None:
                    self.localizer.update(frame, depth_map)

                # 点云生成 (低频率)
                if frame_count % 5 == 0 and depth_map is not None:
                    self.pointcloud.generate(depth_map, frame)

                # 避障
                if depth_map is not None:
                    nav_cmd = self.navigator.compute_navigation_command(
                        depth_map, detections=detections
                    )

                # 可视化输出
                self._render_output(
                    frame, detections, tracks, depth_map, frame_info
                )

                frame_count += 1

                # FPS统计
                if frame_count % 30 == 0:
                    elapsed = time.time() - fps_start
                    fps = 30.0 / elapsed if elapsed > 0 else 0
                    self.logger.info(
                        f"FPS: {fps:.1f} | 检测: {len(detections)} | "
                        f"跟踪: {len(tracks)} | HDR: {self.hdr.is_active}"
                    )
                    fps_start = time.time()

            except KeyboardInterrupt:
                break
            except Exception as e:
                self.exception_handler.handle(e, module="main_loop")
                time.sleep(0.01)

        self.logger.info("主循环结束")

    def _render_output(self, frame, detections, tracks,
                        depth_map, frame_info):
        """渲染输出"""
        import cv2

        vis_frame = frame.copy()

        # 画检测框
        for det in detections:
            bbox = det.get('bbox', None)
            if bbox is None:
                continue
            x1, y1, x2, y2 = [int(v) for v in bbox]
            label = f"{det.get('class', '')} {det.get('confidence', 0):.2f}"
            cv2.rectangle(vis_frame, (x1, y1), (x2, y2), (0, 255, 0), 2)
            cv2.putText(vis_frame, label, (x1, y1 - 5),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.5, (0, 255, 0), 1)

        # 跟踪ID
        for track in tracks:
            bbox = track.get('bbox', None)
            track_id = track.get('id', -1)
            if bbox is None:
                continue
            x1, y1, x2, y2 = [int(v) for v in bbox]
            cv2.putText(vis_frame, f"ID:{track_id}",
                        (x1, y2 + 15), cv2.FONT_HERSHEY_SIMPLEX,
                        0.5, (255, 255, 0), 1)

        # 多视图显示
        views = {'Camera': vis_frame}

        if depth_map is not None:
            depth_vis = cv2.applyColorMap(
                (depth_map / depth_map.max() * 255).astype('uint8')
                if depth_map.max() > 0 else depth_map.astype('uint8'),
                cv2.COLORMAP_MAGMA
            )
            views['Depth'] = depth_vis

        # 避障地图
        obs_map = self.navigator.get_obstacle_map()
        views['Navigation'] = obs_map

        # 位置信息
        pos = self.localizer.get_position()
        status = (
            f"Pos:[{pos['x']:.2f},{pos['y']:.2f},{pos['z']:.2f}] "
            f"HDR:{'ON' if self.hdr.is_active else 'OFF'}"
        )

        self.display.display_multi_view(views)

    def _connect_hand(self):
        """连接灵巧手 (后台)"""
        hand_conf = self.config.get('hand', {})
        conn_type = hand_conf.get('connection_type', 'mock')

        if conn_type == 'serial':
            self.hand.connect(
                port=hand_conf.get('serial_port', '/dev/ttyUSB0'),
                baudrate=hand_conf.get('serial_baudrate', 115200)
            )
        elif conn_type == 'tcp':
            self.hand.connect(
                host=hand_conf.get('tcp_host', '192.168.1.100'),
                port=hand_conf.get('tcp_port', 9000)
            )
        else:
            self.hand.connect()

    def emergency_stop(self):
        """紧急停止"""
        self.logger.critical("!!! 紧急停止 !!!")
        self._running = False

        # 灵巧手释放
        if self.hand.is_connected:
            self.hand.release()

    def shutdown(self):
        """关闭系统"""
        self.logger.info("正在关闭系统...")
        self._running = False

        self.camera.release()
        self.hand.disconnect()
        self.display.release()
        self.depth_estimator.release()

        # 打印异常统计
        stats = self.exception_handler.get_statistics()
        if stats['total'] > 0:
            self.logger.info(f"异常统计: {stats}")

        self.logger.info("系统已关闭")

    def _signal_handler(self, signum, frame):
        """信号处理"""
        self.logger.info(f"收到信号 {signum}, 正在关闭...")
        self._running = False


def parse_args():
    parser = argparse.ArgumentParser(description="A1 Vision System")
    parser.add_argument(
        '--config', type=str,
        default='src/config/config.yaml',
        help='配置文件路径'
    )
    parser.add_argument(
        '--debug', action='store_true',
        help='调试模式'
    )
    return parser.parse_args()


def main():
    args = parse_args()

    system = A1VisionSystem(config_path=args.config)
    system.start()


if __name__ == '__main__':
    main()