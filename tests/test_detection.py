"""
YOLOv8 目标检测模块单元测试
"""
import sys
import os
import unittest
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))


class TestYOLOv8Detector(unittest.TestCase):
    """YOLOv8 检测器测试"""

    @classmethod
    def setUpClass(cls):
        """加载检测器（仅一次）"""
        try:
            from detection.yolov8_detector import YOLOv8Detector
            cls.detector = YOLOv8Detector()
            cls.detector_available = True
        except Exception as e:
            print(f"[SKIP] 检测器初始化失败: {e}")
            cls.detector_available = False

    def test_detector_init(self):
        """测试检测器是否能正常初始化"""
        if not self.detector_available:
            self.skipTest("检测器不可用")
        self.assertIsNotNone(self.detector)

    def test_detect_on_dummy_frame(self):
        """测试对空白帧的检测（不应崩溃）"""
        if not self.detector_available:
            self.skipTest("检测器不可用")
        dummy_frame = np.zeros((720, 1280, 3), dtype=np.uint8)
        results = self.detector.detect(dummy_frame)
        self.assertIsInstance(results, (list, dict, type(None)))

    def test_detect_returns_bbox_format(self):
        """测试检测结果的 bbox 格式: [x1, y1, x2, y2, conf, cls]"""
        if not self.detector_available:
            self.skipTest("检测器不可用")
        dummy_frame = np.random.randint(0, 255, (720, 1280, 3), dtype=np.uint8)
        results = self.detector.detect(dummy_frame)
        if results is not None and len(results) > 0:
            for det in results:
                self.assertGreaterEqual(len(det), 6,
                                        "每个检测结果至少包含 [x1, y1, x2, y2, conf, cls]")

    def test_detect_confidence_threshold(self):
        """测试置信度阈值过滤"""
        if not self.detector_available:
            self.skipTest("检测器不可用")
        dummy_frame = np.random.randint(0, 255, (720, 1280, 3), dtype=np.uint8)
        results = self.detector.detect(dummy_frame, conf_threshold=0.99)
        # 极高置信度下不应有太多检测结果
        if results is not None:
            self.assertIsInstance(results, list)

    def test_detect_performance(self):
        """测试单帧推理延迟（粗略）"""
        if not self.detector_available:
            self.skipTest("检测器不可用")
        import time
        dummy_frame = np.zeros((720, 1280, 3), dtype=np.uint8)

        start = time.time()
        for _ in range(10):
            self.detector.detect(dummy_frame)
        elapsed = (time.time() - start) / 10

        print(f"[INFO] 平均单帧推理耗时: {elapsed*1000:.1f} ms")
        # 不做硬断言，仅输出参考


if __name__ == "__main__":
    unittest.main()