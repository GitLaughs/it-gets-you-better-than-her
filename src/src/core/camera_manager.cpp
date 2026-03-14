#include "camera_manager.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <errno.h>
#include <cmath>
#include <algorithm>
#include <numeric>

#ifdef __linux__
#include <linux/videodev2.h>
#include <sys/select.h>
#endif

static const char* MOD = "Camera";

CameraManager::CameraManager() {
    frameBuffer_.resize(1280 * 720, 128);  // Default
}

CameraManager::~CameraManager() {
    release();
}

bool CameraManager::initialize(const std::string& device, int width,
                                int height, int fps, int bufferCount) {
    state_ = CameraState::INITIALIZING;
    device_ = device;
    width_ = width;
    height_ = height;
    targetFps_ = fps;
    bufferCount_ = bufferCount;
    frameBuffer_.resize(width * height);

    LOG_I(MOD, "Initializing camera: %s %dx%d@%dfps",
          device.c_str(), width, height, fps);

    if (!initV4L2()) {
        LOG_W(MOD, "V4L2 init failed, using synthetic frame mode");
        syntheticMode_ = true;
    }

    state_ = CameraState::READY;
    LOG_I(MOD, "Camera ready (synthetic=%d)", syntheticMode_);
    return true;
}

bool CameraManager::initV4L2() {
#ifdef __linux__
    fd_ = open(device_.c_str(), O_RDWR | O_NONBLOCK);
    if (fd_ < 0) {
        LOG_W(MOD, "Cannot open %s: %s", device_.c_str(), strerror(errno));
        return false;
    }

    // Query capabilities
    struct v4l2_capability cap;
    if (ioctl(fd_, VIDIOC_QUERYCAP, &cap) < 0) {
        LOG_E(MOD, "VIDIOC_QUERYCAP failed");
        close(fd_); fd_ = -1;
        return false;
    }

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)) {
        LOG_E(MOD, "Device does not support capture");
        close(fd_); fd_ = -1;
        return false;
    }

    // Set format: GREY (mono 8-bit)
    struct v4l2_format fmt;
    memset(&fmt, 0, sizeof(fmt));
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = width_;
    fmt.fmt.pix.height = height_;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_GREY;
    fmt.fmt.pix.field = V4L2_FIELD_NONE;

    if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
        LOG_W(MOD, "Cannot set GREY format, trying YUYV");
        fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
        if (ioctl(fd_, VIDIOC_S_FMT, &fmt) < 0) {
            LOG_E(MOD, "VIDIOC_S_FMT failed: %s", strerror(errno));
            close(fd_); fd_ = -1;
            return false;
        }
    }

    // Set framerate
    struct v4l2_streamparm parm;
    memset(&parm, 0, sizeof(parm));
    parm.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    parm.parm.capture.timeperframe.numerator = 1;
    parm.parm.capture.timeperframe.denominator = targetFps_;
    ioctl(fd_, VIDIOC_S_PARM, &parm);  // Best-effort

    // Request buffers
    struct v4l2_requestbuffers req;
    memset(&req, 0, sizeof(req));
    req.count = bufferCount_;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_REQBUFS, &req) < 0) {
        LOG_E(MOD, "VIDIOC_REQBUFS failed");
        close(fd_); fd_ = -1;
        return false;
    }

    // Map buffers
    v4l2Buffers_.resize(req.count);
    for (unsigned int i = 0; i < req.count; ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(fd_, VIDIOC_QUERYBUF, &buf) < 0) {
            LOG_E(MOD, "VIDIOC_QUERYBUF failed for buffer %d", i);
            close(fd_); fd_ = -1;
            return false;
        }

        v4l2Buffers_[i].length = buf.length;
        v4l2Buffers_[i].start = mmap(NULL, buf.length,
                                      PROT_READ | PROT_WRITE,
                                      MAP_SHARED, fd_, buf.m.offset);
        if (v4l2Buffers_[i].start == MAP_FAILED) {
            LOG_E(MOD, "mmap failed for buffer %d", i);
            close(fd_); fd_ = -1;
            return false;
        }
    }

    LOG_I(MOD, "V4L2 initialized: %d buffers mapped", (int)req.count);
    return true;
#else
    return false;
#endif
}

bool CameraManager::startV4L2Streaming() {
#ifdef __linux__
    if (fd_ < 0) return false;

    // Queue all buffers
    for (int i = 0; i < (int)v4l2Buffers_.size(); ++i) {
        struct v4l2_buffer buf;
        memset(&buf, 0, sizeof(buf));
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;
        if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
            LOG_E(MOD, "VIDIOC_QBUF failed");
            return false;
        }
    }

    // Start streaming
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(fd_, VIDIOC_STREAMON, &type) < 0) {
        LOG_E(MOD, "VIDIOC_STREAMON failed");
        return false;
    }

    return true;
#else
    return false;
#endif
}

void CameraManager::stopV4L2Streaming() {
#ifdef __linux__
    if (fd_ < 0) return;
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd_, VIDIOC_STREAMOFF, &type);
#endif
}

bool CameraManager::dequeueFrame(uint8_t*& ptr, int& bytesUsed) {
#ifdef __linux__
    if (fd_ < 0) return false;

    // Wait for frame with select()
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(fd_, &fds);

    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    int r = select(fd_ + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) {
        if (r == 0) LOG_W(MOD, "V4L2 select timeout");
        return false;
    }

    struct v4l2_buffer buf;
    memset(&buf, 0, sizeof(buf));
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    if (ioctl(fd_, VIDIOC_DQBUF, &buf) < 0) {
        if (errno != EAGAIN) {
            LOG_E(MOD, "VIDIOC_DQBUF failed: %s", strerror(errno));
        }
        return false;
    }

    // Copy frame data to frameBuffer_ BEFORE re-queuing the buffer to the
    // kernel, to prevent a use-after-requeue race where the kernel may
    // overwrite the mmap'd region as soon as VIDIOC_QBUF is called.
    int copySize = std::min((int)buf.bytesused, width_ * height_);
    {
        std::lock_guard<std::mutex> lk(frameMutex_);
        memcpy(frameBuffer_.data(),
               (uint8_t*)v4l2Buffers_[buf.index].start, copySize);
    }
    ptr = frameBuffer_.data();
    bytesUsed = copySize;

    // Safe to re-queue now that data has been copied
    if (ioctl(fd_, VIDIOC_QBUF, &buf) < 0) {
        LOG_E(MOD, "VIDIOC_QBUF re-queue failed");
    }

    return true;
#else
    return false;
#endif
}

void CameraManager::releaseV4L2() {
#ifdef __linux__
    stopV4L2Streaming();
    for (auto& b : v4l2Buffers_) {
        if (b.start && b.start != MAP_FAILED) {
            munmap(b.start, b.length);
        }
    }
    v4l2Buffers_.clear();
    if (fd_ >= 0) {
        close(fd_);
        fd_ = -1;
    }
#endif
}

void CameraManager::generateSyntheticFrame() {
    // Generate a test pattern
    uint64_t fid = totalFrames_.load();
    int shift = (int)(fid % 256);
    for (int y = 0; y < height_; ++y) {
        for (int x = 0; x < width_; ++x) {
            int idx = y * width_ + x;
            uint8_t val = (uint8_t)((x + y + shift) & 0xFF);
            // Add gradient and grid
            if (x % 80 == 0 || y % 80 == 0) val = 255;
            frameBuffer_[idx] = val;
        }
    }
}

bool CameraManager::startCapture() {
    if (state_ != CameraState::READY && state_ != CameraState::STREAMING) {
        LOG_E(MOD, "Cannot start capture, state=%d", (int)state_);
        return false;
    }

    if (!syntheticMode_) {
        if (!startV4L2Streaming()) {
            LOG_W(MOD, "V4L2 streaming failed, falling back to synthetic");
            syntheticMode_ = true;
        }
    }

    running_ = true;
    fpsStartTime_ = std::chrono::steady_clock::now();
    fpsFrameCount_ = 0;

    captureThread_ = std::thread(&CameraManager::captureLoop, this);
    state_ = CameraState::STREAMING;
    LOG_I(MOD, "Capture started");
    return true;
}

void CameraManager::stopCapture() {
    running_ = false;
    if (captureThread_.joinable()) {
        captureThread_.join();
    }
    if (!syntheticMode_) {
        stopV4L2Streaming();
    }
    state_ = CameraState::READY;
    LOG_I(MOD, "Capture stopped");
}

void CameraManager::captureLoop() {
    LOG_I(MOD, "Capture thread started");
    int frameIntervalUs = 1000000 / targetFps_;

    while (running_) {
        auto loopStart = std::chrono::steady_clock::now();

        try {
            bool gotFrame = false;

            if (syntheticMode_) {
                generateSyntheticFrame();
                gotFrame = true;
            } else {
                uint8_t* rawPtr = nullptr;
                int bytesUsed = 0;
                if (dequeueFrame(rawPtr, bytesUsed)) {
                    // dequeueFrame already copied the V4L2 buffer into
                    // frameBuffer_ before re-queuing, so no copy needed here.
                    gotFrame = true;
                } else {
                    droppedFrames_++;
                }
            }

            if (gotFrame) {
                totalFrames_++;
                auto now = std::chrono::steady_clock::now();
                double ts = std::chrono::duration<double>(
                    now.time_since_epoch()).count();

                {
                    std::lock_guard<std::mutex> lk(frameMutex_);
                    frameInfo_.frameId = totalFrames_.load();
                    frameInfo_.timestamp = ts;
                }

                updateBrightness(frameBuffer_.data(), width_ * height_);
                updateFps();

                // Invoke callbacks
                for (auto& cb : callbacks_) {
                    try {
                        cb(frameBuffer_.data(), width_, height_, frameInfo_);
                    } catch (const std::exception& e) {
                        LOG_E(MOD, "Callback error: %s", e.what());
                    }
                }

                ExceptionHandler::instance().kickWatchdog();
            }

        } catch (const std::exception& e) {
            ExceptionHandler::instance().handleException(e, "camera");
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }

        // Frame rate limiting
        auto elapsed = std::chrono::steady_clock::now() - loopStart;
        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
        if (elapsedUs < frameIntervalUs) {
            std::this_thread::sleep_for(
                std::chrono::microseconds(frameIntervalUs - elapsedUs));
        }
    }

    LOG_I(MOD, "Capture thread ended. Total frames: %lu, dropped: %lu",
          totalFrames_.load(), droppedFrames_.load());
}

bool CameraManager::getFrame(uint8_t* frame, FrameInfo& info) {
    std::lock_guard<std::mutex> lk(frameMutex_);
    if (frameInfo_.frameId == 0) return false;
    memcpy(frame, frameBuffer_.data(), width_ * height_);
    info = frameInfo_;
    return true;
}

const uint8_t* CameraManager::lockFrame(FrameInfo& info) {
    frameMutex_.lock();
    info = frameInfo_;
    return frameBuffer_.data();
}

void CameraManager::unlockFrame() {
    frameMutex_.unlock();
}

void CameraManager::updateFps() {
    fpsFrameCount_++;
    if (fpsFrameCount_ >= 30) {
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(
            now - fpsStartTime_).count();
        if (elapsed > 0) {
            currentFps_ = (float)(fpsFrameCount_ / elapsed);
        }
        fpsFrameCount_ = 0;
        fpsStartTime_ = now;
    }
}

void CameraManager::updateBrightness(const uint8_t* data, int size) {
    // Sample every 64th pixel for speed
    long sum = 0;
    int count = 0;
    for (int i = 0; i < size; i += 64) {
        sum += data[i];
        count++;
    }
    float mean = (count > 0) ? (float)sum / count : 0.0f;

    std::lock_guard<std::mutex> lk(brightMu_);
    brightnessHistory_.push_back(mean);
    if ((int)brightnessHistory_.size() > BRIGHTNESS_WINDOW) {
        brightnessHistory_.erase(brightnessHistory_.begin());
    }
    frameInfo_.brightnessMean = mean;
}

float CameraManager::getAvgBrightness() const {
    std::lock_guard<std::mutex> lk(brightMu_);
    if (brightnessHistory_.empty()) return 0.0f;
    float sum = std::accumulate(brightnessHistory_.begin(),
                                 brightnessHistory_.end(), 0.0f);
    return sum / brightnessHistory_.size();
}

float CameraManager::getBrightnessStd() const {
    std::lock_guard<std::mutex> lk(brightMu_);
    if (brightnessHistory_.size() < 2) return 0.0f;
    float mean = std::accumulate(brightnessHistory_.begin(),
                                  brightnessHistory_.end(), 0.0f) /
                 brightnessHistory_.size();
    float sq = 0;
    for (float v : brightnessHistory_) {
        float d = v - mean;
        sq += d * d;
    }
    return std::sqrt(sq / brightnessHistory_.size());
}

float CameraManager::getDropRate() const {
    uint64_t total = totalFrames_.load();
    if (total == 0) return 0.0f;
    return (float)droppedFrames_.load() / total;
}

void CameraManager::setExposure(int us) {
#ifdef __linux__
    if (fd_ >= 0) {
        struct v4l2_control ctrl;
        ctrl.id = V4L2_CID_EXPOSURE_ABSOLUTE;
        ctrl.value = us;
        ioctl(fd_, VIDIOC_S_CTRL, &ctrl);
    }
#endif
    LOG_I(MOD, "Exposure set to %d us", us);
}

void CameraManager::setGain(float g) {
#ifdef __linux__
    if (fd_ >= 0) {
        struct v4l2_control ctrl;
        ctrl.id = V4L2_CID_GAIN;
        ctrl.value = (int)(g * 100);
        ioctl(fd_, VIDIOC_S_CTRL, &ctrl);
    }
#endif
    LOG_I(MOD, "Gain set to %.2f", g);
}

void CameraManager::setAutoExposure(bool enabled) {
#ifdef __linux__
    if (fd_ >= 0) {
        struct v4l2_control ctrl;
        ctrl.id = V4L2_CID_EXPOSURE_AUTO;
        ctrl.value = enabled ? V4L2_EXPOSURE_AUTO : V4L2_EXPOSURE_MANUAL;
        ioctl(fd_, VIDIOC_S_CTRL, &ctrl);
    }
#endif
    LOG_I(MOD, "Auto exposure: %s", enabled ? "ON" : "OFF");
}

void CameraManager::setStatusLed(bool on) {
#ifdef __linux__
    // On SmartSens A1, GPIO might be at /sys/class/gpio/
    if (gpioLedFd_ < 0) {
        // Export GPIO
        int fd = open("/sys/class/gpio/export", O_WRONLY);
        if (fd >= 0) {
            write(fd, "42", 2);  // GPIO 42
            close(fd);
        }
        fd = open("/sys/class/gpio/gpio42/direction", O_WRONLY);
        if (fd >= 0) {
            write(fd, "out", 3);
            close(fd);
        }
        gpioLedFd_ = open("/sys/class/gpio/gpio42/value", O_WRONLY);
    }
    if (gpioLedFd_ >= 0) {
        write(gpioLedFd_, on ? "1" : "0", 1);
    }
#endif
}

void CameraManager::registerCallback(FrameCallback cb) {
    callbacks_.push_back(std::move(cb));
}

void CameraManager::release() {
    stopCapture();
    releaseV4L2();
    if (gpioLedFd_ >= 0) {
        close(gpioLedFd_);
        gpioLedFd_ = -1;
    }
    state_ = CameraState::DISCONNECTED;
    LOG_I(MOD, "Camera released");
}