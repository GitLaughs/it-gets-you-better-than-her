#pragma once
#include <cstdint>
#include <vector>
#include <string>
#include <mutex>
#include <atomic>
#include <cmath>

enum class HDRMode {
    OFF,
    SOFTWARE,
    HARDWARE,
    AUTO
};

class HDRController {
public:
    HDRController();
    void init(float brightnessLow, float brightnessHigh,
              float varThresh, int cooldownMs,
              float claheClip, int claheGrid, const std::string& mode);

    // Analyze frame to determine if HDR is needed
    bool shouldEnableHDR(const uint8_t* frame, int width, int height);

    // Apply HDR processing (in-place)
    void processFrame(uint8_t* frame, int width, int height);

    bool isActive() const { return active_.load(); }
    HDRMode getMode() const { return mode_; }

    struct Status {
        bool active;
        std::string modeStr;
        float lastBrightness;
        int historyLen;
    };
    Status getStatus() const;
    void reset();

private:
    // CLAHE (Contrast Limited Adaptive Histogram Equalization)
    void applyCLAHE(uint8_t* frame, int width, int height);

    // Simple gamma correction
    void applyGamma(uint8_t* frame, int width, int height, float gamma);

    // Compute percentiles
    void computePercentiles(const uint8_t* frame, int size,
                            float& p5, float& p95) const;

    HDRMode mode_ = HDRMode::AUTO;
    bool enabled_ = true;
    std::atomic<bool> active_{false};

    float brightnessLow_ = 30.0f;
    float brightnessHigh_ = 220.0f;
    float varianceThresh_ = 40.0f;
    int cooldownMs_ = 2000;
    float claheClip_ = 3.0f;
    int claheGrid_ = 8;

    std::atomic<int64_t> lastSwitchTimeMs_{0};

    std::vector<float> brightnessHistory_;
    static constexpr int MAX_HISTORY = 60;

    // Pre-computed gamma LUT
    uint8_t gammaLUT_[256];

    mutable std::mutex mu_;
};