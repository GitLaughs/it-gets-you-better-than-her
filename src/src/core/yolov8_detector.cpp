#include "yolov8_detector.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>

// Optional: ONNX Runtime
#ifdef USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

static const char* MOD = "Detector";

// COCO 80 classes
static const char* COCO_CLASSES[] = {
    "person","bicycle","car","motorcycle","airplane","bus","train","truck",
    "boat","traffic light","fire hydrant","stop sign","parking meter","bench",
    "bird","cat","dog","horse","sheep","cow","elephant","bear","zebra",
    "giraffe","backpack","umbrella","handbag","tie","suitcase","frisbee",
    "skis","snowboard","sports ball","kite","baseball bat","baseball glove",
    "skateboard","surfboard","tennis racket","bottle","wine glass","cup",
    "fork","knife","spoon","bowl","banana","apple","sandwich","orange",
    "broccoli","carrot","hot dog","pizza","donut","cake","chair","couch",
    "potted plant","bed","dining table","toilet","tv","laptop","mouse",
    "remote","keyboard","cell phone","microwave","oven","toaster","sink",
    "refrigerator","book","clock","vase","scissors","teddy bear",
    "hair drier","toothbrush"
};
static const int NUM_COCO_CLASSES = 80;

struct YOLOv8Detector::Impl {
#ifdef USE_ONNXRUNTIME
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "yolov8"};
    Ort::SessionOptions sessionOpts;
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::vector<std::string> outputNames;
    std::vector<int64_t> inputShape;
    int numClasses = 80;
#endif
    bool loaded = false;
};

YOLOv8Detector::YOLOv8Detector() : impl_(std::make_unique<Impl>()) {}

YOLOv8Detector::~YOLOv8Detector() { release(); }

bool YOLOv8Detector::initialize(const std::string& modelPath,
                                 int inputW, int inputH,
                                 float confThresh, float iouThresh,
                                 int maxDet, int numThreads) {
    inputW_ = inputW;
    inputH_ = inputH;
    confThresh_ = confThresh;
    iouThresh_ = iouThresh;
    maxDet_ = maxDet;

    inputBlob_.resize(3 * inputW * inputH);

    LOG_I(MOD, "Loading model: %s (%dx%d, conf=%.2f, iou=%.2f, threads=%d)",
          modelPath.c_str(), inputW, inputH, confThresh, iouThresh, numThreads);

#ifdef USE_ONNXRUNTIME
    try {
        impl_->sessionOpts.SetIntraOpNumThreads(numThreads);
        impl_->sessionOpts.SetGraphOptimizationLevel(
            GraphOptimizationLevel::ORT_ENABLE_ALL);

        impl_->session = std::make_unique<Ort::Session>(
            impl_->env, modelPath.c_str(), impl_->sessionOpts);

        Ort::AllocatorWithDefaultOptions alloc;

        // Input info
        auto inputNamePtr = impl_->session->GetInputNameAllocated(0, alloc);
        impl_->inputName = inputNamePtr.get();

        auto inputInfo = impl_->session->GetInputTypeInfo(0);
        auto tensorInfo = inputInfo.GetTensorTypeAndShapeInfo();
        impl_->inputShape = tensorInfo.GetShape();

        // Output info
        size_t numOutputs = impl_->session->GetOutputCount();
        for (size_t i = 0; i < numOutputs; ++i) {
            auto name = impl_->session->GetOutputNameAllocated(i, alloc);
            impl_->outputNames.push_back(name.get());
        }

        // Determine number of classes from output shape
        auto outInfo = impl_->session->GetOutputTypeInfo(0);
        auto outShape = outInfo.GetTensorTypeAndShapeInfo().GetShape();
        if (outShape.size() >= 2) {
            // [1, 4+nc, N] or [1, N, 4+nc]
            int dim1 = (int)outShape[1];
            int dim2 = outShape.size() > 2 ? (int)outShape[2] : 0;
            impl_->numClasses = (dim1 > dim2 ? dim1 : dim2) - 4;
            if (impl_->numClasses <= 0 || impl_->numClasses > 1000)
                impl_->numClasses = 80;
        }

        impl_->loaded = true;
        ready_ = true;
        LOG_I(MOD, "Model loaded: input=%s shape=[%ld,%ld,%ld,%ld] classes=%d",
              impl_->inputName.c_str(),
              impl_->inputShape.size() > 0 ? impl_->inputShape[0] : 0,
              impl_->inputShape.size() > 1 ? impl_->inputShape[1] : 0,
              impl_->inputShape.size() > 2 ? impl_->inputShape[2] : 0,
              impl_->inputShape.size() > 3 ? impl_->inputShape[3] : 0,
              impl_->numClasses);
        return true;

    } catch (const std::exception& e) {
        LOG_E(MOD, "ONNX Runtime init failed: %s", e.what());
        ExceptionHandler::instance().handle("OnnxInitError", e.what(), "detection");
        return false;
    }
#else
    LOG_W(MOD, "ONNX Runtime not available, detector will return empty results");
    ready_ = false;
    return false;
#endif
}

void YOLOv8Detector::preprocess(const uint8_t* frame, int width, int height,
                                 float* output, float& ratio,
                                 float& dw, float& dh) {
    // Letterbox resize from grayscale to RGB 320x320
    float scaleW = (float)inputW_ / width;
    float scaleH = (float)inputH_ / height;
    ratio = std::min(scaleW, scaleH);

    int newW = (int)(width * ratio);
    int newH = (int)(height * ratio);
    dw = (inputW_ - newW) / 2.0f;
    dh = (inputH_ - newH) / 2.0f;

    int idw = (int)dw;
    int idh = (int)dh;

    // Fill with 114/255 = 0.447
    int total = 3 * inputW_ * inputH_;
    float padVal = 114.0f / 255.0f;
    for (int i = 0; i < total; ++i) output[i] = padVal;

    // Resize with bilinear interpolation and convert GREY to 3-channel
    for (int y = 0; y < newH; ++y) {
        float srcY = (float)y / ratio;
        int sy0 = (int)srcY;
        int sy1 = std::min(sy0 + 1, height - 1);
        float fy = srcY - sy0;

        int dstY = y + idh;
        if (dstY >= inputH_) continue;

        for (int x = 0; x < newW; ++x) {
            float srcX = (float)x / ratio;
            int sx0 = (int)srcX;
            int sx1 = std::min(sx0 + 1, width - 1);
            float fx = srcX - sx0;

            // Bilinear interpolation
            float val = (1.0f - fy) * ((1.0f - fx) * frame[sy0 * width + sx0] +
                                        fx * frame[sy0 * width + sx1]) +
                        fy * ((1.0f - fx) * frame[sy1 * width + sx0] +
                              fx * frame[sy1 * width + sx1]);

            float normalized = val / 255.0f;
            int dstX = x + idw;
            if (dstX >= inputW_) continue;

            // NCHW format: 3 channels (grayscale replicated)
            int pixIdx = dstY * inputW_ + dstX;
            output[0 * inputW_ * inputH_ + pixIdx] = normalized;  // R
            output[1 * inputW_ * inputH_ + pixIdx] = normalized;  // G
            output[2 * inputW_ * inputH_ + pixIdx] = normalized;  // B
        }
    }
}

std::vector<Detection> YOLOv8Detector::detect(const uint8_t* frame,
                                                int width, int height) {
    if (!ready_) return {};

    auto t0 = std::chrono::steady_clock::now();

    float ratio, dw, dh;
    preprocess(frame, width, height, inputBlob_.data(), ratio, dw, dh);

    std::vector<Detection> results;

#ifdef USE_ONNXRUNTIME
    try {
        std::vector<int64_t> inputShape = {1, 3, inputH_, inputW_};

        Ort::MemoryInfo memInfo = Ort::MemoryInfo::CreateCpu(
            OrtArenaAllocator, OrtMemTypeDefault);

        Ort::Value inputTensor = Ort::Value::CreateTensor<float>(
            memInfo, inputBlob_.data(), inputBlob_.size(),
            inputShape.data(), inputShape.size());

        const char* inputNames[] = {impl_->inputName.c_str()};
        std::vector<const char*> outputNames;
        for (auto& n : impl_->outputNames) outputNames.push_back(n.c_str());

        auto outputs = impl_->session->Run(
            Ort::RunOptions{nullptr},
            inputNames, &inputTensor, 1,
            outputNames.data(), outputNames.size());

        // Parse output
        auto& outTensor = outputs[0];
        auto outShape = outTensor.GetTensorTypeAndShapeInfo().GetShape();
        const float* outData = outTensor.GetTensorData<float>();

        int dim1 = (int)outShape[1];
        int dim2 = outShape.size() > 2 ? (int)outShape[2] : 1;

        // YOLOv8 output: [1, 4+nc, N] -> transpose if needed
        int numAnchors, numValues;
        const float* predData = outData;
        std::vector<float> transposed;

        if (dim1 < dim2) {
            // [1, 4+nc, N] -> transpose to [N, 4+nc]
            numAnchors = dim2;
            numValues = dim1;
            transposed.resize(dim1 * dim2);
            for (int i = 0; i < dim1; ++i) {
                for (int j = 0; j < dim2; ++j) {
                    transposed[j * dim1 + i] = outData[i * dim2 + j];
                }
            }
            predData = transposed.data();
        } else {
            numAnchors = dim1;
            numValues = dim2;
        }

        int nc = numValues - 4;
        results = postprocess(predData, numAnchors, nc,
                              width, height, ratio, dw, dh);

    } catch (const std::exception& e) {
        LOG_E(MOD, "Inference error: %s", e.what());
        ExceptionHandler::instance().handleException(e, "detection");
    }
#endif

    auto t1 = std::chrono::steady_clock::now();
    lastInferMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();

    return results;
}

std::vector<Detection> YOLOv8Detector::postprocess(
    const float* data, int numAnchors, int numClasses,
    int origW, int origH, float ratio, float dw, float dh) {

    std::vector<Detection> dets;

    for (int i = 0; i < numAnchors; ++i) {
        const float* row = data + i * (4 + numClasses);

        float cx = row[0], cy = row[1], w = row[2], h = row[3];

        // Find best class
        int bestClass = 0;
        float bestScore = row[4];
        for (int c = 1; c < numClasses; ++c) {
            if (row[4 + c] > bestScore) {
                bestScore = row[4 + c];
                bestClass = c;
            }
        }

        if (bestScore < confThresh_) continue;

        // Convert cx,cy,w,h to x1,y1,x2,y2
        Detection det;
        det.x1 = (cx - w * 0.5f - dw) / ratio;
        det.y1 = (cy - h * 0.5f - dh) / ratio;
        det.x2 = (cx + w * 0.5f - dw) / ratio;
        det.y2 = (cy + h * 0.5f - dh) / ratio;

        // Clip
        det.x1 = std::max(0.0f, std::min(det.x1, (float)origW));
        det.y1 = std::max(0.0f, std::min(det.y1, (float)origH));
        det.x2 = std::max(0.0f, std::min(det.x2, (float)origW));
        det.y2 = std::max(0.0f, std::min(det.y2, (float)origH));

        det.confidence = bestScore;
        det.classId = bestClass;
        strncpy(det.className, className(bestClass), sizeof(det.className) - 1);

        dets.push_back(det);
    }

    // NMS
    auto keep = nms(dets, iouThresh_);
    std::vector<Detection> result;
    int limit = std::min((int)keep.size(), maxDet_);
    for (int i = 0; i < limit; ++i) {
        result.push_back(dets[keep[i]]);
    }

    return result;
}

float YOLOv8Detector::iou(const Detection& a, const Detection& b) {
    float xx1 = std::max(a.x1, b.x1);
    float yy1 = std::max(a.y1, b.y1);
    float xx2 = std::min(a.x2, b.x2);
    float yy2 = std::min(a.y2, b.y2);

    float inter = std::max(0.0f, xx2 - xx1) * std::max(0.0f, yy2 - yy1);
    float areaA = (a.x2 - a.x1) * (a.y2 - a.y1);
    float areaB = (b.x2 - b.x1) * (b.y2 - b.y1);

    return inter / (areaA + areaB - inter + 1e-6f);
}

std::vector<int> YOLOv8Detector::nms(const std::vector<Detection>& dets,
                                      float iouThreshold) {
    if (dets.empty()) return {};

    // Sort by confidence
    std::vector<int> indices(dets.size());
    for (int i = 0; i < (int)dets.size(); ++i) indices[i] = i;
    std::sort(indices.begin(), indices.end(),
              [&](int a, int b) { return dets[a].confidence > dets[b].confidence; });

    std::vector<int> keep;
    std::vector<bool> suppressed(dets.size(), false);

    for (int idx : indices) {
        if (suppressed[idx]) continue;
        keep.push_back(idx);

        for (int j : indices) {
            if (suppressed[j] || j == idx) continue;
            if (iou(dets[idx], dets[j]) > iouThreshold) {
                suppressed[j] = true;
            }
        }
    }

    return keep;
}

const char* YOLOv8Detector::className(int id) {
    if (id >= 0 && id < NUM_COCO_CLASSES) return COCO_CLASSES[id];
    return "unknown";
}

void YOLOv8Detector::release() {
#ifdef USE_ONNXRUNTIME
    if (impl_) {
        impl_->session.reset();
        impl_->loaded = false;
    }
#endif
    ready_ = false;
    LOG_I(MOD, "Detector released");
}