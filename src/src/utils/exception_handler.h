#pragma once
#include <string>
#include <vector>
#include <map>
#include <mutex>
#include <functional>
#include <chrono>
#include <cstring>
#include <atomic>

enum class Severity {
    SEV_DEBUG = 0,
    SEV_INFO,
    SEV_WARNING,
    SEV_ERROR,
    SEV_CRITICAL,
    SEV_FATAL
};

enum class RecoveryAction {
    IGNORE,
    RETRY,
    RESTART_MODULE,
    RESTART_SYSTEM,
    EMERGENCY_STOP,
    LOG_ONLY
};

struct ExceptionRecord {
    std::string exceptionType;
    std::string message;
    Severity severity;
    std::string module;
    double timestamp;
    RecoveryAction action;
    bool resolved = false;
    int retryCount = 0;
};

struct ExceptionRule {
    Severity severity = Severity::SEV_ERROR;
    RecoveryAction action = RecoveryAction::LOG_ONLY;
    int maxRetries = 3;
    float retryDelayMs = 1000.0f;
    float cooldownMs = 0.0f;
};

class ExceptionHandler {
public:
    static ExceptionHandler& instance();

    void init(int maxRecords = 1000);

    void addRule(const std::string& name, const ExceptionRule& rule);

    RecoveryAction handle(const std::string& exType, const std::string& msg,
                          const std::string& module,
                          Severity severity = Severity::SEV_ERROR);

    // Convenience: handle with automatic exception type
    RecoveryAction handleException(const std::exception& e,
                                    const std::string& module);

    void setEmergencyCallback(std::function<void()> cb);
    void setRestartCallback(std::function<void()> cb);

    struct Statistics {
        int total = 0;
        std::map<std::string, int> byModule;
        std::map<int, int> bySeverity;
    };

    Statistics getStatistics() const;
    void clear();

    // Watchdog
    void kickWatchdog();
    bool isWatchdogExpired(int timeoutMs) const;

private:
    ExceptionHandler() = default;

    const ExceptionRule* findRule(const std::string& module) const;
    void executeRecovery(RecoveryAction action, const std::string& module);

    std::vector<ExceptionRecord> records_;
    std::map<std::string, ExceptionRule> rules_;
    std::map<std::string, int> exceptionCounts_;
    std::map<std::string, double> lastExceptionTime_;
    int maxRecords_ = 1000;

    std::function<void()> emergencyCallback_;
    std::function<void()> restartCallback_;

    mutable std::mutex mu_;

    std::atomic<int64_t> lastWatchdogKick_{0};
};

// Helper: safe execution with retry
template<typename Func>
auto safeExecute(Func&& func, const std::string& module,
                 int maxRetries = 0, int retryDelayMs = 100)
    -> decltype(func()) {
    using RetType = decltype(func());
    for (int attempt = 0; attempt <= maxRetries; ++attempt) {
        try {
            return func();
        } catch (const std::exception& e) {
            auto action = ExceptionHandler::instance().handleException(e, module);
            if (action == RecoveryAction::EMERGENCY_STOP) {
                throw;
            }
            if (attempt < maxRetries) {
                std::this_thread::sleep_for(std::chrono::milliseconds(retryDelayMs));
            }
        }
    }
    if constexpr (std::is_same_v<RetType, void>) {
        return;
    } else {
        return RetType{};
    }
}