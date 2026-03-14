#include "hand_interface.h"
#include "../utils/logger.h"
#include "../utils/exception_handler.h"

#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <chrono>
#include <algorithm>

#ifdef __linux__
#include <termios.h>
#include <sys/select.h>
#endif

static const char* MOD = "Hand";

HandInterface::HandInterface() {}

HandInterface::~HandInterface() { release(); }

bool HandInterface::initialize(const std::string& serialPort, int baudrate) {
    port_ = serialPort;
    baudrate_ = baudrate;

    LOG_I(MOD, "Initializing hand interface: %s @ %d baud", serialPort.c_str(), baudrate);

    if (!openSerial(serialPort, baudrate)) {
        LOG_W(MOD, "Cannot open serial port %s, hand interface offline", serialPort.c_str());
        return false;
    }

    connected_ = true;
    running_ = true;
    commThread_ = std::thread(&HandInterface::commLoop, this);

    LOG_I(MOD, "Hand interface ready");
    return true;
}

bool HandInterface::openSerial(const std::string& port, int baudrate) {
#ifdef __linux__
    serialFd_ = open(port.c_str(), O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (serialFd_ < 0) {
        LOG_W(MOD, "Cannot open %s: %s", port.c_str(), strerror(errno));
        return false;
    }

    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(serialFd_, &tty) != 0) {
        LOG_E(MOD, "tcgetattr failed");
        close(serialFd_); serialFd_ = -1;
        return false;
    }

    speed_t baud;
    switch (baudrate) {
        case 9600:   baud = B9600;   break;
        case 19200:  baud = B19200;  break;
        case 38400:  baud = B38400;  break;
        case 57600:  baud = B57600;  break;
        case 115200: baud = B115200; break;
        default:     baud = B115200; break;
    }

    cfsetospeed(&tty, baud);
    cfsetispeed(&tty, baud);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_cflag |= (CLOCAL | CREAD);
    tty.c_cflag &= ~(PARENB | PARODD);
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CRTSCTS;

    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL);

    tty.c_lflag = 0;
    tty.c_oflag = 0;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1; // 100ms timeout

    if (tcsetattr(serialFd_, TCSANOW, &tty) != 0) {
        LOG_E(MOD, "tcsetattr failed");
        close(serialFd_); serialFd_ = -1;
        return false;
    }

    tcflush(serialFd_, TCIOFLUSH);
    return true;
#else
    return false;
#endif
}

void HandInterface::closeSerial() {
#ifdef __linux__
    if (serialFd_ >= 0) {
        close(serialFd_);
        serialFd_ = -1;
    }
#endif
}

uint16_t HandInterface::computeChecksum(const uint8_t* data, int len) {
    uint16_t sum = 0;
    for (int i = 0; i < len; ++i) sum += data[i];
    return sum & 0xFFFF;
}

bool HandInterface::sendCommand(const HandCommand& cmd) {
    if (serialFd_ < 0) return false;

    // Protocol: [0xAA][0x55][cmdType][fingerId][position][speed][checksumH][checksumL]
    uint8_t packet[8];
    packet[0] = 0xAA;        // Header
    packet[1] = 0x55;        // Header
    packet[2] = cmd.cmdType;
    packet[3] = cmd.fingerId;
    packet[4] = cmd.position;
    packet[5] = cmd.speed;

    uint16_t chk = computeChecksum(packet + 2, 4);
    packet[6] = (chk >> 8) & 0xFF;
    packet[7] = chk & 0xFF;

#ifdef __linux__
    int written = write(serialFd_, packet, 8);
    if (written != 8) {
        LOG_W(MOD, "Serial write failed: %d/%d bytes", written, 8);
        return false;
    }
    tcdrain(serialFd_);
    return true;
#else
    return false;
#endif
}

bool HandInterface::readResponse(uint8_t* buf, int maxLen, int& bytesRead) {
    bytesRead = 0;
    if (serialFd_ < 0) return false;

#ifdef __linux__
    fd_set fds;
    FD_ZERO(&fds);
    FD_SET(serialFd_, &fds);

    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 50000; // 50ms

    int r = select(serialFd_ + 1, &fds, NULL, NULL, &tv);
    if (r <= 0) return false;

    bytesRead = read(serialFd_, buf, maxLen);
    return bytesRead > 0;
#else
    return false;
#endif
}

void HandInterface::commLoop() {
    LOG_I(MOD, "Hand comm thread started");

    while (running_) {
        // Send pending commands
        HandCommand cmd;
        bool hasCmd = false;
        {
            std::lock_guard<std::mutex> lk(cmdMu_);
            if (!cmdQueue_.empty()) {
                cmd = cmdQueue_.front();
                cmdQueue_.pop_front();
                hasCmd = true;
            }
        }

        if (hasCmd) {
            if (sendCommand(cmd)) {
                consecutiveErrors_ = 0;

                // Read feedback
                uint8_t resp[16];
                int respLen = 0;
                if (readResponse(resp, sizeof(resp), respLen) && respLen >= 6) {
                    if (resp[0] == 0xAA && resp[1] == 0x55) {
                        std::lock_guard<std::mutex> lk(stateMu_);
                        // Parse state feedback
                        if (resp[2] == 0x10) { // State report
                            for (int i = 0; i < 5 && (3 + i) < respLen; ++i) {
                                state_.fingerPos[i] = resp[3 + i] / 255.0f;
                            }
                            state_.lastUpdateMs = (uint32_t)
                                std::chrono::duration_cast<std::chrono::milliseconds>(
                                    std::chrono::steady_clock::now().time_since_epoch()).count();
                        }
                    }
                }
            } else {
                consecutiveErrors_++;
                if (consecutiveErrors_ >= MAX_CONSECUTIVE_ERRORS) {
                    LOG_E(MOD, "Too many consecutive errors, reconnecting...");
                    closeSerial();
                    connected_ = false;
                    std::this_thread::sleep_for(std::chrono::seconds(1));
                    if (openSerial(port_, baudrate_)) {
                        connected_ = true;
                        consecutiveErrors_ = 0;
                        LOG_I(MOD, "Serial reconnected");
                    }
                }
            }
        }

        // Periodic state query
        if (++queryCount_ >= 20) { // ~every 200ms
            queryCount_ = 0;
            HandCommand query;
            query.cmdType = 0x04; // query
            query.fingerId = 0xFF;
            query.position = 0;
            query.speed = 0;
            sendCommand(query);

            uint8_t resp[16];
            int respLen = 0;
            if (readResponse(resp, sizeof(resp), respLen) && respLen >= 8) {
                std::lock_guard<std::mutex> lk(stateMu_);
                if (resp[0] == 0xAA && resp[1] == 0x55 && resp[2] == 0x10) {
                    for (int i = 0; i < 5 && (3+i) < respLen; ++i) {
                        state_.fingerPos[i] = resp[3+i] / 255.0f;
                    }
                    state_.connected = true;
                    state_.lastUpdateMs = (uint32_t)
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::steady_clock::now().time_since_epoch()).count();
                }
            }

            // Copy callback and state under the lock, then invoke outside
            // the lock to avoid holding it during a potentially blocking call.
            StateCallback cb;
            HandState stateCopy;
            {
                std::lock_guard<std::mutex> lk(stateMu_);
                cb = stateCallback_;
                stateCopy = state_;
            }
            if (cb) cb(stateCopy);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    LOG_I(MOD, "Hand comm thread ended");
}

bool HandInterface::setFingerPosition(Finger finger, float position, float speed) {
    if (!connected_) return false;

    position = std::max(0.0f, std::min(1.0f, position));
    speed = std::max(0.0f, std::min(1.0f, speed));

    HandCommand cmd;
    cmd.cmdType = 0x01;
    cmd.fingerId = (uint8_t)finger;
    cmd.position = (uint8_t)(position * 255);
    cmd.speed = (uint8_t)(speed * 255);

    std::lock_guard<std::mutex> lk(cmdMu_);
    cmdQueue_.push_back(cmd);
    return true;
}

bool HandInterface::executeGrip(GripPattern pattern, float speed) {
    if (!connected_) return false;

    float positions[5] = {0};

    switch (pattern) {
        case GripPattern::OPEN:
            // All fingers open
            break;
        case GripPattern::CLOSE:
            for (auto& p : positions) p = 1.0f;
            break;
        case GripPattern::PINCH:
            positions[(int)Finger::THUMB] = 0.7f;
            positions[(int)Finger::INDEX] = 0.7f;
            break;
        case GripPattern::POWER:
            for (auto& p : positions) p = 0.9f;
            break;
        case GripPattern::POINT:
            positions[(int)Finger::THUMB] = 0.8f;
            positions[(int)Finger::MIDDLE] = 0.8f;
            positions[(int)Finger::RING] = 0.8f;
            positions[(int)Finger::PINKY] = 0.8f;
            // INDEX stays open (pointing)
            break;
        case GripPattern::CUSTOM:
            break;
    }

    HandCommand cmd;
    cmd.cmdType = 0x02;
    cmd.fingerId = (uint8_t)pattern;
    cmd.position = (uint8_t)(speed * 255);
    cmd.speed = (uint8_t)(speed * 255);

    std::lock_guard<std::mutex> lk(cmdMu_);
    cmdQueue_.push_back(cmd);

    LOG_I(MOD, "Grip pattern: %d, speed: %.2f", (int)pattern, speed);
    return true;
}

bool HandInterface::setWristAngle(float degrees) {
    if (!connected_) return false;

    degrees = std::max(-90.0f, std::min(90.0f, degrees));

    HandCommand cmd;
    cmd.cmdType = 0x03;
    cmd.fingerId = 0;
    cmd.position = (uint8_t)((degrees + 90.0f) / 180.0f * 255.0f);
    cmd.speed = 128;

    std::lock_guard<std::mutex> lk(cmdMu_);
    cmdQueue_.push_back(cmd);
    return true;
}

bool HandInterface::openHand(float speed) {
    return executeGrip(GripPattern::OPEN, speed);
}

bool HandInterface::closeHand(float speed) {
    return executeGrip(GripPattern::CLOSE, speed);
}

HandState HandInterface::getState() const {
    std::lock_guard<std::mutex> lk(stateMu_);
    return state_;
}

bool HandInterface::autoGrip(float targetDistance, float gripThreshold) {
    if (!connected_) return false;

    if (targetDistance < gripThreshold && targetDistance > 0.05f) {
        // Close proportional to distance
        float gripStrength = 1.0f - (targetDistance / gripThreshold);
        gripStrength = std::max(0.3f, std::min(1.0f, gripStrength));

        for (int i = 0; i < (int)Finger::NUM_FINGERS; ++i) {
            setFingerPosition((Finger)i, gripStrength, 0.8f);
        }
        LOG_D(MOD, "Auto-grip: dist=%.2f strength=%.2f", targetDistance, gripStrength);
        return true;
    } else if (targetDistance >= gripThreshold) {
        openHand(0.3f);
    }
    return false;
}

void HandInterface::setStateCallback(StateCallback cb) {
    std::lock_guard<std::mutex> lk(stateMu_);
    stateCallback_ = std::move(cb);
}

void HandInterface::release() {
    running_ = false;
    if (commThread_.joinable()) {
        commThread_.join();
    }
    closeSerial();
    connected_ = false;
    LOG_I(MOD, "Hand interface released");
}