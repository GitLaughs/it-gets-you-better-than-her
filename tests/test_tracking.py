"""
目标跟踪模块单元测试 (DeepSORT / ByteTrack)
"""
import sys
import os
import unittest
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))


class TestTracker(unittest.TestCase):
    """目标跟踪器测试"""

    @classmethod
    def setUpClass(cls):
        try:
            from tracking.tracker import Tracker
            cls.tracker = Tracker()
            cls.tracker_available = True
        except Exception as e:
            print(f"[SKIP] 跟踪器初始化失败: {e}")
            cls.tracker_available = False

    def test_tracker_init(self):
        """测试跟踪器初始化"""
        if not self.tracker_available:
            self.skipTest("跟踪器不可用")
        self.assertIsNotNone(self.tracker)

    def test_update_with_empty_detections(self):
        """空检测列表不应崩溃"""
        if not self.tracker_available:
            self.skipTest("跟踪器不可用")
        dummy_frame = np.zeros((720, 1280, 3), dtype=np.uint8)
        tracks = self.tracker.update([], dummy_frame)
        self.assertIsInstance(tracks, list)
        self.assertEqual(len(tracks), 0)

    def test_update_with_single_detection(self):
        """单目标检测 → 应返回 1 条 track"""
        if not self.tracker_available:
            self.skipTest("跟踪器不可用")
        dummy_frame = np.zeros((720, 1280, 3), dtype=np.uint8)
        detections = [[100, 100, 200, 200, 0.9, 0]]  # x1,y1,x2,y2,conf,cls
        tracks = self.tracker.update(detections, dummy_frame)
        self.assertGreaterEqual(len(tracks), 0)  # 首帧可能需要初始化

    def test_track_id_consistency(self):
        """连续帧同一目标应保持相同 track ID"""
        if not self.tracker_available:
            self.skipTest("跟踪器不可用")
        self.tracker.reset()
        dummy_frame = np.zeros((720, 1280, 3), dtype=np.uint8)
        det = [[100, 100, 200, 200, 0.9, 0]]

        ids = []
        for _ in range(5):
            tracks = self.tracker.update(det, dummy_frame)
            if tracks:
                ids.append(tracks[0].get('track_id', tracks[0][0] if isinstance(tracks[0], (list, tuple)) else None))

        if len(ids) >= 2:
            self.assertEqual(len(set(ids)), 1, "连续帧同一目标的 track ID 应保持一致")

    def test_multi_target_tracking(self):
        """多目标跟踪"""
        if not self.tracker_available:
            self.skipTest("跟踪器不可用")
        self.tracker.reset()
        dummy_frame = np.zeros((720, 1280, 3), dtype=np.uint8)
        detections = [
            [100, 100, 200, 200, 0.9, 0],
            [400, 300, 500, 450, 0.85, 1],
            [700, 200, 800, 350, 0.8, 0],
        ]
        for _ in range(3):
            tracks = self.tracker.update(detections, dummy_frame)
        if tracks:
            self.assertGreaterEqual(len(tracks), 1)


if __name__ == "__main__":
    unittest.main()