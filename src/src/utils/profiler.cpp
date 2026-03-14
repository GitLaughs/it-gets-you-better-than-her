#include "profiler.h"
#include "logger.h"

#include <cstdio>
#include <algorithm>

static const char* MOD = "Profiler";

Profiler& Profiler::instance() {
    static Profiler inst;
    return inst;
}

Profiler::Profiler() {
    startupTime_ = std::chrono::steady_clock::now();
}

void Profiler::begin(const std::string& name) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& t = timers_[name];
    t.startTime = std::chrono::steady_clock::now();
}

void Profiler::end(const std::string& name) {
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lk(mu_);
    auto it = timers_.find(name);
    if (it == timers_.end()) return;

    auto& t = it->second;
    float ms = std::chrono::duration<float, std::milli>(now - t.startTime).count();

    t.lastMs = ms;
    t.totalMs += ms;
    t.count++;

    // Exponential moving average
    if (t.count == 1) {
        t.avgMs = ms;
    } else {
        t.avgMs = t.avgMs * 0.95f + ms * 0.05f;
    }

    t.minMs = std::min(t.minMs, ms);
    t.maxMs = std::max(t.maxMs, ms);

    // FPS calculation
    t.fpsCount++;
    float elapsed = std::chrono::duration<float>(now - t.lastUpdate).count();
    if (elapsed >= 1.0f) {
        t.fps = t.fpsCount / elapsed;
        t.fpsCount = 0;
        t.lastUpdate = now;
    }
}

ProfileEntry Profiler::getEntry(const std::string& name) const {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = timers_.find(name);
    if (it == timers_.end()) {
        return ProfileEntry{name, 0, 0, 0, 0, 0, 0, 0};
    }

    auto& t = it->second;
    return ProfileEntry{
        name, t.lastMs, t.avgMs, t.minMs, t.maxMs, t.totalMs, t.count, t.fps
    };
}

std::vector<ProfileEntry> Profiler::getAllEntries() const {
    std::lock_guard<std::mutex> lk(mu_);
    std::vector<ProfileEntry> entries;
    entries.reserve(timers_.size());

    for (auto& [name, t] : timers_) {
        entries.push_back({
            name, t.lastMs, t.avgMs, t.minMs, t.maxMs, t.totalMs, t.count, t.fps
        });
    }

    // Sort by total time descending
    std::sort(entries.begin(), entries.end(),
              [](const ProfileEntry& a, const ProfileEntry& b) {
                  return a.totalMs > b.totalMs;
              });

    return entries;
}

void Profiler::printReport() const {
    auto entries = getAllEntries();

    LOG_I(MOD, "╔═══════════════════════════════════════════════════════════════════════════╗");
    LOG_I(MOD, "║                         PERFORMANCE PROFILE REPORT                       ║");
    LOG_I(MOD, "╠══════════════════════╦════════╦════════╦════════╦════════╦═══════╦════════╣");
    LOG_I(MOD, "║ Section              ║ Last   ║ Avg    ║ Min    ║ Max    ║ Count ║ FPS    ║");
    LOG_I(MOD, "╠══════════════════════╬════════╬════════╬════════╬════════╬═══════╬════════╣");

    for (auto& e : entries) {
        char line[256];
        snprintf(line, sizeof(line),
                 "║ %-20s ║ %6.2f ║ %6.2f ║ %6.2f ║ %6.2f ║ %5d ║ %6.1f ║",
                 e.name.c_str(), e.lastMs, e.avgMs, e.minMs, e.maxMs,
                 e.count, e.fps);
        LOG_I(MOD, "%s", line);
    }

    LOG_I(MOD, "╚══════════════════════╩════════╩════════╩════════╩════════╩═══════╩════════╝");

    float totalElapsed = getTotalElapsedMs();
    LOG_I(MOD, "Total elapsed: %.1f ms (%.1f s)", totalElapsed, totalElapsed / 1000.0f);
}

void Profiler::reset() {
    std::lock_guard<std::mutex> lk(mu_);
    timers_.clear();
    startupTime_ = std::chrono::steady_clock::now();
    LOG_I(MOD, "Profiler reset");
}

float Profiler::getTotalElapsedMs() const {
    std::lock_guard<std::mutex> lk(mu_);
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration<float, std::milli>(now - startupTime_).count();
}