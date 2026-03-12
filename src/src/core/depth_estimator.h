#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>
#include <mutex>

class DepthEstimator {
public:
    DepthEstimator();
    ~DepthEstimator();

    bool initialize(const std::string& modelPath, int inputW, int inputH,
                    float minDepth, float maxDepth, int numThreads);

    // Estimate depth from grayscale frame, output depthMap size = width*height
    bool estimate(const uint8_t* frame, int width, int height,
                  std::vector<float>& depthMap);

    // Query depth at pixel
    float getDepthAt(int x, int y) const;

    // Average depth in bounding box
    float getRegionDepth(int x1, int y1, int x2, int y2) const;

    // Minimum depth in bounding box (closest obstacle)
    float getMinRegionDepth(int x1, int y1, int x2, int y2) const;

    void release();

    bool isReady() const { return ready_; }
    bool isFallback() const { return useFallback_; }
    float getLastInferTimeMs() const { return lastInferMs_; }
    int getOutputWidth() const { return outW_; }
    int getOutputHeight() const { return outH_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    void preprocess(const uint8_t* frame, int width, int height, float* output);
    void postprocessDepth(const float* rawOut, int rawW, int rawH,
                          int origW, int origH, std::vector<float>& depthMap);
    // Software fallback when no ONNX model available
    void fallbackDepth(const uint8_t* frame, int width, int height,
                       std::vector<float>& depthMap);

    int inputW_ = 256;
    int inputH_ = 256;
    int outW_ = 0;
    int outH_ = 0;
    float minDepth_ = 0.1f;
    float maxDepth_ = 10.0f;
    bool ready_ = false;
    bool useFallback_ = false;
    float lastInferMs_ = 0.0f;

    std::vector<float> inputBlob_;
    std::vector<float> lastDepthMap_;
    mutable std::mutex depthMu_;
};