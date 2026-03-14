#pragma once
#include <cstdint>
#include <string>
#include <map>
#include <vector>
#include <mutex>
#include <chrono>

struct ProfileEntry {
    std::string name;
    float lastMs;
    float avgMs;
    float minMs;
    float maxMs;
    float totalMs;
    int count;
    float fps;
};

class Profiler {
public:
    static Profiler& instance();

    // Start timing a named section
    void begin(const std::string& name);

    // End timing
    void end(const std::string& name);

    // Get results
    ProfileEntry getEntry(const std::string& name) const;
    std::vector<ProfileEntry> getAllEntries() const;

    // Print formatted report
    void printReport() const;

    // Reset all counters
    void reset();

    // Get total elapsed time
    float getTotalElapsedMs() const;

    // Scoped timer helper
    class ScopedTimer {
    public:
        ScopedTimer(const std::string& name) : name_(name) {
            Profiler::instance().begin(name_);
        }
        ~ScopedTimer() {
            Profiler::instance().end(name_);
        }
    private:
        std::string name_;
    };

private:
    Profiler();

    struct TimerData {
        std::chrono::steady_clock::time_point startTime;
        float lastMs = 0;
        float avgMs = 0;
        float minMs = 1e9f;
        float maxMs = 0;
        float totalMs = 0;
        int count = 0;
        std::chrono::steady_clock::time_point lastUpdate;
        float fps = 0;
        int fpsCount = 0;
    };

    mutable std::mutex mu_;
    std::map<std::string, TimerData> timers_;
    std::chrono::steady_clock::time_point startupTime_;
};

// Convenience macros
#ifdef ENABLE_PROFILING
    // Use two-level macro expansion so __LINE__ is replaced with its value
    // before token-pasting, preventing variable name collisions on the same line.
    #define PROFILE_SCOPE_IMPL(name, line) Profiler::ScopedTimer _timer_##line(name)
    #define PROFILE_SCOPE(name) PROFILE_SCOPE_IMPL(name, __LINE__)
    #define PROFILE_BEGIN(name) Profiler::instance().begin(name)
    #define PROFILE_END(name)   Profiler::instance().end(name)
    #define PROFILE_REPORT()    Profiler::instance().printReport()
#else
    #define PROFILE_SCOPE(name)
    #define PROFILE_BEGIN(name)
    #define PROFILE_END(name)
    #define PROFILE_REPORT()
#endif