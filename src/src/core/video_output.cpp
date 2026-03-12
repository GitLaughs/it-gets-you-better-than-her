#include "video_output.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

#ifdef __linux__
#include <termios.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#endif

static const char* MOD = "VideoOut";

// Minimal 5x7 font for overlay rendering
static const uint8_t FONT_5X7[96][7] = {
    // space (32) to tilde (126), simplified bitmap
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // space
    {0x04,0x04,0x04,0x04,0x00,0x04,0x00}, // !
    {0x0A,0x0A,0x00,0x00,0x00,0x00,0x00}, // "
    {0x0A,0x1F,0x0A,0x1F,0x0A,0x00,0x00}, // #
    {0x04,0x0F,0x14,0x0E,0x05,0x1E,0x04}, // $
    {0x18,0x19,0x02,0x04,0x08,0x13,0x03}, // %
    {0x08,0x14,0x08,0x15,0x12,0x0D,0x00}, // &
    {0x04,0x04,0x00,0x00,0x00,0x00,0x00}, // '
    {0x02,0x04,0x04,0x04,0x04,0x02,0x00}, // (
    {0x08,0x04,0x04,0x04,0x04,0x08,0x00}, // )
    {0x04,0x15,0x0E,0x15,0x04,0x00,0x00}, // *
    {0x00,0x04,0x04,0x1F,0x04,0x04,0x00}, // +
    {0x00,0x00,0x00,0x00,0x04,0x04,0x08}, // ,
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x04,0x00}, // .
    {0x01,0x02,0x04,0x08,0x10,0x00,0x00}, // /
    {0x0E,0x11,0x13,0x15,0x19,0x0E,0x00}, // 0
    {0x04,0x0C,0x04,0x04,0x04,0x0E,0x00}, // 1
    {0x0E,0x11,0x01,0x06,0x08,0x1F,0x00}, // 2
    {0x0E,0x11,0x02,0x01,0x11,0x0E,0x00}, // 3
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x00}, // 4
    {0x1F,0x10,0x1E,0x01,0x11,0x0E,0x00}, // 5
    {0x06,0x08,0x1E,0x11,0x11,0x0E,0x00}, // 6
    {0x1F,0x01,0x02,0x04,0x08,0x08,0x00}, // 7
    {0x0E,0x11,0x0E,0x11,0x11,0x0E,0x00}, // 8
    {0x0E,0x11,0x0F,0x01,0x02,0x0C,0x00}, // 9
    {0x00,0x04,0x00,0x00,0x04,0x00,0x00}, // :
    {0x00,0x04,0x00,0x00,0x04,0x04,0x08}, // ;
    {0x02,0x04,0x08,0x04,0x02,0x00,0x00}, // <
    {0x00,0x00,0x1F,0x00,0x1F,0x00,0x00}, // =
    {0x08,0x04,0x02,0x04,0x08,0x00,0x00}, // >
    {0x0E,0x11,0x02,0x04,0x00,0x04,0x00}, // ?
    {0x0E,0x11,0x17,0x15,0x17,0x10,0x0E}, // @
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x00}, // A
    {0x1E,0x11,0x1E,0x11,0x11,0x1E,0x00}, // B
    {0x0E,0x11,0x10,0x10,0x11,0x0E,0x00}, // C
    {0x1E,0x11,0x11,0x11,0x11,0x1E,0x00}, // D
    {0x1F,0x10,0x1E,0x10,0x10,0x1F,0x00}, // E
    {0x1F,0x10,0x1E,0x10,0x10,0x10,0x00}, // F
    {0x0E,0x11,0x10,0x13,0x11,0x0E,0x00}, // G
    {0x11,0x11,0x1F,0x11,0x11,0x11,0x00}, // H
    {0x0E,0x04,0x04,0x04,0x04,0x0E,0x00}, // I
    {0x01,0x01,0x01,0x01,0x11,0x0E,0x00}, // J
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, // K
    {0x10,0x10,0x10,0x10,0x10,0x1F,0x00}, // L
    {0x11,0x1B,0x15,0x11,0x11,0x11,0x00}, // M
    {0x11,0x19,0x15,0x13,0x11,0x11,0x00}, // N
    {0x0E,0x11,0x11,0x11,0x11,0x0E,0x00}, // O
    {0x1E,0x11,0x1E,0x10,0x10,0x10,0x00}, // P
    {0x0E,0x11,0x11,0x15,0x12,0x0D,0x00}, // Q
    {0x1E,0x11,0x1E,0x14,0x12,0x11,0x00}, // R
    {0x0E,0x11,0x0C,0x02,0x11,0x0E,0x00}, // S
    {0x1F,0x04,0x04,0x04,0x04,0x04,0x00}, // T
    {0x11,0x11,0x11,0x11,0x11,0x0E,0x00}, // U
    {0x11,0x11,0x11,0x0A,0x0A,0x04,0x00}, // V
    {0x11,0x11,0x15,0x15,0x0A,0x0A,0x00}, // W
    {0x11,0x0A,0x04,0x04,0x0A,0x11,0x00}, // X
    {0x11,0x0A,0x04,0x04,0x04,0x04,0x00}, // Y
    {0x1F,0x01,0x02,0x04,0x08,0x1F,0x00}, // Z
    {0x0E,0x08,0x08,0x08,0x08,0x0E,0x00}, // [
    {0x10,0x08,0x04,0x02,0x01,0x00,0x00}, // backslash
    {0x0E,0x02,0x02,0x02,0x02,0x0E,0x00}, // ]
    {0x04,0x0A,0x11,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x1F,0x00}, // _
    {0x08,0x04,0x00,0x00,0x00,0x00,0x00}, // `
    {0x00,0x0E,0x01,0x0F,0x11,0x0F,0x00}, // a
    {0x10,0x10,0x1E,0x11,0x11,0x1E,0x00}, // b
    {0x00,0x0E,0x11,0x10,0x11,0x0E,0x00}, // c
    {0x01,0x01,0x0F,0x11,0x11,0x0F,0x00}, // d
    {0x00,0x0E,0x11,0x1F,0x10,0x0E,0x00}, // e
    {0x06,0x08,0x1C,0x08,0x08,0x08,0x00}, // f
    {0x00,0x0F,0x11,0x0F,0x01,0x0E,0x00}, // g
    {0x10,0x10,0x1E,0x11,0x11,0x11,0x00}, // h
    {0x04,0x00,0x0C,0x04,0x04,0x0E,0x00}, // i
    {0x02,0x00,0x06,0x02,0x02,0x12,0x0C}, // j
    {0x10,0x12,0x14,0x18,0x14,0x12,0x00}, // k
    {0x0C,0x04,0x04,0x04,0x04,0x0E,0x00}, // l
    {0x00,0x1A,0x15,0x15,0x11,0x11,0x00}, // m
    {0x00,0x1E,0x11,0x11,0x11,0x11,0x00}, // n
    {0x00,0x0E,0x11,0x11,0x11,0x0E,0x00}, // o
    {0x00,0x1E,0x11,0x1E,0x10,0x10,0x00}, // p
    {0x00,0x0F,0x11,0x0F,0x01,0x01,0x00}, // q
    {0x00,0x16,0x19,0x10,0x10,0x10,0x00}, // r
    {0x00,0x0F,0x10,0x0E,0x01,0x1E,0x00}, // s
    {0x08,0x1C,0x08,0x08,0x09,0x06,0x00}, // t
    {0x00,0x11,0x11,0x11,0x11,0x0F,0x00}, // u
    {0x00,0x11,0x11,0x0A,0x0A,0x04,0x00}, // v
    {0x00,0x11,0x11,0x15,0x15,0x0A,0x00}, // w
    {0x00,0x11,0x0A,0x04,0x0A,0x11,0x00}, // x
    {0x00,0x11,0x11,0x0F,0x01,0x0E,0x00}, // y
    {0x00,0x1F,0x02,0x04,0x08,0x1F,0x00}, // z
    {0x06,0x04,0x08,0x04,0x04,0x06,0x00}, // {
    {0x04,0x04,0x04,0x04,0x04,0x04,0x00}, // |
    {0x0C,0x04,0x02,0x04,0x04,0x0C,0x00}, // }
    {0x00,0x00,0x0D,0x12,0x00,0x00,0x00}, // ~
};

VideoOutput::VideoOutput() {}

VideoOutput::~VideoOutput() { release(); }

bool VideoOutput::initialize(int width, int height, int fps,
                              const std::string& outputMode,
                              const std::string& outputPath) {
    width_ = width;
    height_ = height;
    targetFps_ = fps;
    outputMode_ = outputMode;
    outputPath_ = outputPath;

    LOG_I(MOD, "VideoOutput init: %dx%d @%dfps mode=%s path=%s",
          width, height, fps, outputMode.c_str(), outputPath.c_str());

    if (outputMode == "fb") {
        if (!initFramebuffer(outputPath)) {
            LOG_W(MOD, "Framebuffer init failed, falling back to serial");
            outputMode_ = "serial";
        }
    }

    if (outputMode_ == "serial" || outputMode == "serial") {
        initSerialOutput(outputPath);
    }

    if (outputMode_ == "file") {
        // Create output directory
        mkdir(outputPath.c_str(), 0755);
    }

    running_ = true;
    outputThread_ = std::thread(&VideoOutput::outputLoop, this);

    return true;
}

bool VideoOutput::initFramebuffer(const std::string& fbDevice) {
#ifdef __linux__
    std::string dev = fbDevice.empty() ? "/dev/fb0" : fbDevice;
    fbFd_ = open(dev.c_str(), O_RDWR);
    if (fbFd_ < 0) {
        LOG_W(MOD, "Cannot open framebuffer %s", dev.c_str());
        return false;
    }
    LOG_I(MOD, "Framebuffer opened: %s", dev.c_str());
    return true;
#else
    return false;
#endif
}

bool VideoOutput::initSerialOutput(const std::string& port) {
#ifdef __linux__
    if (port.empty()) return false;

    serialFd_ = open(port.c_str(), O_WRONLY | O_NOCTTY);
    if (serialFd_ < 0) {
        // Try stdout as fallback
        serialFd_ = STDOUT_FILENO;
        LOG_W(MOD, "Cannot open serial %s, using stdout", port.c_str());
    } else {
        struct termios tty;
        memset(&tty, 0, sizeof(tty));
        tcgetattr(serialFd_, &tty);
        cfsetospeed(&tty, B115200);
        tty.c_cflag = CS8 | CLOCAL;
        tty.c_oflag = 0;
        tty.c_lflag = 0;
        tcsetattr(serialFd_, TCSANOW, &tty);
    }
    return true;
#else
    return false;
#endif
}

bool VideoOutput::pushFrame(const uint8_t* frame, int width, int height,
                             uint64_t timestampMs) {
    totalFrames_++;

    OutputFrame of;
    of.data.assign(frame, frame + width * height);
    of.width = width;
    of.height = height;
    of.timestampMs = timestampMs;

    // Get and apply overlays
    {
        std::lock_guard<std::mutex> lk(overlayMu_);
        of.boxes = pendingBoxes_;
        of.texts = pendingTexts_;
    }

    {
        std::lock_guard<std::mutex> lk(queueMu_);
        if (frameQueue_.size() >= 3) {
            frameQueue_.pop_front();
            droppedFrames_++;
        }
        frameQueue_.push_back(std::move(of));
    }

    return true;
}

void VideoOutput::addBox(int x1, int y1, int x2, int y2,
                          uint8_t color, const std::string& label, int trackId) {
    std::lock_guard<std::mutex> lk(overlayMu_);
    pendingBoxes_.push_back({x1, y1, x2, y2, color, label, trackId});
}

void VideoOutput::addText(int x, int y, const std::string& text, uint8_t color) {
    std::lock_guard<std::mutex> lk(overlayMu_);
    pendingTexts_.push_back({x, y, text, color});
}

void VideoOutput::clearOverlays() {
    std::lock_guard<std::mutex> lk(overlayMu_);
    pendingBoxes_.clear();
    pendingTexts_.clear();
}

void VideoOutput::drawRect(uint8_t* frame, int w, int h,
                            int x1, int y1, int x2, int y2,
                            uint8_t color, int thickness) {
    x1 = std::max(0, std::min(w-1, x1));
    y1 = std::max(0, std::min(h-1, y1));
    x2 = std::max(0, std::min(w-1, x2));
    y2 = std::max(0, std::min(h-1, y2));

    for (int t = 0; t < thickness; ++t) {
        int xx1 = x1 + t, yy1 = y1 + t;
        int xx2 = x2 - t, yy2 = y2 - t;
        if (xx1 >= xx2 || yy1 >= yy2) break;

        for (int x = xx1; x <= xx2; ++x) {
            frame[yy1 * w + x] = color;
            frame[yy2 * w + x] = color;
        }
        for (int y = yy1; y <= yy2; ++y) {
            frame[y * w + xx1] = color;
            frame[y * w + xx2] = color;
        }
    }
}

void VideoOutput::drawChar(uint8_t* frame, int w, int h,
                            int x, int y, char c, uint8_t color) {
    int idx = c - 32;
    if (idx < 0 || idx >= 96) return;

    for (int row = 0; row < 7; ++row) {
        for (int col = 0; col < 5; ++col) {
            if (FONT_5X7[idx][row] & (1 << (4 - col))) {
                int px = x + col, py = y + row;
                if (px >= 0 && px < w && py >= 0 && py < h) {
                    frame[py * w + px] = color;
                }
            }
        }
    }
}

void VideoOutput::drawString(uint8_t* frame, int w, int h,
                              int x, int y, const std::string& str,
                              uint8_t color) {
    for (int i = 0; i < (int)str.size(); ++i) {
        drawChar(frame, w, h, x + i * 6, y, str[i], color);
    }
}

void VideoOutput::renderOverlays(uint8_t* frame, int width, int height) {
    std::lock_guard<std::mutex> lk(overlayMu_);

    for (auto& box : pendingBoxes_) {
        drawRect(frame, width, height, box.x1, box.y1, box.x2, box.y2,
                 box.color, 2);
        if (!box.label.empty()) {
            drawString(frame, width, height, box.x1 + 2, box.y1 - 8,
                       box.label, box.color);
        }
    }

    for (auto& txt : pendingTexts_) {
        drawString(frame, width, height, txt.x, txt.y, txt.text, txt.color);
    }
}

bool VideoOutput::writeFramebuffer(const uint8_t* frame, int width, int height) {
#ifdef __linux__
    if (fbFd_ < 0) return false;
    lseek(fbFd_, 0, SEEK_SET);
    write(fbFd_, frame, width * height);
    return true;
#else
    return false;
#endif
}

bool VideoOutput::writeSerialFrame(const uint8_t* frame, int width, int height) {
    if (serialFd_ < 0) return false;

    // Send frame header + downsampled data for serial bandwidth
    // Downsample to 160x90 for serial transmission
    int outW = 160, outH = 90;
    float scaleX = (float)width / outW;
    float scaleY = (float)height / outH;

    uint8_t header[8] = {0xFF, 0xD8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    header[2] = (outW >> 8) & 0xFF;
    header[3] = outW & 0xFF;
    header[4] = (outH >> 8) & 0xFF;
    header[5] = outH & 0xFF;

    write(serialFd_, header, 8);

    std::vector<uint8_t> downsampled(outW * outH);
    for (int y = 0; y < outH; ++y) {
        for (int x = 0; x < outW; ++x) {
            int sx = (int)(x * scaleX);
            int sy = (int)(y * scaleY);
            sx = std::min(sx, width - 1);
            sy = std::min(sy, height - 1);
            downsampled[y * outW + x] = frame[sy * width + sx];
        }
    }

    write(serialFd_, downsampled.data(), downsampled.size());
    return true;
}

bool VideoOutput::writeFileFrame(const uint8_t* frame, int width, int height,
                                  uint64_t timestamp) {
    char filename[256];
    snprintf(filename, sizeof(filename), "%s/frame_%06d_%lu.raw",
             outputPath_.c_str(), frameIndex_, (unsigned long)timestamp);

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;

    write(fd, frame, width * height);
    close(fd);
    return true;
}

bool VideoOutput::sendSerialStatus(const std::string& statusJson) {
    if (serialFd_ < 0 && outputMode_ != "serial") return false;

    int fd = (serialFd_ >= 0) ? serialFd_ : STDOUT_FILENO;

    // Send with line terminator
    std::string msg = statusJson + "\n";
    write(fd, msg.c_str(), msg.size());
    return true;
}

void VideoOutput::outputLoop() {
    LOG_I(MOD, "Video output thread started");

    auto lastTime = std::chrono::steady_clock::now();
    int framesSinceLastFps = 0;
    float fpsAccum = 0;

    while (running_) {
        OutputFrame frame;
        bool hasFrame = false;

        {
            std::lock_guard<std::mutex> lk(queueMu_);
            if (!frameQueue_.empty()) {
                frame = std::move(frameQueue_.front());
                frameQueue_.pop_front();
                hasFrame = true;
            }
        }

        if (hasFrame) {
            // Render overlays onto frame
            for (auto& box : frame.boxes) {
                drawRect(frame.data.data(), frame.width, frame.height,
                         box.x1, box.y1, box.x2, box.y2, box.color, 2);
                if (!box.label.empty()) {
                    drawString(frame.data.data(), frame.width, frame.height,
                               box.x1 + 2, std::max(0, box.y1 - 9),
                               box.label, box.color);
                }
            }
            for (auto& txt : frame.texts) {
                drawString(frame.data.data(), frame.width, frame.height,
                           txt.x, txt.y, txt.text, txt.color);
            }

            // Output based on mode
            if (outputMode_ == "fb") {
                writeFramebuffer(frame.data.data(), frame.width, frame.height);
            } else if (outputMode_ == "serial") {
                writeSerialFrame(frame.data.data(), frame.width, frame.height);
            } else if (outputMode_ == "file") {
                writeFileFrame(frame.data.data(), frame.width, frame.height,
                               frame.timestampMs);
            }

            frameIndex_++;
            framesSinceLastFps++;

            // FPS calculation
            auto now = std::chrono::steady_clock::now();
            float elapsed = std::chrono::duration<float>(now - lastTime).count();
            if (elapsed >= 1.0f) {
                outputFps_ = framesSinceLastFps / elapsed;
                framesSinceLastFps = 0;
                lastTime = now;
            }
        } else {
            // No frame available, sleep
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }

        // Rate limiting
        if (targetFps_ > 0) {
            int delayMs = 1000 / targetFps_ - 2;
            if (delayMs > 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
            }
        }
    }

    LOG_I(MOD, "Video output thread ended (total=%d dropped=%d)",
          totalFrames_, droppedFrames_);
}

void VideoOutput::release() {
    running_ = false;
    if (outputThread_.joinable()) {
        outputThread_.join();
    }

#ifdef __linux__
    if (fbFd_ >= 0 && fbFd_ != STDOUT_FILENO) { close(fbFd_); fbFd_ = -1; }
    if (serialFd_ >= 0 && serialFd_ != STDOUT_FILENO) { close(serialFd_); serialFd_ = -1; }
#endif

    LOG_I(MOD, "VideoOutput released");
}