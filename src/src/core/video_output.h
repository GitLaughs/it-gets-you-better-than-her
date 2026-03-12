#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <deque>
#include <functional>

struct OverlayBox {
    int x1, y1, x2, y2;
    uint8_t color;      // grayscale value for border
    std::string label;
    int trackId;
};

struct OverlayText {
    int x, y;
    std::string text;
    uint8_t color;
};

struct OutputFrame {
    std::vector<uint8_t> data;
    int width, height;
    uint64_t timestampMs;
    std::vector<OverlayBox> boxes;
    std::vector<OverlayText> texts;
};

class VideoOutput {
public:
    VideoOutput();
    ~VideoOutput();

    bool initialize(int width, int height, int fps,
                    const std::string& outputMode,     // "serial", "fb", "file"
                    const std::string& outputPath);

    // Submit frame for output
    bool pushFrame(const uint8_t* frame, int width, int height,
                   uint64_t timestampMs);

    // Add overlays before rendering
    void addBox(int x1, int y1, int x2, int y2,
                uint8_t color, const std::string& label, int trackId = -1);
    void addText(int x, int y, const std::string& text, uint8_t color = 255);
    void clearOverlays();

    // Render overlays onto frame
    void renderOverlays(uint8_t* frame, int width, int height);

    // Stats
    float getOutputFps() const { return outputFps_; }
    int getDroppedFrames() const { return droppedFrames_; }
    int getTotalFrames() const { return totalFrames_; }

    // Serial output: send compressed frame + metadata
    bool sendSerialStatus(const std::string& statusJson);

    void release();

private:
    void outputLoop();

    // Drawing primitives on grayscale image
    void drawRect(uint8_t* frame, int w, int h,
                  int x1, int y1, int x2, int y2, uint8_t color, int thickness = 1);
    void drawChar(uint8_t* frame, int w, int h,
                  int x, int y, char c, uint8_t color);
    void drawString(uint8_t* frame, int w, int h,
                    int x, int y, const std::string& str, uint8_t color);

    // Framebuffer output
    bool initFramebuffer(const std::string& fbDevice);
    bool writeFramebuffer(const uint8_t* frame, int width, int height);

    // Serial output
    bool initSerialOutput(const std::string& port);
    bool writeSerialFrame(const uint8_t* frame, int width, int height);

    // File output (raw frames)
    bool writeFileFrame(const uint8_t* frame, int width, int height,
                        uint64_t timestamp);

    int width_ = 0, height_ = 0;
    int targetFps_ = 30;
    std::string outputMode_;
    std::string outputPath_;

    int fbFd_ = -1;
    int serialFd_ = -1;
    int fileFd_ = -1;
    int frameIndex_ = 0;

    std::deque<OutputFrame> frameQueue_;
    std::mutex queueMu_;
    std::atomic<bool> running_{false};
    std::thread outputThread_;

    std::vector<OverlayBox> pendingBoxes_;
    std::vector<OverlayText> pendingTexts_;
    std::mutex overlayMu_;

    float outputFps_ = 0;
    int droppedFrames_ = 0;
    int totalFrames_ = 0;
};