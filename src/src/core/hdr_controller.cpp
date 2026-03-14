#include "hdr_controller.h"
#include "../utils/logger.h"
#include <algorithm>
#include <numeric>
#include <chrono>
#include <cstring>

static const char* MOD = "HDR";

static int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

HDRController::HDRController() {
    // Init identity LUT
    for (int i = 0; i < 256; ++i) gammaLUT_[i] = (uint8_t)i;
}

void HDRController::init(float brightnessLow, float brightnessHigh,
                          float varThresh, int cooldownMs,
                          float claheClip, int claheGrid,
                          const std::string& mode) {
    brightnessLow_ = brightnessLow;
    brightnessHigh_ = brightnessHigh;
    varianceThresh_ = varThresh;
    cooldownMs_ = cooldownMs;
    claheClip_ = claheClip;
    claheGrid_ = claheGrid;

    if (mode == "off") mode_ = HDRMode::OFF;
    else if (mode == "software") mode_ = HDRMode::SOFTWARE;
    else if (mode == "hardware") mode_ = HDRMode::HARDWARE;
    else mode_ = HDRMode::AUTO;

    enabled_ = (mode_ != HDRMode::OFF);

    LOG_I(MOD, "HDR init: mode=%s low=%.0f high=%.0f var=%.1f cooldown=%dms",
          mode.c_str(), brightnessLow, brightnessHigh, varThresh, cooldownMs);
}

bool HDRController::shouldEnableHDR(const uint8_t* frame, int width, int height) {
    if (!enabled_) return false;

    int size = width * height;

    // Compute mean brightness (sample for speed)
    long sum = 0;
    int count = 0;
    for (int i = 0; i < size; i += 32) {
        sum += frame[i];
        count++;
    }
    float brightness = (count > 0) ? (float)sum / count : 128.0f;

    {
        std::lock_guard<std::mutex> lk(mu_);
        brightnessHistory_.push_back(brightness);
        if ((int)brightnessHistory_.size() > MAX_HISTORY) {
            brightnessHistory_.erase(brightnessHistory_.begin());
        }
    }

    // Cooldown check
    int64_t now = nowMs();
    if (now - lastSwitchTimeMs_.load() < cooldownMs_) {
        return active_.load();
    }

    bool needsHdr = false;

    // Condition 1: overall too dark or too bright
    if (brightness < brightnessLow_ || brightness > brightnessHigh_) {
        needsHdr = true;
    }

    // Condition 2: brightness variance
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (brightnessHistory_.size() >= 10) {
            int n = (int)brightnessHistory_.size();
            float mean = std::accumulate(
                brightnessHistory_.end() - 10,
                brightnessHistory_.end(), 0.0f) / 10.0f;
            float var = 0;
            for (int i = n - 10; i < n; ++i) {
                float d = brightnessHistory_[i] - mean;
                var += d * d;
            }
            var = std::sqrt(var / 10.0f);
            if (var > varianceThresh_) needsHdr = true;
        }
    }

    // Condition 3: high dynamic range in frame
    float p5, p95;
    computePercentiles(frame, size, p5, p95);
    if ((p95 - p5) > 180.0f) {
        needsHdr = true;
    }

    if (needsHdr != active_.load()) {
        active_.store(needsHdr);
        lastSwitchTimeMs_.store(now);
        LOG_I(MOD, "HDR %s (brightness=%.1f, DR=%.0f)",
              active_.load() ? "ENABLED" : "DISABLED", brightness, p95 - p5);
    }

    return active_.load();
}

void HDRController::processFrame(uint8_t* frame, int width, int height) {
    if (!active_.load()) return;
    applyCLAHE(frame, width, height);
}

void HDRController::applyCLAHE(uint8_t* frame, int width, int height) {
    int gridW = claheGrid_;
    int gridH = claheGrid_;
    int tileW = width / gridW;
    int tileH = height / gridH;
    if (tileW < 2 || tileH < 2) {
        // Too small for CLAHE, apply simple gamma
        float brightness = 0;
        int size = width * height;
        for (int i = 0; i < size; i += 64) brightness += frame[i];
        int sampleCount = (size + 63) / 64;
        brightness /= sampleCount;

        float gamma = 1.0f;
        if (brightness < 60) gamma = 0.5f;
        else if (brightness > 200) gamma = 1.8f;
        applyGamma(frame, width, height, gamma);
        return;
    }

    int clipLimit = (int)(claheClip_ * (tileW * tileH) / 256.0f);
    if (clipLimit < 1) clipLimit = 1;

    // Process each tile
    for (int ty = 0; ty < gridH; ++ty) {
        for (int tx = 0; tx < gridW; ++tx) {
            int x0 = tx * tileW;
            int y0 = ty * tileH;
            int x1 = (tx == gridW - 1) ? width : x0 + tileW;
            int y1 = (ty == gridH - 1) ? height : y0 + tileH;

            // Build histogram
            int hist[256] = {0};
            int tilePixels = 0;
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    hist[frame[y * width + x]]++;
                    tilePixels++;
                }
            }

            // Clip histogram
            int excess = 0;
            for (int i = 0; i < 256; ++i) {
                if (hist[i] > clipLimit) {
                    excess += hist[i] - clipLimit;
                    hist[i] = clipLimit;
                }
            }
            int avgInc = excess / 256;
            int remain = excess - avgInc * 256;
            for (int i = 0; i < 256; ++i) {
                hist[i] += avgInc;
                if (i < remain) hist[i]++;
            }

            // Build CDF and map
            int cdf[256];
            cdf[0] = hist[0];
            for (int i = 1; i < 256; ++i) {
                cdf[i] = cdf[i - 1] + hist[i];
            }

            int cdfMin = 0;
            for (int i = 0; i < 256; ++i) {
                if (cdf[i] > 0) { cdfMin = cdf[i]; break; }
            }

            uint8_t lut[256];
            int denom = tilePixels - cdfMin;
            if (denom <= 0) denom = 1;
            for (int i = 0; i < 256; ++i) {
                lut[i] = (uint8_t)std::min(255,
                    std::max(0, (int)((float)(cdf[i] - cdfMin) / denom * 255.0f)));
            }

            // Apply
            for (int y = y0; y < y1; ++y) {
                for (int x = x0; x < x1; ++x) {
                    frame[y * width + x] = lut[frame[y * width + x]];
                }
            }
        }
    }
}

void HDRController::applyGamma(uint8_t* frame, int width, int height,
                                float gamma) {
    // Build LUT
    for (int i = 0; i < 256; ++i) {
        float normalized = (float)i / 255.0f;
        gammaLUT_[i] = (uint8_t)(std::pow(normalized, gamma) * 255.0f);
    }

    int size = width * height;
    for (int i = 0; i < size; ++i) {
        frame[i] = gammaLUT_[frame[i]];
    }
}

void HDRController::computePercentiles(const uint8_t* frame, int size,
                                        float& p5, float& p95) const {
    int hist[256] = {0};
    int step = std::max(1, size / 10000);  // Sample ~10k pixels
    int count = 0;
    for (int i = 0; i < size; i += step) {
        hist[frame[i]]++;
        count++;
    }

    int target5 = (int)(count * 0.05f);
    int target95 = (int)(count * 0.95f);
    int cumul = 0;
    p5 = 0; p95 = 255;

    bool p5Set = false;
    for (int i = 0; i < 256; ++i) {
        cumul += hist[i];
        if (!p5Set && cumul >= target5) { p5 = (float)i; p5Set = true; }
        if (cumul >= target95) { p95 = (float)i; break; }
    }
}

HDRController::Status HDRController::getStatus() const {
    std::lock_guard<std::mutex> lk(mu_);
    Status s;
    s.active = active_.load();
    switch (mode_) {
        case HDRMode::OFF: s.modeStr = "off"; break;
        case HDRMode::SOFTWARE: s.modeStr = "software"; break;
        case HDRMode::HARDWARE: s.modeStr = "hardware"; break;
        case HDRMode::AUTO: s.modeStr = "auto"; break;
    }
    s.historyLen = (int)brightnessHistory_.size();
    s.lastBrightness = brightnessHistory_.empty() ? 0 : brightnessHistory_.back();
    return s;
}

void HDRController::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    active_.store(false);
    brightnessHistory_.clear();
    lastSwitchTimeMs_.store(0);
    LOG_I(MOD, "HDR controller reset");
}