#include "logger.h"
#include <cstdio>
#include <chrono>
#include <iomanip>

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lk(mu_);
    if (fileStream_.is_open()) {
        fileStream_.close();
    }
}

void Logger::ensureDir(const std::string& path) {
    mkdir(path.c_str(), 0755);
}

void Logger::init(const std::string& name, LogLevel level,
                  const std::string& logDir, bool console, bool file) {
    std::lock_guard<std::mutex> lk(mu_);
    if (initialized_) return;

    name_ = name;
    level_ = level;
    console_ = console;
    file_ = file;

    if (file_) {
        ensureDir(logDir);
        auto now = std::chrono::system_clock::now();
        auto t = std::chrono::system_clock::to_time_t(now);
        struct tm tm_buf;
        localtime_r(&t, &tm_buf);
        char dateBuf[32];
        strftime(dateBuf, sizeof(dateBuf), "%Y%m%d", &tm_buf);

        std::string filepath = logDir + "/" + name + "_" + dateBuf + ".log";
        fileStream_.open(filepath, std::ios::app);
        if (!fileStream_.is_open()) {
            std::cerr << "[Logger] Cannot open log file: " << filepath << std::endl;
            file_ = false;
        }
    }

    initialized_ = true;
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lk(mu_);
    level_ = level;
}

std::string Logger::levelString(LogLevel level) const {
    switch (level) {
        case LogLevel::LOG_DEBUG:    return "DEBUG   ";
        case LogLevel::LOG_INFO:     return "INFO    ";
        case LogLevel::LOG_WARNING:  return "WARNING ";
        case LogLevel::LOG_ERROR:    return "ERROR   ";
        case LogLevel::LOG_CRITICAL: return "CRITICAL";
    }
    return "UNKNOWN ";
}

std::string Logger::colorCode(LogLevel level) const {
    switch (level) {
        case LogLevel::LOG_DEBUG:    return "\033[37m";    // white
        case LogLevel::LOG_INFO:     return "\033[32m";    // green
        case LogLevel::LOG_WARNING:  return "\033[33m";    // yellow
        case LogLevel::LOG_ERROR:    return "\033[31m";    // red
        case LogLevel::LOG_CRITICAL: return "\033[1;31m";  // bold red
    }
    return "\033[0m";
}

std::string Logger::timestamp() const {
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    struct tm tm_buf;
    localtime_r(&t, &tm_buf);
    char buf[64];
    strftime(buf, sizeof(buf), "%H:%M:%S", &tm_buf);
    char result[80];
    snprintf(result, sizeof(result), "%s.%03d", buf, (int)ms.count());
    return std::string(result);
}

void Logger::log(LogLevel level, const char* module, const char* fmt, va_list args) {
    if (level < level_) return;

    char msgBuf[2048];
    vsnprintf(msgBuf, sizeof(msgBuf), fmt, args);

    std::lock_guard<std::mutex> lk(mu_);

    // Generate timestamp inside the lock so log entries are ordered correctly.
    std::string ts = timestamp();
    std::string lev = levelString(level);

    if (console_) {
        fprintf(stdout, "%s[%s] [%s] [%s]%s %s\n",
                colorCode(level).c_str(), ts.c_str(), lev.c_str(),
                module, "\033[0m", msgBuf);
        fflush(stdout);
    }

    if (file_ && fileStream_.is_open()) {
        fileStream_ << "[" << ts << "] [" << lev << "] [" << module << "] "
                     << msgBuf << "\n";
        fileStream_.flush();
    }
}

void Logger::debug(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::LOG_DEBUG, module, fmt, args);
    va_end(args);
}

void Logger::info(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::LOG_INFO, module, fmt, args);
    va_end(args);
}

void Logger::warning(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::LOG_WARNING, module, fmt, args);
    va_end(args);
}

void Logger::error(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::LOG_ERROR, module, fmt, args);
    va_end(args);
}

void Logger::critical(const char* module, const char* fmt, ...) {
    va_list args; va_start(args, fmt);
    log(LogLevel::LOG_CRITICAL, module, fmt, args);
    va_end(args);
}

void Logger::flush() {
    std::lock_guard<std::mutex> lk(mu_);
    if (console_) fflush(stdout);
    if (file_ && fileStream_.is_open()) fileStream_.flush();
}