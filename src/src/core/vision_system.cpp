#include "vision_system.h"
#include "camera_manager.h"
#include "yolov8_detector.h"
#include "depth_estimator.h"
#include "tracker.h"
#include "obstacle_avoidance.h"
#include "point_cloud.h"
#include "position_estimator.h"
#include "hand_interface.h"
#include "video_output.h"
#include "hdr_controller.h"
#include "../config/config_loader.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <algorithm>
#include <cstring>
#include <sstream>
#include <fstream>
#include <cstdio>

#ifdef __linux__
#include <sys/sysinfo.h>
#include <unistd.h>
#endif

static const char* MOD = "VisionSys";

VisionSystem::VisionSystem() {
    memset(&stats_, 0, sizeof(stats_));
}

VisionSystem::~VisionSystem() {
    stop();
}

bool VisionSystem::initialize(const std::string& configFile) {
    LOG_I(MOD, "========================================");
    LOG_I(MOD, "  Vision System Initializing");
    LOG_I(MOD, "  Config: %s", configFile.c_str());
    LOG_I(MOD, "========================================");

    // Load configuration
    config_ = std::make_shared<ConfigLoader>();
    if (!config_->load(configFile)) {
        LOG_E(MOD, "Failed to load config: %s", configFile.c_str());
        return false;
    }

    // Get config parameters
    int camWidth = config_->getInt("camera.width", 1280);
    int camHeight = config_->getInt("camera.height", 720);
    int camFps = config_->getInt("camera.fps", 90);
    std::string camDevice = config_->getString("camera.device", "/dev/video0");

    std::string detectorModel = config_->getString("detector.model", "yolov8n.onnx");
    int detInputSize = config_->getInt("detector.input_size", 320);
    float detConfThresh = config_->getFloat("detector.conf_threshold", 0.5f);
    float detNmsThresh = config_->getFloat("detector.nms_threshold", 0.45f);
    int detThreads = config_->getInt("detector.threads", 2);

    std::string depthModel = config_->getString("depth.model", "midas_small.onnx");
    int depthInputSize = config_->getInt("depth.input_size", 256);
    float depthMin = config_->getFloat("depth.min_depth", 0.1f);
    float depthMax = config_->getFloat("depth.max_depth", 10.0f);

    float focalLength = config_->getFloat("camera.focal_length_px", 600.0f);

    std::string handPort = config_->getString("hand.serial_port", "");
    int handBaud = config_->getInt("hand.baudrate", 115200);

    std::string outputMode = config_->getString("output.mode", "serial");
    std::string outputPath = config_->getString("output.path", "/dev/ttyS1");
    int outputFps = config_->getInt("output.fps", 30);

    enableDetection_ = config_->getBool("features.detection", true);
    enableDepth_ = config_->getBool("features.depth", true);
    enableTracking_ = config_->getBool("features.tracking", true);
    enableObstacle_ = config_->getBool("features.obstacle_avoidance", true);
    enablePointCloud_ = config_->getBool("features.point_cloud", true);
    enablePosition_ = config_->getBool("features.position_estimation", true);
    enableHand_ = config_->getBool("features.hand_control", false);
    enableVideoOut_ = config_->getBool("features.video_output", true);

    // Initialize Camera
    camera_ = std::make_shared<CameraManager>();
    if (!camera_->initialize(camDevice, camWidth, camHeight, camFps)) {
        LOG_E(MOD, "Camera initialization failed!");
        ExceptionHandler::instance().handleCameraError("Camera init failed");
        return false;
    }
    LOG_I(MOD, "Camera initialized: %dx%d @%dfps", camWidth, camHeight, camFps);

    // Initialize frame buffers
    int frameSize = camWidth * camHeight;
    for (int i = 0; i < 2; ++i) {
        captureBuffer_[i].data.resize(frameSize, 0);
        captureBuffer_[i].width = camWidth;
        captureBuffer_[i].height = camHeight;
    }

    // Initialize YOLOv8 Detector
    if (enableDetection_) {
        detector_ = std::make_shared<YoloV8Detector>();
        if (!detector_->initialize(detectorModel, detInputSize,
                                    detConfThresh, detNmsThresh, detThreads)) {
            LOG_W(MOD, "Detector init failed, detection disabled");
            enableDetection_ = false;
        } else {
            LOG_I(MOD, "YOLOv8 detector initialized");
        }
    }

    // Initialize Depth Estimator
    if (enableDepth_) {
        depth_ = std::make_shared<DepthEstimator>();
        if (!depth_->initialize(depthModel, depthInputSize, depthInputSize,
                                 depthMin, depthMax, detThreads)) {
            LOG_W(MOD, "Depth estimator init failed, depth disabled");
            enableDepth_ = false;
        } else {
            LOG_I(MOD, "Depth estimator initialized (fallback=%s)",
                  depth_->isFallback() ? "yes" : "no");
        }
    }

    // Initialize Tracker
    if (enableTracking_) {
        tracker_ = std::make_shared<Tracker>();
        tracker_->init(
            config_->getInt("tracker.max_age", 15),
            config_->getInt("tracker.min_hits", 3),
            config_->getFloat("tracker.iou_threshold", 0.3f),
            config_->getInt("tracker.max_tracks", 50),
            config_->getInt("tracker.history_len", 30));
        LOG_I(MOD, "Tracker initialized");
    }

    // Initialize Obstacle Avoidance
    if (enableObstacle_) {
        obstacle_ = std::make_shared<ObstacleAvoidance>();
        obstacle_->init(
            config_->getFloat("obstacle.critical_dist", 0.3f),
            config_->getFloat("obstacle.danger_dist", 0.8f),
            config_->getFloat("obstacle.warning_dist", 1.5f),
            config_->getFloat("obstacle.safe_dist", 3.0f),
            config_->getFloat("obstacle.max_linear_speed", 0.5f),
            config_->getFloat("obstacle.max_angular_speed", 1.0f),
            config_->getInt("obstacle.sector_count", 36),
            config_->getFloat("camera.fov_h", 49.7f));
        LOG_I(MOD, "Obstacle avoidance initialized");
    }

    // Initialize Point Cloud Generator
    if (enablePointCloud_) {
        pointCloud_ = std::make_shared<PointCloudGenerator>();
        float cx = camWidth / 2.0f;
        float cy = camHeight / 2.0f;
        pointCloud_->init(focalLength, focalLength, cx, cy,
                           config_->getInt("pointcloud.downsample_step", 8),
                           config_->getInt("pointcloud.max_points", 30000));
        LOG_I(MOD, "Point cloud generator initialized");
    }

    // Initialize Position Estimator
    if (enablePosition_) {
        posEstimator_ = std::make_shared<PositionEstimator>();
        posEstimator_->init(
            focalLength,
            config_->getFloat("position.baseline_estimate", 0.1f),
            config_->getInt("position.max_features", 150),
            config_->getFloat("position.match_threshold", 0.7f),
            config_->getInt("position.history_size", 500));
        LOG_I(MOD, "Position estimator initialized");
    }

    // Initialize Hand Interface
    if (enableHand_ && !handPort.empty()) {
        hand_ = std::make_shared<HandInterface>();
        if (!hand_->initialize(handPort, handBaud)) {
            LOG_W(MOD, "Hand interface init failed, hand control disabled");
            enableHand_ = false;
        } else {
            LOG_I(MOD, "Hand interface initialized");
        }
    }

    // Initialize Video Output
    if (enableVideoOut_) {
        videoOut_ = std::make_shared<VideoOutput>();
        videoOut_->initialize(camWidth, camHeight, outputFps,
                               outputMode, outputPath);
        LOG_I(MOD, "Video output initialized");
    }

    LOG_I(MOD, "========================================");
    LOG_I(MOD, "  All modules initialized successfully");
    LOG_I(MOD, "  Features: det=%d depth=%d track=%d obs=%d pc=%d pos=%d hand=%d vid=%d",
          enableDetection_, enableDepth_, enableTracking_, enableObstacle_,
          enablePointCloud_, enablePosition_, enableHand_, enableVideoOut_);
    LOG_I(MOD, "========================================");

    return true;
}

void VisionSystem::start() {
    if (running_) return;

    LOG_I(MOD, "Starting vision system...");
    running_ = true;
    startTime_ = std::chrono::steady_clock::now();

    captureThread_ = std::thread(&VisionSystem::captureLoop, this);
    processThread_ = std::thread(&VisionSystem::processLoop, this);
    statsThread_ = std::thread(&VisionSystem::statsLoop, this);

    LOG_I(MOD, "Vision system started");
}

void VisionSystem::stop() {
    if (!running_) return;

    LOG_I(MOD, "Stopping vision system...");
    running_ = false;

    if (captureThread_.joinable()) captureThread_.join();
    if (processThread_.joinable()) processThread_.join();
    if (statsThread_.joinable()) statsThread_.join();

    // Release all modules
    if (camera_) camera_->release();
    if (detector_) detector_->release();
    if (depth_) depth_->release();
    if (hand_) hand_->release();
    if (videoOut_) videoOut_->release();

    auto stats = getStats();
    LOG_I(MOD, "========================================");
    LOG_I(MOD, "  Vision System Stopped");
    LOG_I(MOD, "  Total frames: %d, Dropped: %d (%.1f%%)",
          stats.framesCaptured, stats.framesDropped, stats.dropRate * 100);
    LOG_I(MOD, "  Avg FPS: capture=%.1f process=%.1f output=%.1f",
          stats.captureFps, stats.processFps, stats.outputFps);
    LOG_I(MOD, "  Avg latency: %.1fms", stats.totalLatencyMs);
    LOG_I(MOD, "========================================");
}

void VisionSystem::captureLoop() {
    LOG_I(MOD, "Capture thread started");

    auto lastFpsTime = std::chrono::steady_clock::now();
    int fpsCount = 0;

    while (running_) {
        int idx = writeIdx_.load();
        auto& buf = captureBuffer_[idx];

        auto t0 = std::chrono::steady_clock::now();

        bool ok = camera_->captureFrame(buf.data.data(),
                                         buf.data.size());
        if (!ok) {
            ExceptionHandler::instance().handleCameraError("Capture failed");
            // Attempt recovery
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            camera_->release();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            std::string dev = config_->getString("camera.device", "/dev/video0");
            int w = config_->getInt("camera.width", 1280);
            int h = config_->getInt("camera.height", 720);
            int fps = config_->getInt("camera.fps", 90);
            camera_->initialize(dev, w, h, fps);
            continue;
        }

        auto t1 = std::chrono::steady_clock::now();
        float captureMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

        buf.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            t1.time_since_epoch()).count();
        buf.ready = true;

        // Swap buffers
        writeIdx_.store(1 - idx);
        readIdx_.store(idx);

        {
            std::lock_guard<std::mutex> lk(statsMu_);
            stats_.captureLatencyMs = stats_.captureLatencyMs * 0.9f + captureMs * 0.1f;
            stats_.framesCaptured++;
        }

        fpsCount++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastFpsTime).count();
        if (elapsed >= 1.0f) {
            std::lock_guard<std::mutex> lk(statsMu_);
            stats_.captureFps = fpsCount / elapsed;
            fpsCount = 0;
            lastFpsTime = now;
        }
    }

    LOG_I(MOD, "Capture thread ended");
}

void VisionSystem::processLoop() {
    LOG_I(MOD, "Process thread started");

    auto lastFpsTime = std::chrono::steady_clock::now();
    int fpsCount = 0;

    while (running_) {
        int idx = readIdx_.load();
        auto& buf = captureBuffer_[idx];

        if (!buf.ready) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            continue;
        }

        auto t0 = std::chrono::steady_clock::now();

        try {
            const uint8_t* frame = buf.data.data();
            int w = buf.width;
            int h = buf.height;
            uint64_t ts = buf.timestampMs;

            // HDR adaptation
            adaptHDR(frame, w, h);

            // Pipeline stages
            if (enableDetection_) runDetection(frame, w, h);
            if (enableDepth_) runDepth(frame, w, h);
            if (enableTracking_) runTracking();
            if (enableObstacle_) runObstacleAvoidance();

            // Lower priority tasks: run every Nth frame
            if (enablePointCloud_ && (frameCounter_ % 3 == 0)) {
                runPointCloud(frame, w, h);
            }
            if (enablePosition_) {
                runPositionEstimation(frame, w, h, ts);
            }
            if (enableHand_) runHandControl();
            if (enableVideoOut_) runVideoOutput(frame, w, h, ts);

            // Callback
            if (frameCallback_) frameCallback_(frame, w, h, ts);

            buf.ready = false;
            frameCounter_++;

        } catch (const std::exception& e) {
            LOG_E(MOD, "Processing error: %s", e.what());
            ExceptionHandler::instance().handleException(e, "process");
            buf.ready = false;
            continue;
        }

        auto t1 = std::chrono::steady_clock::now();
        float procMs = std::chrono::duration<float, std::milli>(t1 - t0).count();

        {
            std::lock_guard<std::mutex> lk(statsMu_);
            stats_.totalLatencyMs = stats_.totalLatencyMs * 0.9f + procMs * 0.1f;
            stats_.framesProcessed++;
        }

        fpsCount++;
        auto now = std::chrono::steady_clock::now();
        float elapsed = std::chrono::duration<float>(now - lastFpsTime).count();
        if (elapsed >= 1.0f) {
            std::lock_guard<std::mutex> lk(statsMu_);
            stats_.processFps = fpsCount / elapsed;
            fpsCount = 0;
            lastFpsTime = now;
        }
    }

    LOG_I(MOD, "Process thread ended");
}

void VisionSystem::runDetection(const uint8_t* frame, int w, int h) {
    auto t0 = std::chrono::steady_clock::now();

    std::vector<BBox> detections;
    // YOLOv8 expects the frame; our detector handles grayscale→3ch inside
    detector_->detect(frame, w, h, detections);

    {
        std::lock_guard<std::mutex> lk(resultMu_);
        lastDetections_ = detections;
    }

    auto t1 = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lk(statsMu_);
        stats_.detectLatencyMs = stats_.detectLatencyMs * 0.9f + ms * 0.1f;
    }
}

void VisionSystem::runDepth(const uint8_t* frame, int w, int h) {
    auto t0 = std::chrono::steady_clock::now();

    std::vector<float> depthMap;
    depth_->estimate(frame, w, h, depthMap);

    {
        std::lock_guard<std::mutex> lk(resultMu_);
        lastDepthMap_ = depthMap;
    }

    auto t1 = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lk(statsMu_);
        stats_.depthLatencyMs = stats_.depthLatencyMs * 0.9f + ms * 0.1f;
    }
}

void VisionSystem::runTracking() {
    auto t0 = std::chrono::steady_clock::now();

    std::vector<BBox> dets;
    {
        std::lock_guard<std::mutex> lk(resultMu_);
        dets = lastDetections_;
    }

    tracker_->update(dets);

    // Update track depths
    if (enableDepth_ && depth_) {
        auto tracks = tracker_->getTracks(true);
        for (auto& tr : tracks) {
            float cx = (tr.bbox.x1 + tr.bbox.x2) / 2.0f;
            float cy = (tr.bbox.y1 + tr.bbox.y2) / 2.0f;
            float d = depth_->getRegionDepth(
                (int)tr.bbox.x1, (int)tr.bbox.y1,
                (int)tr.bbox.x2, (int)tr.bbox.y2);
            tracker_->updateTrackDepth(tr.trackId, d);
        }
    }

    auto t1 = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(t1 - t0).count();
    {
        std::lock_guard<std::mutex> lk(statsMu_);
        stats_.trackLatencyMs = stats_.trackLatencyMs * 0.9f + ms * 0.1f;
        stats_.activeTrackCount = tracker_->getActiveTrackCount();
    }
}

void VisionSystem::runObstacleAvoidance() {
    std::vector<float> depthMap;
    int w, h;
    {
        std::lock_guard<std::mutex> lk(resultMu_);
        depthMap = lastDepthMap_;
    }

    if (depthMap.empty()) return;

    w = depth_->getOutputWidth();
    h = depth_->getOutputHeight();

    obstacle_->updateDepthMap(depthMap, w, h);
    auto nav = obstacle_->computeNavigation();

    {
        std::lock_guard<std::mutex> lk(statsMu_);
        stats_.emergencyStop = nav.emergencyStop;
    }

    // Send navigation command to hand for emergency response
    if (nav.emergencyStop && enableHand_ && hand_ && hand_->isConnected()) {
        hand_->openHand(1.0f); // emergency: open hand
    }
}

void VisionSystem::runPointCloud(const uint8_t* frame, int w, int h) {
    std::vector<float> depthMap;
    {
        std::lock_guard<std::mutex> lk(resultMu_);
        depthMap = lastDepthMap_;
    }

    if (depthMap.empty()) return;

    pointCloud_->generate(depthMap, frame, w, h);

    {
        std::lock_guard<std::mutex> lk(statsMu_);
        stats_.pointCloudSize = pointCloud_->getPointCount();
    }
}

void VisionSystem::runPositionEstimation(const uint8_t* frame, int w, int h,
                                          uint64_t ts) {
    posEstimator_->update(frame, w, h, ts);
}

void VisionSystem::runHandControl() {
    if (!hand_ || !hand_->isConnected()) return;

    // Auto-grip based on closest tracked object
    auto tracks = tracker_->getTracks(true);
    if (!tracks.empty()) {
        // Find closest track
        float minDist = 999.0f;
        for (auto& tr : tracks) {
            if (tr.depth > 0 && tr.depth < minDist) {
                minDist = tr.depth;
            }
        }

        if (minDist < 999.0f) {
            hand_->autoGrip(minDist, 0.3f);
        }
    }

    {
        std::lock_guard<std::mutex> lk(statsMu_);
        stats_.handConnected = hand_->isConnected();
    }
}

void VisionSystem::runVideoOutput(const uint8_t* frame, int w, int h,
                                   uint64_t ts) {
    if (!videoOut_) return;

    videoOut_->clearOverlays();

    // Add detection boxes
    auto tracks = tracker_ ? tracker_->getTracks(true) : std::vector<TrackedObject>();
    for (auto& tr : tracks) {
        char label[64];
        snprintf(label, sizeof(label), "T%d %s d=%.1fm",
                 tr.trackId, tr.bbox.label.c_str(), tr.depth);
        videoOut_->addBox((int)tr.bbox.x1, (int)tr.bbox.y1,
                          (int)tr.bbox.x2, (int)tr.bbox.y2,
                          255, label, tr.trackId);
    }

    // Add status overlay
    auto st = getStats();
    char statusBuf[128];
    snprintf(statusBuf, sizeof(statusBuf),
             "FPS:%.0f Lat:%.0fms Trk:%d PC:%d",
             st.processFps, st.totalLatencyMs,
             st.activeTrackCount, st.pointCloudSize);
    videoOut_->addText(5, 5, statusBuf, 255);

    // Pose info
    if (enablePosition_ && posEstimator_) {
        auto pose = posEstimator_->getCurrentPose();
        snprintf(statusBuf, sizeof(statusBuf),
                 "Pos:(%.2f,%.2f) Th:%.1f",
                 pose.x, pose.y, pose.theta * 180.0f / 3.14159f);
        videoOut_->addText(5, 15, statusBuf, 200);
    }

    // Obstacle warning
    if (enableObstacle_ && obstacle_) {
        float closestDist = obstacle_->getClosestDistance();
        if (closestDist < 1.0f) {
            snprintf(statusBuf, sizeof(statusBuf),
                     "!! OBSTACLE: %.2fm !!", closestDist);
            videoOut_->addText(w / 2 - 60, h / 2, statusBuf, 255);
        }
    }

    videoOut_->pushFrame(frame, w, h, ts);

    // Also send JSON status via serial
    if (frameCounter_ % 10 == 0) {
        std::ostringstream json;
        json << "{\"fps\":" << (int)st.processFps
             << ",\"lat\":" << (int)st.totalLatencyMs
             << ",\"trk\":" << st.activeTrackCount
             << ",\"pc\":" << st.pointCloudSize
             << ",\"drop\":" << st.framesDropped;
        if (enablePosition_ && posEstimator_) {
            auto pose = posEstimator_->getCurrentPose();
            json << ",\"px\":" << pose.x << ",\"py\":" << pose.y;
        }
        if (enableObstacle_ && obstacle_) {
            json << ",\"obs\":" << obstacle_->getClosestDistance();
        }
        json << "}";
        videoOut_->sendSerialStatus(json.str());
    }

    {
        std::lock_guard<std::mutex> lk(statsMu_);
        stats_.outputFps = videoOut_->getOutputFps();
        stats_.framesDropped = videoOut_->getDroppedFrames();
        if (stats_.framesCaptured > 0) {
            stats_.dropRate = (float)stats_.framesDropped / stats_.framesCaptured;
        }
    }
}

void VisionSystem::adaptHDR(const uint8_t* frame, int w, int h) {
    // Compute average brightness for HDR / auto-exposure adaptation
    // Sample every 32nd pixel
    int sum = 0, count = 0;
    for (int i = 0; i < w * h; i += 32) {
        sum += frame[i];
        count++;
    }
    float avgBrightness = (count > 0) ? (float)sum / count : 128.0f;

    // Auto-exposure hint: if too dark or too bright, adjust
    // This would interface with the HDR controller
    if (avgBrightness < 30.0f) {
        // Very dark scene: increase exposure
        LOG_D(MOD, "Dark scene detected: avg=%.0f", avgBrightness);
    } else if (avgBrightness > 220.0f) {
        // Very bright: decrease exposure
        LOG_D(MOD, "Bright scene detected: avg=%.0f", avgBrightness);
    }
}

void VisionSystem::statsLoop() {
    LOG_I(MOD, "Stats thread started");

    while (running_) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        checkResources();

        auto st = getStats();
        LOG_I(MOD, "Stats: capFPS=%.1f procFPS=%.1f outFPS=%.1f lat=%.1fms drop=%.1f%% trk=%d pc=%d",
              st.captureFps, st.processFps, st.outputFps,
              st.totalLatencyMs, st.dropRate * 100,
              st.activeTrackCount, st.pointCloudSize);
    }

    LOG_I(MOD, "Stats thread ended");
}

void VisionSystem::checkResources() {
    float cpu = getCpuUsage();
    float mem = getMemUsageMB();

    {
        std::lock_guard<std::mutex> lk(statsMu_);
        stats_.cpuUsage = cpu;
        stats_.memUsageMB = mem;
        auto now = std::chrono::steady_clock::now();
        stats_.uptimeMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - startTime_).count();
    }

    // Resource alerts
    if (mem > 200.0f) {
        LOG_W(MOD, "High memory usage: %.1f MB", mem);
        ExceptionHandler::instance().handleResourceWarning("memory", mem);
    }
    if (cpu > 95.0f) {
        LOG_W(MOD, "High CPU usage: %.1f%%", cpu);
        ExceptionHandler::instance().handleResourceWarning("cpu", cpu);
    }
}

float VisionSystem::getCpuUsage() {
#ifdef __linux__
    static long prevIdle = 0, prevTotal = 0;
    std::ifstream proc("/proc/stat");
    if (!proc.is_open()) return 0;

    std::string cpu;
    long user, nice, sys, idle, iowait, irq, softirq, steal;
    proc >> cpu >> user >> nice >> sys >> idle >> iowait >> irq >> softirq >> steal;

    long totalIdle = idle + iowait;
    long total = user + nice + sys + idle + iowait + irq + softirq + steal;

    long diffIdle = totalIdle - prevIdle;
    long diffTotal = total - prevTotal;

    prevIdle = totalIdle;
    prevTotal = total;

    if (diffTotal == 0) return 0;
    return (1.0f - (float)diffIdle / diffTotal) * 100.0f;
#else
    return 0;
#endif
}

float VisionSystem::getMemUsageMB() {
#ifdef __linux__
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.find("VmRSS:") == 0) {
            long kb = 0;
            sscanf(line.c_str(), "VmRSS: %ld kB", &kb);
            return kb / 1024.0f;
        }
    }
#endif
    return 0;
}

SystemStats VisionSystem::getStats() const {
    std::lock_guard<std::mutex> lk(statsMu_);
    return stats_;
}

void VisionSystem::setFrameCallback(FrameCallback cb) {
    frameCallback_ = std::move(cb);
}

void VisionSystem::triggerGrip() {
    if (hand_ && hand_->isConnected()) {
        hand_->executeGrip(GripPattern::POWER, 0.8f);
        LOG_I(MOD, "Manual grip triggered");
    }
}

void VisionSystem::triggerPointCloudExport(const std::string& filename) {
    if (pointCloud_) {
        pointCloud_->exportPLY(filename);
        LOG_I(MOD, "Point cloud export triggered: %s", filename.c_str());
    }
}