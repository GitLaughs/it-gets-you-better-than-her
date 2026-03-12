#include "depth_estimator.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cmath>
#include <numeric>

#ifdef USE_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#endif

static const char* MOD = "Depth";

struct DepthEstimator::Impl {
#ifdef USE_ONNXRUNTIME
    Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "depth"};
    Ort::SessionOptions sessionOpts;
    std::unique_ptr<Ort::Session> session;
    std::string inputName;
    std::vector<std::string> outputNames;
    std::vector<int64_t> inputShape;
    std::vector<int64_t> outputShape;
#endif
    bool loaded = false;
};

DepthEstimator::DepthEstimator() : impl_(std::make_unique<Impl>()) {}

DepthEstimator::~DepthEstimator() { release(); }

bool DepthEstimator::initialize(const std::string& modelPath, int inputW,
                                 int inputH, float minDepth, float maxDepth,
                                 int numThreads) {
    inputW_ = inputW;
    inputH_ = inputH;
    minDepth_ = minDepth;
    maxDepth_ = maxDepth;

    inputBlob_.resize(3 * inputW * inputH);

    LOG_I(MOD, "Initializing depth estimator: %s (%dx%d, depth=[%.1f,%.1f], threads=%d)",
          modelPath.c_str(), inputW, inputH, minDepth, maxDepth, numThreads);

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

        auto outInfo = impl_->session->GetOutputTypeInfo(0);
        impl_->outputShape = outInfo.GetTensorTypeAndShapeInfo().GetShape();

        impl_->loaded = true;
        ready_ = true;
        useFallback_ = false;

        LOG_I(MOD, "Depth model loaded: input=%s, output_dims=%d",
              impl_->inputName.c_str(), (int)impl_->outputShape.size());
        return true;

    } catch (const std::exception& e) {
        LOG_W(MOD, "ONNX depth model load failed: %s, using fallback", e.what());
        useFallback_ = true;
        ready_ = true;
        return true;
    }
#else
    LOG_W(MOD, "ONNX Runtime not available, using brightness-based fallback depth");
    useFallback_ = true;
    ready_ = true;
    return true;
#endif
}

void DepthEstimator::preprocess(const uint8_t* frame, int width, int height,
                                 float* output) {
    // Resize grayscale to inputW_ x inputH_ with bilinear interpolation
    // Convert to 3-channel normalized [0,1] NCHW
    float scaleX = (float)width / inputW_;
    float scaleY = (float)height / inputH_;

    for (int y = 0; y < inputH_; ++y) {
        float srcY = y * scaleY;
        int sy0 = (int)srcY;
        int sy1 = std::min(sy0 + 1, height - 1);
        float fy = srcY - sy0;

        for (int x = 0; x < inputW_; ++x) {
            float srcX = x * scaleX;
            int sx0 = (int)srcX;
            int sx1 = std::min(sx0 + 1, width - 1);
            float fx = srcX - sx0;

            float val = (1.0f - fy) * ((1.0f - fx) * frame[sy0 * width + sx0] +
                                        fx * frame[sy0 * width + sx1]) +
                        fy * ((1.0f - fx) * frame[sy1 * width + sx0] +
                              fx * frame[sy1 * width + sx1]);

            float normalized = val / 255.0f;
            int pixIdx = y * inputW_ + x;
            output[0 * inputW_ * inputH_ + pixIdx] = normalized;
            output[1 * inputW_ * inputH_ + pixIdx] = normalized;
            output[2 * inputW_ * inputH_ + pixIdx] = normalized;
        }
    }
}

void DepthEstimator::postprocessDepth(const float* rawOut, int rawW, int rawH,
                                       int origW, int origH,
                                       std::vector<float>& depthMap) {
    depthMap.resize(origW * origH);

    float scaleX = (float)rawW / origW;
    float scaleY = (float)rawH / origH;

    // Find min/max of raw output for normalization
    float rawMin = rawOut[0], rawMax = rawOut[0];
    int rawSize = rawW * rawH;
    for (int i = 1; i < rawSize; ++i) {
        if (rawOut[i] < rawMin) rawMin = rawOut[i];
        if (rawOut[i] > rawMax) rawMax = rawOut[i];
    }
    float rawRange = rawMax - rawMin;
    if (rawRange < 1e-6f) rawRange = 1.0f;

    // Resize raw depth to original resolution and map to [minDepth_, maxDepth_]
    for (int y = 0; y < origH; ++y) {
        float srcY = y * scaleY;
        int sy = std::min((int)srcY, rawH - 1);

        for (int x = 0; x < origW; ++x) {
            float srcX = x * scaleX;
            int sx = std::min((int)srcX, rawW - 1);

            float rawVal = rawOut[sy * rawW + sx];
            // Normalize to [0,1] then map to depth range
            float norm = (rawVal - rawMin) / rawRange;
            // Invert: higher raw value typically means closer (MiDaS convention)
            float depth = minDepth_ + (1.0f - norm) * (maxDepth_ - minDepth_);
            depth = std::max(minDepth_, std::min(maxDepth_, depth));
            depthMap[y * origW + x] = depth;
        }
    }
}

void DepthEstimator::fallbackDepth(const uint8_t* frame, int width, int height,
                                    std::vector<float>& depthMap) {
    // Heuristic depth from brightness + gradient
    // Brighter regions assumed closer for well-lit scenes
    // Uses local contrast as additional cue
    depthMap.resize(width * height);

    // First pass: brightness-based rough depth
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int idx = y * width + x;
            float brightness = frame[idx] / 255.0f;

            // Vertical position heuristic: lower in image = closer (ground plane)
            float verticalFactor = 1.0f - 0.3f * ((float)y / height);

            // Brightness heuristic: moderate brightness = closer
            float depthFromBright;
            if (brightness < 0.1f) {
                depthFromBright = maxDepth_;  // very dark = unknown/far
            } else {
                depthFromBright = maxDepth_ - brightness * (maxDepth_ - minDepth_) * 0.6f;
            }

            depthMap[idx] = depthFromBright * verticalFactor;
            depthMap[idx] = std::max(minDepth_, std::min(maxDepth_, depthMap[idx]));
        }
    }

    // Second pass: local contrast enhancement (edges = object boundaries)
    // Simple 3x3 Sobel gradient magnitude
    std::vector<float> gradient(width * height, 0.0f);
    for (int y = 1; y < height - 1; ++y) {
        for (int x = 1; x < width - 1; ++x) {
            float gx = -frame[(y-1)*width+(x-1)] + frame[(y-1)*width+(x+1)]
                       -2*frame[y*width+(x-1)] + 2*frame[y*width+(x+1)]
                       -frame[(y+1)*width+(x-1)] + frame[(y+1)*width+(x+1)];
            float gy = -frame[(y-1)*width+(x-1)] - 2*frame[(y-1)*width+x] - frame[(y-1)*width+(x+1)]
                       +frame[(y+1)*width+(x-1)] + 2*frame[(y+1)*width+x] + frame[(y+1)*width+(x+1)];
            gradient[y * width + x] = std::sqrt(gx*gx + gy*gy) / 1442.0f; // normalize
        }
    }

    // Blend: high gradient areas get slightly closer depth (object edges)
    for (int i = 0; i < width * height; ++i) {
        if (gradient[i] > 0.1f) {
            depthMap[i] *= (1.0f - 0.2f * gradient[i]);
            depthMap[i] = std::max(minDepth_, depthMap[i]);
        }
    }
}

bool DepthEstimator::estimate(const uint8_t* frame, int width, int height,
                               std::vector<float>& depthMap) {
    if (!ready_) return false;

    auto t0 = std::chrono::steady_clock::now();

    if (useFallback_) {
        fallbackDepth(frame, width, height, depthMap);
        outW_ = width;
        outH_ = height;
    } else {
#ifdef USE_ONNXRUNTIME
        try {
            preprocess(frame, width, height, inputBlob_.data());

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

            auto& outTensor = outputs[0];
            auto outShape = outTensor.GetTensorTypeAndShapeInfo().GetShape();
            const float* outData = outTensor.GetTensorData<float>();

            // Output shape: [1, 1, H, W] or [1, H, W]
            int rawH, rawW;
            if (outShape.size() == 4) {
                rawH = (int)outShape[2];
                rawW = (int)outShape[3];
            } else if (outShape.size() == 3) {
                rawH = (int)outShape[1];
                rawW = (int)outShape[2];
            } else {
                rawH = inputH_;
                rawW = inputW_;
            }

            postprocessDepth(outData, rawW, rawH, width, height, depthMap);
            outW_ = width;
            outH_ = height;

        } catch (const std::exception& e) {
            LOG_E(MOD, "Depth inference error: %s, switching to fallback", e.what());
            ExceptionHandler::instance().handleException(e, "depth");
            useFallback_ = true;
            fallbackDepth(frame, width, height, depthMap);
            outW_ = width;
            outH_ = height;
        }
#else
        fallbackDepth(frame, width, height, depthMap);
        outW_ = width;
        outH_ = height;
#endif
    }

    auto t1 = std::chrono::steady_clock::now();
    lastInferMs_ = std::chrono::duration<float, std::milli>(t1 - t0).count();

    {
        std::lock_guard<std::mutex> lk(depthMu_);
        lastDepthMap_ = depthMap;
    }

    return true;
}

float DepthEstimator::getDepthAt(int x, int y) const {
    std::lock_guard<std::mutex> lk(depthMu_);
    if (lastDepthMap_.empty() || outW_ == 0 || outH_ == 0) return maxDepth_;
    if (x < 0 || x >= outW_ || y < 0 || y >= outH_) return maxDepth_;
    return lastDepthMap_[y * outW_ + x];
}

float DepthEstimator::getRegionDepth(int x1, int y1, int x2, int y2) const {
    std::lock_guard<std::mutex> lk(depthMu_);
    if (lastDepthMap_.empty() || outW_ == 0) return maxDepth_;

    x1 = std::max(0, std::min(x1, outW_ - 1));
    y1 = std::max(0, std::min(y1, outH_ - 1));
    x2 = std::max(0, std::min(x2, outW_ - 1));
    y2 = std::max(0, std::min(y2, outH_ - 1));

    float sum = 0;
    int count = 0;
    // Sample every 4th pixel for speed
    for (int y = y1; y <= y2; y += 4) {
        for (int x = x1; x <= x2; x += 4) {
            sum += lastDepthMap_[y * outW_ + x];
            count++;
        }
    }
    return (count > 0) ? sum / count : maxDepth_;
}

float DepthEstimator::getMinRegionDepth(int x1, int y1, int x2, int y2) const {
    std::lock_guard<std::mutex> lk(depthMu_);
    if (lastDepthMap_.empty() || outW_ == 0) return maxDepth_;

    x1 = std::max(0, std::min(x1, outW_ - 1));
    y1 = std::max(0, std::min(y1, outH_ - 1));
    x2 = std::max(0, std::min(x2, outW_ - 1));
    y2 = std::max(0, std::min(y2, outH_ - 1));

    float minD = maxDepth_;
    for (int y = y1; y <= y2; y += 2) {
        for (int x = x1; x <= x2; x += 2) {
            float d = lastDepthMap_[y * outW_ + x];
            if (d < minD) minD = d;
        }
    }
    return minD;
}

void DepthEstimator::release() {
#ifdef USE_ONNXRUNTIME
    if (impl_) {
        impl_->session.reset();
        impl_->loaded = false;
    }
#endif
    ready_ = false;
    LOG_I(MOD, "Depth estimator released");
}