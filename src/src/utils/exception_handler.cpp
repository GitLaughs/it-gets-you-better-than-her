#include "exception_handler.h"
#include "logger.h"
#include <thread>

static const char* MOD = "ExceptionHandler";

static double nowSec() {
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(now.time_since_epoch()).count();
}

ExceptionHandler& ExceptionHandler::instance() {
    static ExceptionHandler inst;
    return inst;
}

void ExceptionHandler::init(int maxRecords) {
    std::lock_guard<std::mutex> lk(mu_);
    maxRecords_ = maxRecords;
    records_.clear();

    // Default rules
    addRule("camera", {Severity::SEV_ERROR, RecoveryAction::RETRY, 5, 2000});
    addRule("detection", {Severity::SEV_WARNING, RecoveryAction::RETRY, 3, 500});
    addRule("depth", {Severity::SEV_WARNING, RecoveryAction::RETRY, 3, 500});
    addRule("tracking", {Severity::SEV_WARNING, RecoveryAction::LOG_ONLY, 0, 0});
    addRule("navigation", {Severity::SEV_WARNING, RecoveryAction::LOG_ONLY, 0, 0});
    addRule("hand", {Severity::SEV_WARNING, RecoveryAction::LOG_ONLY, 0, 0});
    addRule("system", {Severity::SEV_CRITICAL, RecoveryAction::RESTART_SYSTEM, 1, 5000});
    addRule("default", {Severity::SEV_ERROR, RecoveryAction::LOG_ONLY, 0, 0});

    lastWatchdogKick_.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
    );

    LOG_I(MOD, "Exception handler initialized (max_records=%d)", maxRecords);
}

void ExceptionHandler::addRule(const std::string& name, const ExceptionRule& rule) {
    rules_[name] = rule;
}

const ExceptionRule* ExceptionHandler::findRule(const std::string& module) const {
    auto it = rules_.find(module);
    if (it != rules_.end()) return &it->second;
    it = rules_.find("default");
    if (it != rules_.end()) return &it->second;
    return nullptr;
}

RecoveryAction ExceptionHandler::handle(const std::string& exType,
                                         const std::string& msg,
                                         const std::string& module,
                                         Severity severity) {
    const ExceptionRule* rule = findRule(module);
    RecoveryAction action = rule ? rule->action : RecoveryAction::LOG_ONLY;
    Severity sev = rule ? rule->severity : severity;

    std::string key = module + ":" + exType;

    ExceptionRecord rec;
    rec.exceptionType = exType;
    rec.message = msg;
    rec.severity = sev;
    rec.module = module;
    rec.timestamp = nowSec();
    rec.action = action;

    // Cooldown check and record update under a single lock to prevent TOCTOU race.
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (rule && rule->cooldownMs > 0) {
            auto it = lastExceptionTime_.find(key);
            if (it != lastExceptionTime_.end()) {
                if ((rec.timestamp - it->second) * 1000.0 < rule->cooldownMs) {
                    return RecoveryAction::IGNORE;
                }
            }
        }
        records_.push_back(rec);
        if ((int)records_.size() > maxRecords_) {
            records_.erase(records_.begin());
        }
        exceptionCounts_[key]++;
        lastExceptionTime_[key] = rec.timestamp;
    }

    // Log (outside the lock to avoid blocking other threads while doing I/O)
    switch (sev) {
        case Severity::SEV_CRITICAL:
        case Severity::SEV_FATAL:
            LOG_C(module.c_str(), "[%s] %s", exType.c_str(), msg.c_str());
            break;
        case Severity::SEV_ERROR:
            LOG_E(module.c_str(), "[%s] %s", exType.c_str(), msg.c_str());
            break;
        case Severity::SEV_WARNING:
            LOG_W(module.c_str(), "[%s] %s", exType.c_str(), msg.c_str());
            break;
        default:
            LOG_I(module.c_str(), "[%s] %s", exType.c_str(), msg.c_str());
            break;
    }

    executeRecovery(action, module);
    return action;
}

RecoveryAction ExceptionHandler::handleException(const std::exception& e,
                                                   const std::string& module) {
    return handle(typeid(e).name(), e.what(), module);
}

void ExceptionHandler::executeRecovery(RecoveryAction action,
                                        const std::string& module) {
    if (action == RecoveryAction::EMERGENCY_STOP) {
        LOG_C(MOD, "EMERGENCY STOP triggered by module: %s", module.c_str());
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lk(mu_);
            cb = emergencyCallback_;
        }
        if (cb) cb();
    } else if (action == RecoveryAction::RESTART_SYSTEM) {
        LOG_C(MOD, "SYSTEM RESTART requested by module: %s", module.c_str());
        std::function<void()> cb;
        {
            std::lock_guard<std::mutex> lk(mu_);
            cb = restartCallback_;
        }
        if (cb) cb();
    }
}

void ExceptionHandler::setEmergencyCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mu_);
    emergencyCallback_ = std::move(cb);
}

void ExceptionHandler::setRestartCallback(std::function<void()> cb) {
    std::lock_guard<std::mutex> lk(mu_);
    restartCallback_ = std::move(cb);
}

ExceptionHandler::Statistics ExceptionHandler::getStatistics() const {
    std::lock_guard<std::mutex> lk(mu_);
    Statistics stats;
    stats.total = (int)records_.size();
    for (auto& r : records_) {
        stats.byModule[r.module]++;
        stats.bySeverity[(int)r.severity]++;
    }
    return stats;
}

void ExceptionHandler::clear() {
    std::lock_guard<std::mutex> lk(mu_);
    records_.clear();
    exceptionCounts_.clear();
}

void ExceptionHandler::kickWatchdog() {
    lastWatchdogKick_.store(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count()
    );
}

bool ExceptionHandler::isWatchdogExpired(int timeoutMs) const {
    auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    return (nowMs - lastWatchdogKick_.load()) > timeoutMs;
}