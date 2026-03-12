#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

struct Detection {
    float x1, y1, x2, y2;   // Bounding box
    float confidence;
    int classId;
    char className[32];

    float centerX() const { return (x1 + x2) * 0.5f; }
    float centerY() const { return (y1 + y2) * 0.5f; }
    float area() const { return std::max(0.0f, x2 - x1) * std::max(0.0f, y2 - y1); }
};

class YOLOv8Detector {
public:
    YOLOv8Detector();
    ~YOLOv8Detector();

    bool initialize(const std::string& modelPath, int inputW, int inputH,
                    float confThresh, float iouThresh, int maxDet, int numThreads);

    // Detect on grayscale frame
    std::vector<Detection> detect(const uint8_t* frame, int width, int height);

    void release();

    bool isReady() const { return ready_; }
    float getLastInferTimeMs() const { return lastInferMs_; }

    // COCO class names
    static const char* className(int id);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Preprocessing
    void preprocess(const uint8_t* frame, int width, int height,
                    float* output, float& ratio, float& dw, float& dh);

    // Postprocessing
    std::vector<Detection> postprocess(const float* rawOutput, int numAnchors,
                                        int numClasses, int origW, int origH,
                                        float ratio, float dw, float dh);

    // NMS
    static std::vector<int> nms(const std::vector<Detection>& dets, float iouThresh);

    static float iou(const Detection& a, const Detection& b);

    int inputW_ = 320;
    int inputH_ = 320;
    float confThresh_ = 0.45f;
    float iouThresh_ = 0.5f;
    int maxDet_ = 50;
    bool ready_ = false;
    float lastInferMs_ = 0.0f;

    std::vector<float> inputBlob_;     // Pre-allocated input buffer
    std::vector<float> resizeBuffer_;  // Intermediate resize buffer
};