"""
端到端集成测试
验证：摄像头采集 → 检测 → 跟踪 → 深度 → 点云 → 定位 → 避障 完整流水线
"""
import sys
import os
import unittest
import time
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))


class TestPipelineIntegration(unittest.TestCase):
    """完整流水线集成测试"""

    STABILITY_DURATION = 60  # 秒

    @classmethod
    def setUpClass(cls):
        """尝试初始化所有模块"""
        cls.modules = {}
        cls.all_ready = True

        module_map = {
            'detector':   ('detection.yolov8_detector', 'YOLOv8Detector'),
            'tracker':    ('tracking.tracker', 'Tracker'),
            'depth':      ('depth.monocular_depth', 'MonocularDepthEstimator'),
            'pointcloud': ('pointcloud.pointcloud_generator', 'PointCloudGenerator'),
            'locator':    ('localization.position_estimator', 'PositionEstimator'),
            'navigator':  ('navigation.obstacle_avoidance', 'ObstacleAvoidance'),
        }

        for key, (mod_path, cls_name) in module_map.items():
            try:
                mod = __import__(mod_path, fromlist=[cls_name])
                cls.modules[key] = getattr(mod, cls_name)()
                print(f"[✓] {key} 初始化成功")
            except Exception as e:
                print(f"[✗] {key} 初始化失败: {e}")
                cls.all_ready = False

    def test_single_frame_pipeline(self):
        """单帧流水线完整执行"""
        if not self.all_ready:
            self.skipTest("部分模块不可用")

        frame = np.random.randint(0, 255, (720, 1280, 3), dtype=np.uint8)

        # 检测
        detections = self.modules['detector'].detect(frame)
        self.assertIsNotNone(detections)

        # 跟踪
        tracks = self.modules['tracker'].update(
            detections if detections else [], frame
        )

        # 深度
        depth_map = self.modules['depth'].estimate(frame)
        self.assertIsNotNone(depth_map)

        # 点云
        pc = self.modules['pointcloud'].generate(depth_map, frame)

        # 定位
        position = self.modules['locator'].estimate(tracks, depth_map)

        # 避障
        cmd = self.modules['navigator'].compute(depth_map, tracks)

    def test_pipeline_no_crash_60s(self):
        """流水线连续运行 60 秒不崩溃"""
        if not self.all_ready:
            self.skipTest("部分模块不可用")

        start = time.time()
        frame_count = 0
        errors = []

        while (time.time() - start) < self.STABILITY_DURATION:
            try:
                frame = np.random.randint(0, 255, (720, 1280, 3), dtype=np.uint8)
                det = self.modules['detector'].detect(frame)
                trk = self.modules['tracker'].update(det or [], frame)
                dep = self.modules['depth'].estimate(frame)
                frame_count += 1
            except Exception as e:
                errors.append((time.time() - start, str(e)))

        elapsed = time.time() - start
        fps = frame_count / elapsed if elapsed > 0 else 0
        print(f"\n[INFO] 运行 {elapsed:.1f}s, 总帧数 {frame_count}, FPS={fps:.1f}")
        if errors:
            print(f"[WARN] 错误数: {len(errors)}")
            for t, e in errors[:5]:
                print(f"  @{t:.1f}s: {e}")

        self.assertEqual(len(errors), 0, f"60 秒内出现 {len(errors)} 个错误")

    def test_pipeline_latency(self):
        """端到端延迟 P95 测量"""
        if not self.all_ready:
            self.skipTest("部分模块不可用")

        latencies = []
        for _ in range(100):
            frame = np.random.randint(0, 255, (720, 1280, 3), dtype=np.uint8)
            t0 = time.time()
            det = self.modules['detector'].detect(frame)
            self.modules['tracker'].update(det or [], frame)
            self.modules['depth'].estimate(frame)
            latencies.append(time.time() - t0)

        latencies.sort()
        p50 = latencies[49] * 1000
        p95 = latencies[94] * 1000
        p99 = latencies[98] * 1000
        print(f"\n[INFO] 延迟 P50={p50:.1f}ms  P95={p95:.1f}ms  P99={p99:.1f}ms")


if __name__ == "__main__":
    unittest.main()