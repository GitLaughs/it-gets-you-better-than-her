#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>
#include <deque>

// Finger indices
enum class Finger {
    THUMB = 0,
    INDEX,
    MIDDLE,
    RING,
    PINKY,
    NUM_FINGERS
};

// Predefined grip patterns
enum class GripPattern {
    OPEN = 0,
    CLOSE,
    PINCH,
    POWER,
    POINT,
    CUSTOM
};

struct HandState {
    float fingerPos[5] = {0};   // 0.0=open, 1.0=closed per finger
    float wristAngle = 0.0f;
    bool connected = false;
    uint32_t lastUpdateMs = 0;
    int errorCount = 0;
};

struct HandCommand {
    uint8_t cmdType;     // 0x01=set_finger, 0x02=grip, 0x03=wrist, 0x04=query
    uint8_t fingerId;
    uint8_t position;    // 0-255
    uint8_t speed;       // 0-255
    uint16_t checksum;
};

class HandInterface {
public:
    HandInterface();
    ~HandInterface();

    bool initialize(const std::string& serialPort, int baudrate);

    // Individual finger control [0.0, 1.0]
    bool setFingerPosition(Finger finger, float position, float speed = 0.5f);

    // Predefined grips
    bool executeGrip(GripPattern pattern, float speed = 0.5f);

    // Wrist rotation [-90, 90] degrees
    bool setWristAngle(float degrees);

    // Open all fingers
    bool openHand(float speed = 0.5f);

    // Close all fingers
    bool closeHand(float speed = 0.5f);

    HandState getState() const;
    bool isConnected() const { return connected_; }

    // Auto-grip based on detection: grip when target is close enough
    bool autoGrip(float targetDistance, float gripThreshold = 0.3f);

    void release();

    using StateCallback = std::function<void(const HandState&)>;
    void setStateCallback(StateCallback cb);

private:
    bool openSerial(const std::string& port, int baudrate);
    void closeSerial();
    bool sendCommand(const HandCommand& cmd);
    bool readResponse(uint8_t* buf, int maxLen, int& bytesRead);
    void commLoop();
    uint16_t computeChecksum(const uint8_t* data, int len);

    int serialFd_ = -1;
    std::string port_;
    int baudrate_ = 115200;
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};

    HandState state_;
    mutable std::mutex stateMu_;

    // Command queue
    std::deque<HandCommand> cmdQueue_;
    std::mutex cmdMu_;

    std::thread commThread_;
    StateCallback stateCallback_;

    // Retry tracking
    int consecutiveErrors_ = 0;
    static constexpr int MAX_CONSECUTIVE_ERRORS = 10;

    // Periodic query counter (instance-level to avoid sharing across instances)
    int queryCount_ = 0;
};