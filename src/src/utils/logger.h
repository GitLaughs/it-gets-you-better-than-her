#pragma once
#include <string>
#include <fstream>
#include <mutex>
#include <memory>
#include <cstdarg>
#include <ctime>
#include <iostream>
#include <sstream>
#include <sys/stat.h>
#include <cstring>

enum class LogLevel {
    LOG_DEBUG = 0,
    LOG_INFO,
    LOG_WARNING,
    LOG_ERROR,
    LOG_CRITICAL
};

class Logger {
public:
    static Logger& instance();

    void init(const std::string& name, LogLevel level,
              const std::string& logDir = "/tmp/a1_logs",
              bool console = true, bool file = true);

    LogLevel getLevel() const { return level_; }
    void setLevel(LogLevel level);

    void flush();

    void debug(const char* module, const char* fmt, ...);
    void info(const char* module, const char* fmt, ...);
    void warning(const char* module, const char* fmt, ...);
    void error(const char* module, const char* fmt, ...);
    void critical(const char* module, const char* fmt, ...);

    void log(LogLevel level, const char* module, const char* fmt, va_list args);

private:
    Logger() = default;
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    std::string levelString(LogLevel level) const;
    std::string colorCode(LogLevel level) const;
    std::string timestamp() const;
    void ensureDir(const std::string& path);

    LogLevel level_ = LogLevel::LOG_INFO;
    bool console_ = true;
    bool file_ = true;
    std::ofstream fileStream_;
    std::mutex mu_;
    bool initialized_ = false;
    std::string name_;
};

// Convenience macros
#define LOG_D(mod, fmt, ...) Logger::instance().debug(mod, fmt, ##__VA_ARGS__)
#define LOG_I(mod, fmt, ...) Logger::instance().info(mod, fmt, ##__VA_ARGS__)
#define LOG_W(mod, fmt, ...) Logger::instance().warning(mod, fmt, ##__VA_ARGS__)
#define LOG_E(mod, fmt, ...) Logger::instance().error(mod, fmt, ##__VA_ARGS__)
#define LOG_C(mod, fmt, ...) Logger::instance().critical(mod, fmt, ##__VA_ARGS__)