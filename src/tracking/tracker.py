"""
多目标跟踪器 — ByteTrack 实现

轻量级、高性能的跟踪器，适合 A1 开发板的受限资源
"""


import numpy as np
from typing import List, Optional, Tuple, Dict
from dataclasses import dataclass, field
from collections import defaultdict

from src.detection.yolov8_detector import Detection
from src.utils.logger import logger

try:
    import cv2
except ImportError:
    cv2 = None


@dataclass
class Track:
    """单个被跟踪对象"""
    track_id: int  # 跟踪 ID
    bbox: Tuple[int, int, int, int]  # 边界框：x1, y1, x2, y2
    confidence: float  # 置信度
    class_id: int  # 类别 ID
    class_name: str = ""  # 类别名称
    age: int = 0  # 创建以来的帧数
    hits: int = 1  # 连续成功匹配次数
    time_since_update: int = 0  # 自更新以来的帧数
    velocity: Tuple[float, float] = (0.0, 0.0)  # 速度
    # 历史轨迹
    trajectory: List[Tuple[int, int]] = field(default_factory=list)  # 轨迹点列表

    @property
    def center(self) -> Tuple[int, int]:
        """获取边界框中心点坐标"""
        x1, y1, x2, y2 = self.bbox
        return ((x1 + x2) // 2, (y1 + y2) // 2)

    @property
    def is_confirmed(self) -> bool:
        """判断跟踪是否已确认"""
        return self.hits >= 3



class ByteTracker:
    """
    ByteTrack 多目标跟踪器

    两阶段关联:
    1. 使用 IoU 匹配高分数检测结果与跟踪
    2. 匹配剩余低分数检测结果与未匹配的跟踪
    """

    def __init__(self, config: dict):
        """初始化 ByteTracker"""
        self._cfg = config
        self._max_age = config.get("max_age", 30)  # 最大跟踪年龄
        self._min_hits = config.get("min_hits", 3)  # 最小匹配次数
        self._iou_threshold = config.get("iou_threshold", 0.3)  # IoU 阈值
        self._high_thresh = config.get("track_high_thresh", 0.5)  # 高分数阈值
        self._low_thresh = config.get("track_low_thresh", 0.1)  # 低分数阈值
        self._new_track_thresh = config.get("new_track_thresh", 0.6)  # 新跟踪阈值
        self._match_thresh = config.get("match_thresh", 0.8)  # 匹配阈值

        self._tracks: List[Track] = []  # 跟踪列表
        self._next_id = 1  # 下一个跟踪 ID
        self._frame_count = 0  # 帧计数

        logger.info(
            f"ByteTracker 初始化: max_age={self._max_age} "
            f"high={self._high_thresh} low={self._low_thresh}"
        )


    def update(self, detections: List[Detection]) -> List[Track]:
        """
        使用当前帧的检测结果更新跟踪器

        参数:
            detections: YOLOv8 生成的 Detection 对象列表

        返回:
            活跃的 Track 对象列表
        """

        self._frame_count += 1

        # Split detections by confidence
        high_dets = [d for d in detections if d.confidence >= self._high_thresh]
        low_dets = [
            d for d in detections
            if self._low_thresh <= d.confidence < self._high_thresh
        ]

        # Predict track positions (simple linear prediction)
        for track in self._tracks:
            track.age += 1
            track.time_since_update += 1
            if track.velocity != (0.0, 0.0):
                x1, y1, x2, y2 = track.bbox
                vx, vy = track.velocity
                track.bbox = (
                    int(x1 + vx), int(y1 + vy),
                    int(x2 + vx), int(y2 + vy),
                )

        # ── Stage 1: Match high-score detections ──
        confirmed_tracks = [t for t in self._tracks if t.is_confirmed]
        unconfirmed_tracks = [t for t in self._tracks if not t.is_confirmed]

        matched_t, matched_d, unmatched_tracks_1, unmatched_dets_1 = (
            self._associate(confirmed_tracks, high_dets, self._match_thresh)
        )

        # Update matched tracks
        for t_idx, d_idx in zip(matched_t, matched_d):
            self._update_track(confirmed_tracks[t_idx], high_dets[d_idx])

        # ── Stage 2: Match low-score detections with remaining tracks ──
        remaining_tracks = [confirmed_tracks[i] for i in unmatched_tracks_1]
        matched_t2, matched_d2, unmatched_tracks_2, _ = (
            self._associate(remaining_tracks, low_dets, 0.5)
        )

        for t_idx, d_idx in zip(matched_t2, matched_d2):
            self._update_track(remaining_tracks[t_idx], low_dets[d_idx])

        # ── Stage 3: Match unconfirmed tracks with remaining high dets ──
        remaining_high_dets = [high_dets[i] for i in unmatched_dets_1]
        matched_t3, matched_d3, _, unmatched_dets_3 = (
            self._associate(unconfirmed_tracks, remaining_high_dets, 0.7)
        )

        for t_idx, d_idx in zip(matched_t3, matched_d3):
            self._update_track(unconfirmed_tracks[t_idx], remaining_high_dets[d_idx])

        # ── Create new tracks from unmatched high-confidence detections ──
        for i in unmatched_dets_3:
            det = remaining_high_dets[i]
            if det.confidence >= self._new_track_thresh:
                self._create_track(det)

        # ── Remove dead tracks ──
        self._tracks = [
            t for t in self._tracks
            if t.time_since_update <= self._max_age
        ]

        # Return confirmed tracks
        active = [t for t in self._tracks if t.is_confirmed and t.time_since_update == 0]
        return active

    def _associate(
        self,
        tracks: List[Track],
        detections: List[Detection],
        thresh: float,
    ) -> Tuple[List[int], List[int], List[int], List[int]]:
        """
        使用 IoU 关联跟踪与检测结果

        返回:
            (匹配的跟踪索引, 匹配的检测索引,
             未匹配的跟踪索引, 未匹配的检测索引)
        """

        if len(tracks) == 0 or len(detections) == 0:
            return (
                [],
                [],
                list(range(len(tracks))),
                list(range(len(detections))),
            )

        # Compute IoU matrix
        t_boxes = np.array([t.bbox for t in tracks], dtype=float)
        d_boxes = np.array([d.bbox for d in detections], dtype=float)
        iou_matrix = self._batch_iou(t_boxes, d_boxes)

        # Greedy matching
        matched_t = []
        matched_d = []
        used_t = set()
        used_d = set()

        # Sort by IoU descending
        indices = np.dstack(np.unravel_index(
            np.argsort(-iou_matrix, axis=None), iou_matrix.shape
        ))[0]

        for t_idx, d_idx in indices:
            if t_idx in used_t or d_idx in used_d:
                continue
            if iou_matrix[t_idx, d_idx] < thresh:
                break
            matched_t.append(t_idx)
            matched_d.append(d_idx)
            used_t.add(t_idx)
            used_d.add(d_idx)

        unmatched_t = [i for i in range(len(tracks)) if i not in used_t]
        unmatched_d = [i for i in range(len(detections)) if i not in used_d]

        return matched_t, matched_d, unmatched_t, unmatched_d

    @staticmethod
    def _batch_iou(boxes_a: np.ndarray, boxes_b: np.ndarray) -> np.ndarray:
        """计算两组边界框之间的 IoU。形状: [M,4] x [N,4] → [M,N]"""

        x1 = np.maximum(boxes_a[:, 0:1], boxes_b[:, 0:1].T)
        y1 = np.maximum(boxes_a[:, 1:2], boxes_b[:, 1:2].T)
        x2 = np.minimum(boxes_a[:, 2:3], boxes_b[:, 2:3].T)
        y2 = np.minimum(boxes_a[:, 3:4], boxes_b[:, 3:4].T)

        inter = np.maximum(0, x2 - x1) * np.maximum(0, y2 - y1)
        area_a = (boxes_a[:, 2] - boxes_a[:, 0]) * (boxes_a[:, 3] - boxes_a[:, 1])
        area_b = (boxes_b[:, 2] - boxes_b[:, 0]) * (boxes_b[:, 3] - boxes_b[:, 1])

        union = area_a[:, None] + area_b[None, :] - inter
        return inter / (union + 1e-6)

    def _update_track(self, track: Track, det: Detection):
        """使用匹配的检测结果更新跟踪"""

        old_center = track.center
        track.bbox = det.bbox
        track.confidence = det.confidence
        track.class_id = det.class_id
        track.class_name = det.class_name
        track.hits += 1
        track.time_since_update = 0

        # Update velocity
        new_center = track.center
        track.velocity = (
            new_center[0] - old_center[0],
            new_center[1] - old_center[1],
        )

        # Update trajectory
        track.trajectory.append(new_center)
        if len(track.trajectory) > 100:
            track.trajectory = track.trajectory[-100:]

    def _create_track(self, det: Detection):
        """创建新的跟踪"""
        track = Track(
            track_id=self._next_id,
            bbox=det.bbox,
            confidence=det.confidence,
            class_id=det.class_id,
            class_name=det.class_name,
        )
        track.trajectory.append(track.center)
        self._tracks.append(track)
        self._next_id += 1

    # ── Visualization ─────────────────────────────────────

    def draw_tracks(self, frame: np.ndarray, tracks: List[Track]) -> np.ndarray:
        """绘制带有 ID 和轨迹的跟踪对象"""
        if cv2 is None:
            return frame

        vis = frame.copy()
        for track in tracks:
            x1, y1, x2, y2 = track.bbox
            color = self._id_color(track.track_id)

            # Box
            cv2.rectangle(vis, (x1, y1), (x2, y2), color, 2)

            # Label
            label = f"ID:{track.track_id} {track.class_name} {track.confidence:.2f}"
            cv2.putText(vis, label, (x1, y1 - 8), cv2.FONT_HERSHEY_SIMPLEX, 0.5, color, 2)

            # Trajectory
            if len(track.trajectory) > 1:
                pts = np.array(track.trajectory, dtype=np.int32)
                cv2.polylines(vis, [pts], False, color, 2)

        return vis

    @staticmethod
    def _id_color(track_id: int) -> Tuple[int, int, int]:
        """根据跟踪 ID 生成一致的颜色"""
        np.random.seed(track_id * 7 + 13)
        return tuple(int(c) for c in np.random.randint(80, 255, 3))

    def reset(self):
        """重置跟踪器状态"""
        self._tracks.clear()
        self._next_id = 1
        self._frame_count = 0
        logger.info("跟踪器已重置")

    def get_track_by_id(self, track_id: int) -> Optional[Track]:
        """根据 ID 获取跟踪对象"""
        for t in self._tracks:
            if t.track_id == track_id:
                return t
        return None
