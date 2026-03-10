"""
YOLOv8 目标检测器

支持:
  - ONNX Runtime 推理（跨平台）
  - A1 NPU 推理（通过 SmartSens SDK）
  - Letterbox 预处理，NMS 后处理
"""


import time
import numpy as np
from typing import List, Optional, Tuple
from dataclasses import dataclass, field

from src.utils.logger import logger

try:
    import cv2
except ImportError:
    cv2 = None

try:
    import onnxruntime as ort
    _ORT_AVAILABLE = True
except ImportError:
    _ORT_AVAILABLE = False


@dataclass
class Detection:
    """单个检测结果"""
    bbox: Tuple[int, int, int, int]  # 边界框：x1, y1, x2, y2
    confidence: float  # 置信度
    class_id: int  # 类别ID
    class_name: str = ""  # 类别名称

    @property
    def center(self) -> Tuple[int, int]:
        """获取边界框中心点坐标"""
        x1, y1, x2, y2 = self.bbox
        return ((x1 + x2) // 2, (y1 + y2) // 2)

    @property
    def area(self) -> int:
        """计算边界框面积"""
        x1, y1, x2, y2 = self.bbox
        return max(0, x2 - x1) * max(0, y2 - y1)

    @property
    def width_height(self) -> Tuple[int, int]:
        """获取边界框宽高"""
        x1, y1, x2, y2 = self.bbox
        return (x2 - x1, y2 - y1)



# COCO class names (80 classes)
COCO_CLASSES = [
    "person", "bicycle", "car", "motorcycle", "airplane", "bus", "train", "truck",
    "boat", "traffic light", "fire hydrant", "stop sign", "parking meter", "bench",
    "bird", "cat", "dog", "horse", "sheep", "cow", "elephant", "bear", "zebra",
    "giraffe", "backpack", "umbrella", "handbag", "tie", "suitcase", "frisbee",
    "skis", "snowboard", "sports ball", "kite", "baseball bat", "baseball glove",
    "skateboard", "surfboard", "tennis racket", "bottle", "wine glass", "cup",
    "fork", "knife", "spoon", "bowl", "banana", "apple", "sandwich", "orange",
    "broccoli", "carrot", "hot dog", "pizza", "donut", "cake", "chair", "couch",
    "potted plant", "bed", "dining table", "toilet", "tv", "laptop", "mouse",
    "remote", "keyboard", "cell phone", "microwave", "oven", "toaster", "sink",
    "refrigerator", "book", "clock", "vase", "scissors", "teddy bear",
    "hair drier", "toothbrush",
]


class YOLOv8Detector:
    """
    YOLOv8 推理引擎，支持 ONNX/NPU 后端
    """

    def __init__(self, config: dict):
        """初始化 YOLOv8 检测器"""
        self._cfg = config
        self._model_path = config.get("model_path", "models/yolov8n.onnx")  # 模型路径
        self._model_type = config.get("model_type", "onnx")  # 模型类型
        self._input_size = tuple(config.get("input_size", [320, 320]))  # 输入尺寸
        self._conf_thresh = config.get("conf_threshold", 0.45)  # 置信度阈值
        self._iou_thresh = config.get("iou_threshold", 0.5)  # IoU 阈值
        self._max_det = config.get("max_detections", 50)  # 最大检测数量
        self._classes = config.get("classes", None)  # 目标类别
        self._device = config.get("device", "cpu")  # 运行设备

        self._session = None  # 推理会话
        self._input_name = None  # 输入名称
        self._output_names = None  # 输出名称

        logger.info(
            f"YOLOv8Detector: model={self._model_path} "
            f"input={self._input_size} device={self._device}"
        )


    def initialize(self) -> bool:
        """加载模型并创建推理会话"""
        try:
            if self._model_type == "onnx":
                return self._init_onnx()
            elif self._model_type == "sdk_npu":
                return self._init_npu()
            else:
                logger.error(f"未知模型类型: {self._model_type}")
                return False
        except Exception as e:
            logger.error(f"模型初始化失败: {e}")
            return False


    def _init_onnx(self) -> bool:
        """初始化 ONNX Runtime 会话"""
        if not _ORT_AVAILABLE:
            logger.error("onnxruntime 未安装")
            return False

        providers = ["CPUExecutionProvider"]
        if self._device == "npu":
            # A1 NPU 可能需要自定义执行提供程序
            logger.info("请求 NPU，使用 ONNX 的 CPU 回退")

        self._session = ort.InferenceSession(self._model_path, providers=providers)
        self._input_name = self._session.get_inputs()[0].name
        self._output_names = [o.name for o in self._session.get_outputs()]

        logger.info(
            f"ONNX 模型已加载: input={self._input_name} "
            f"outputs={self._output_names}"
        )
        return True


    def _init_npu(self) -> bool:
        """初始化 A1 NPU 推理"""
        npu_cfg = self._cfg.get("npu_config", {})
        model_bin = npu_cfg.get("model_bin", "")
        model_param = npu_cfg.get("model_param", "")

        logger.info(f"初始化 NPU: bin={model_bin} param={model_param}")

        # SmartSens NPU API 占位符:
        # self._npu_net = smartsens_sdk.npu.load_model(model_bin, model_param)
        # self._npu_net.set_input_size(*self._input_size)

        logger.warning("NPU 初始化是占位符 - 使用 ONNX 回退")
        return self._init_onnx()


    # ── 推理 ─────────────────────────────────────────

    def detect(self, frame: np.ndarray) -> List[Detection]:
        """
        在帧上运行 YOLOv8 检测

        参数:
            frame: BGR 或灰度图像

        返回:
            Detection 对象列表
        """

        if self._session is None:
            logger.warning("Detector not initialized")
            return []

        t0 = time.time()

        # Preprocess
        input_tensor, ratio, (dw, dh) = self._preprocess(frame)

        # Inference
        outputs = self._session.run(self._output_names, {self._input_name: input_tensor})

        # Postprocess
        detections = self._postprocess(outputs, frame.shape, ratio, dw, dh)

        elapsed = (time.time() - t0) * 1000
        logger.debug(f"Detection: {len(detections)} objects in {elapsed:.1f}ms")

        return detections

    def _preprocess(self, frame: np.ndarray) -> Tuple[np.ndarray, float, Tuple[int, int]]:
        """
        Letterbox 调整大小 + 归一化

        返回:
            (input_tensor, ratio, (pad_w, pad_h))
        """

        if len(frame.shape) == 2:
            frame = cv2.cvtColor(frame, cv2.COLOR_GRAY2BGR)

        img_h, img_w = frame.shape[:2]
        inp_w, inp_h = self._input_size

        # Scale ratio
        ratio = min(inp_w / img_w, inp_h / img_h)
        new_w, new_h = int(img_w * ratio), int(img_h * ratio)
        dw, dh = (inp_w - new_w) // 2, (inp_h - new_h) // 2

        # Resize
        resized = cv2.resize(frame, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

        # Pad
        padded = np.full((inp_h, inp_w, 3), 114, dtype=np.uint8)
        padded[dh:dh + new_h, dw:dw + new_w] = resized

        # Normalize: HWC → CHW, /255, float32, add batch dim
        blob = padded.astype(np.float32) / 255.0
        blob = blob.transpose(2, 0, 1)  # CHW
        blob = np.expand_dims(blob, axis=0)  # NCHW

        return blob, ratio, (dw, dh)

    def _postprocess(
        self,
        outputs: list,
        orig_shape: tuple,
        ratio: float,
        dw: int,
        dh: int,
    ) -> List[Detection]:
        """
        后处理 YOLOv8 输出: 解码边界框，应用 NMS

        YOLOv8 输出形状: [1, num_classes+4, num_anchors]
        """

        predictions = outputs[0]  # [1, 4+nc, N]

        # Transpose to [N, 4+nc]
        if predictions.shape[1] < predictions.shape[2]:
            predictions = predictions.transpose(0, 2, 1)  # [1, N, 4+nc]
        predictions = predictions[0]  # [N, 4+nc]

        # Extract boxes (cx, cy, w, h) and class scores
        boxes_cxcywh = predictions[:, :4]
        scores_all = predictions[:, 4:]

        # Get max class score and class id per box
        class_ids = np.argmax(scores_all, axis=1)
        confidences = scores_all[np.arange(len(class_ids)), class_ids]

        # Filter by confidence
        mask = confidences > self._conf_thresh
        if self._classes is not None:
            class_mask = np.isin(class_ids, self._classes)
            mask = mask & class_mask

        boxes_cxcywh = boxes_cxcywh[mask]
        confidences = confidences[mask]
        class_ids = class_ids[mask]

        if len(boxes_cxcywh) == 0:
            return []

        # Convert cx,cy,w,h → x1,y1,x2,y2
        boxes_xyxy = np.zeros_like(boxes_cxcywh)
        boxes_xyxy[:, 0] = boxes_cxcywh[:, 0] - boxes_cxcywh[:, 2] / 2  # x1
        boxes_xyxy[:, 1] = boxes_cxcywh[:, 1] - boxes_cxcywh[:, 3] / 2  # y1
        boxes_xyxy[:, 2] = boxes_cxcywh[:, 0] + boxes_cxcywh[:, 2] / 2  # x2
        boxes_xyxy[:, 3] = boxes_cxcywh[:, 1] + boxes_cxcywh[:, 3] / 2  # y2

        # Remove padding and rescale to original image
        boxes_xyxy[:, [0, 2]] = (boxes_xyxy[:, [0, 2]] - dw) / ratio
        boxes_xyxy[:, [1, 3]] = (boxes_xyxy[:, [1, 3]] - dh) / ratio

        # Clip to image bounds
        orig_h, orig_w = orig_shape[:2]
        boxes_xyxy[:, [0, 2]] = np.clip(boxes_xyxy[:, [0, 2]], 0, orig_w)
        boxes_xyxy[:, [1, 3]] = np.clip(boxes_xyxy[:, [1, 3]], 0, orig_h)

        # NMS
        indices = self._nms(boxes_xyxy, confidences, self._iou_thresh)
        indices = indices[:self._max_det]

        # Build Detection objects
        detections = []
        for i in indices:
            x1, y1, x2, y2 = boxes_xyxy[i].astype(int)
            cid = int(class_ids[i])
            cname = COCO_CLASSES[cid] if cid < len(COCO_CLASSES) else str(cid)
            detections.append(Detection(
                bbox=(x1, y1, x2, y2),
                confidence=float(confidences[i]),
                class_id=cid,
                class_name=cname,
            ))

        return detections

    @staticmethod
    def _nms(boxes: np.ndarray, scores: np.ndarray, iou_threshold: float) -> List[int]:
        """非极大值抑制 (纯 numpy 实现)"""

        if len(boxes) == 0:
            return []

        x1 = boxes[:, 0]
        y1 = boxes[:, 1]
        x2 = boxes[:, 2]
        y2 = boxes[:, 3]
        areas = (x2 - x1) * (y2 - y1)

        order = scores.argsort()[::-1]
        keep = []

        while len(order) > 0:
            i = order[0]
            keep.append(i)

            if len(order) == 1:
                break

            xx1 = np.maximum(x1[i], x1[order[1:]])
            yy1 = np.maximum(y1[i], y1[order[1:]])
            xx2 = np.minimum(x2[i], x2[order[1:]])
            yy2 = np.minimum(y2[i], y2[order[1:]])

            inter = np.maximum(0, xx2 - xx1) * np.maximum(0, yy2 - yy1)
            iou = inter / (areas[i] + areas[order[1:]] - inter + 1e-6)

            remain = np.where(iou <= iou_threshold)[0]
            order = order[remain + 1]

        return keep

    # ── Utils ─────────────────────────────────────────────

    def draw_detections(
        self, frame: np.ndarray, detections: List[Detection]
    ) -> np.ndarray:
        """在帧上绘制边界框和标签"""

        if cv2 is None:
            return frame

        vis = frame.copy()
        for det in detections:
            x1, y1, x2, y2 = det.bbox
            color = self._class_color(det.class_id)
            cv2.rectangle(vis, (x1, y1), (x2, y2), color, 2)
            label = f"{det.class_name} {det.confidence:.2f}"
            (tw, th), _ = cv2.getTextSize(label, cv2.FONT_HERSHEY_SIMPLEX, 0.5, 1)
            cv2.rectangle(vis, (x1, y1 - th - 6), (x1 + tw, y1), color, -1)
            cv2.putText(
                vis, label, (x1, y1 - 4),
                cv2.FONT_HERSHEY_SIMPLEX, 0.5, (255, 255, 255), 1,
            )
        return vis

    @staticmethod
    def _class_color(class_id: int) -> Tuple[int, int, int]:
        """为类别生成一致的颜色"""

        np.random.seed(class_id + 42)
        return tuple(int(c) for c in np.random.randint(50, 255, 3))

    def release(self):
        """释放模型资源"""
        self._session = None
        logger.info("YOLOv8 检测器已释放")
