#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <memory>
#include <functional>
#include <chrono>
#include <deque>

#include "tracker.h"
#include "hdr_controller.h"

// Forward declarations
class CameraManager;
class YOLOv8Detector;
class DepthEstimator;
class Tracker;
class ObstacleAvoidance;
class PointCloudGenerator;
class PositionEstimator;
class HandInterface;
class VideoOutput;
class ConfigLoader;

struct SystemStats {
    float captureFps;
    float processFps;
    float outputFps;
    float captureLatencyMs;
    float detectLatencyMs;
    float depthLatencyMs;
    float trackLatencyMs;
    float totalLatencyMs;
    int framesCaptured;
    int framesProcessed;
    int framesDropped;
    float dropRate;
    float cpuUsage;
    float memUsageMB;
    int activeTrackCount;
    int pointCloudSize;
    bool handConnected;
    bool emergencyStop;
    uint64_t uptimeMs;
};

class VisionSystem {
public:
    VisionSystem();
    ~VisionSystem();

    bool initialize(const std::string& configFile);

    void start();
    void stop();

    bool isRunning() const { return running_; }

    SystemStats getStats() const;

    // Register callback for processed frames
    using FrameCallback = std::function<void(const uint8_t*, int, int, uint64_t)>;
    void setFrameCallback(FrameCallback cb);

    // Manual trigger
    void triggerGrip();
    void triggerPointCloudExport(const std::string& filename);

private:
    // Main processing pipeline
    void captureLoop();
    void processLoop();
    void statsLoop();

    // Pipeline stages
    void runDetection(const uint8_t* frame, int w, int h);
    void runDepth(const uint8_t* frame, int w, int h);
    void runTracking();
    void runObstacleAvoidance();
    void runPointCloud(const uint8_t* frame, int w, int h);
    void runPositionEstimation(const uint8_t* frame, int w, int h, uint64_t ts);
    void runHandControl();
    void runVideoOutput(const uint8_t* frame, int w, int h, uint64_t ts);

    // Resource monitoring
    void checkResources();
    float getCpuUsage();
    float getMemUsageMB();

    // HDR adaptive control
    void adaptHDR(uint8_t* frame, int w, int h);

    // Config
    std::shared_ptr<ConfigLoader> config_;

    // HDR controller
    HDRController hdrController_;

    // Core modules
    std::shared_ptr<CameraManager> camera_;
    std::shared_ptr<YOLOv8Detector> detector_;
    std::shared_ptr<DepthEstimator> depth_;
    std::shared_ptr<Tracker> tracker_;
    std::shared_ptr<ObstacleAvoidance> obstacle_;
    std::shared_ptr<PointCloudGenerator> pointCloud_;
    std::shared_ptr<PositionEstimator> posEstimator_;
    std::shared_ptr<HandInterface> hand_;
    std::shared_ptr<VideoOutput> videoOut_;

    // Threads
    std::thread captureThread_;
    std::thread processThread_;
    std::thread statsThread_;

    std::atomic<bool> running_{false};
    std::atomic<bool> captureReady_{false};

    // Frame buffer (double-buffered)
    struct FrameBuffer {
        std::vector<uint8_t> data;
        int width = 0, height = 0;
        uint64_t timestampMs = 0;
        bool ready = false;
    };

    FrameBuffer captureBuffer_[2];
    std::atomic<int> writeIdx_{0};
    std::atomic<int> readIdx_{1};
    std::mutex frameMu_;

    // Pipeline results
    std::vector<BBox> lastDetections_;
    std::vector<float> lastDepthMap_;
    std::mutex resultMu_;

    // Stats
    SystemStats stats_;
    mutable std::mutex statsMu_;
    std::chrono::steady_clock::time_point startTime_;

    // Callbacks
    FrameCallback frameCallback_;

    // Feature flags
    bool enableDetection_ = true;
    bool enableDepth_ = true;
    bool enableTracking_ = true;
    bool enableObstacle_ = true;
    bool enablePointCloud_ = true;
    bool enablePosition_ = true;
    bool enableHand_ = false;
    bool enableVideoOut_ = true;

    int frameCounter_ = 0;
};