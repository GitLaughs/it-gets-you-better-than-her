#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>

struct FrameInfo {
    uint64_t frameId = 0;
    double   timestamp = 0.0;
    int      exposureUs = 0;
    float    gain = 0.0f;
    float    brightnessMean = 0.0f;
    bool     isHdr = false;
};

enum class CameraState {
    DISCONNECTED,
    INITIALIZING,
    READY,
    STREAMING,
    ERROR
};

class CameraManager {
public:
    CameraManager();
    ~CameraManager();

    bool initialize(const std::string& device, int width, int height,
                    int fps, int bufferCount = 4);
    bool startCapture();
    void stopCapture();
    void release();

    // Get latest frame (thread-safe, returns copy)
    // frame: output buffer (caller must pre-allocate width*height bytes for GREY)
    bool getFrame(uint8_t* frame, FrameInfo& info);

    // Zero-copy access: lock frame, use pointer, then unlock
    const uint8_t* lockFrame(FrameInfo& info);
    void unlockFrame();

    CameraState getState() const { return state_; }
    float getCurrentFps() const { return currentFps_; }
    int getWidth() const { return width_; }
    int getHeight() const { return height_; }
    int getTargetFps() const { return targetFps_; }

    // Brightness monitoring
    float getAvgBrightness() const;
    float getBrightnessStd() const;

    // Frame drop statistics
    uint64_t getTotalFrames() const { return totalFrames_; }
    uint64_t getDroppedFrames() const { return droppedFrames_; }
    float getDropRate() const;

    // Exposure control
    void setExposure(int us);
    void setGain(float g);
    void setAutoExposure(bool enabled);

    // GPIO status LED
    void setStatusLed(bool on);

    using FrameCallback = std::function<void(const uint8_t*, int, int, const FrameInfo&)>;
    void registerCallback(FrameCallback cb);

private:
    void captureLoop();
    bool initV4L2();
    void releaseV4L2();
    bool startV4L2Streaming();
    void stopV4L2Streaming();
    bool dequeueFrame(uint8_t*& ptr, int& bytesUsed);
    void enqueueBuffer(int idx);

    // Synthetic frame for testing without hardware
    void generateSyntheticFrame();

    void updateFps();
    void updateBrightness(const uint8_t* data, int size);

    std::string device_;
    int width_ = 1280;
    int height_ = 720;
    int targetFps_ = 90;
    int bufferCount_ = 4;

    int fd_ = -1;                  // V4L2 file descriptor
    struct V4L2Buffer {
        void*  start = nullptr;
        size_t length = 0;
    };
    std::vector<V4L2Buffer> v4l2Buffers_;

    // Current frame storage
    std::vector<uint8_t> frameBuffer_;
    FrameInfo frameInfo_;
    std::mutex frameMutex_;

    // Capture thread
    std::thread captureThread_;
    std::atomic<bool> running_{false};
    CameraState state_ = CameraState::DISCONNECTED;

    // FPS calculation
    std::atomic<float> currentFps_{0.0f};
    uint64_t fpsFrameCount_ = 0;
    std::chrono::steady_clock::time_point fpsStartTime_;

    // Frame counter
    std::atomic<uint64_t> totalFrames_{0};
    std::atomic<uint64_t> droppedFrames_{0};

    // Brightness history
    std::vector<float> brightnessHistory_;
    static constexpr int BRIGHTNESS_WINDOW = 30;
    mutable std::mutex brightMu_;

    // Callbacks
    std::vector<FrameCallback> callbacks_;

    // Synthetic mode
    bool syntheticMode_ = false;

    // GPIO
    int gpioLedFd_ = -1;
};