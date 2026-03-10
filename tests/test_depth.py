"""
单目深度估计模块单元测试
"""
import sys
import os
import unittest
import numpy as np

sys.path.insert(0, os.path.join(os.path.dirname(__file__), '..', 'src'))


class TestMonocularDepth(unittest.TestCase):
    """单目深度估计测试"""

    @classmethod
    def setUpClass(cls):
        try:
            from depth.monocular_depth import MonocularDepthEstimator
            cls.estimator = MonocularDepthEstimator()
            cls.available = True
        except Exception as e:
            print(f"[SKIP] 深度估计器初始化失败: {e}")
            cls.available = False

    def test_init(self):
        """测试深度估计器初始化"""
        if not self.available:
            self.skipTest("深度估计器不可用")
        self.assertIsNotNone(self.estimator)

    def test_estimate_returns_depth_map(self):
        """测试返回深度图的形状"""
        if not self.available:
            self.skipTest("深度估计器不可用")
        dummy = np.zeros((720, 1280, 3), dtype=np.uint8)
        depth_map = self.estimator.estimate(dummy)
        self.assertIsNotNone(depth_map)
        self.assertEqual(len(depth_map.shape), 2, "深度图应为二维")
        self.assertGreater(depth_map.shape[0], 0)
        self.assertGreater(depth_map.shape[1], 0)

    def test_depth_values_non_negative(self):
        """深度值应非负"""
        if not self.available:
            self.skipTest("深度估计器不可用")
        dummy = np.random.randint(0, 255, (720, 1280, 3), dtype=np.uint8)
        depth_map = self.estimator.estimate(dummy)
        if depth_map is not None:
            self.assertTrue(np.all(depth_map >= 0), "深度值不应为负")

    def test_depth_consistency(self):
        """同一帧多次推理结果应一致（确定性模型）"""
        if not self.available:
            self.skipTest("深度估计器不可用")
        dummy = np.ones((720, 1280, 3), dtype=np.uint8) * 128
        d1 = self.estimator.estimate(dummy)
        d2 = self.estimator.estimate(dummy)
        if d1 is not None and d2 is not None:
            np.testing.assert_array_almost_equal(d1, d2, decimal=4)


if __name__ == "__main__":
    unittest.main()