#include "core/vision_system.h"
#include "utils/logger.h"
#include "utils/exception_handler.h"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <thread>
#include <atomic>

#ifdef __linux__
#include <unistd.h>
#include <sys/resource.h>
#include <sched.h>
#endif

static const char* MOD = "Main";
static std::atomic<bool> g_running{true};
static VisionSystem* g_system = nullptr;

void signalHandler(int sig) {
    LOG_I(MOD, "Signal %d received, shutting down...", sig);
    g_running = false;
    if (g_system) {
        g_system->stop();
    }
}

void printUsage(const char* prog) {
    printf("Usage: %s [options]\n", prog);
    printf("Options:\n");
    printf("  -c <config>    Config file (default: config.yaml)\n");
    printf("  -v             Verbose logging\n");
    printf("  -q             Quiet mode (errors only)\n");
    printf("  -d             Daemon mode\n");
    printf("  -p <priority>  Set process priority (-20 to 19)\n");
    printf("  -a <cpu>       Set CPU affinity (bitmask)\n");
    printf("  -h             Show this help\n");
    printf("\n");
    printf("Embedded Vision System for OV5640 Camera\n");
    printf("Build: %s %s\n", __DATE__, __TIME__);
}

bool setProcessPriority(int priority) {
#ifdef __linux__
    if (setpriority(PRIO_PROCESS, 0, priority) != 0) {
        LOG_W(MOD, "Failed to set priority %d: %s", priority, strerror(errno));
        return false;
    }
    LOG_I(MOD, "Process priority set to %d", priority);
    return true;
#else
    (void)priority;
    return false;
#endif
}

bool setCpuAffinity(unsigned int mask) {
#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    for (int i = 0; i < 32; ++i) {
        if (mask & (1u << i)) {
            CPU_SET(i, &cpuset);
        }
    }
    if (sched_setaffinity(0, sizeof(cpuset), &cpuset) != 0) {
        LOG_W(MOD, "Failed to set CPU affinity 0x%x: %s", mask, strerror(errno));
        return false;
    }
    LOG_I(MOD, "CPU affinity set to 0x%x", mask);
    return true;
#else
    (void)mask;
    return false;
#endif
}

bool daemonize() {
#ifdef __linux__
    pid_t pid = fork();
    if (pid < 0) {
        LOG_E(MOD, "Fork failed: %s", strerror(errno));
        return false;
    }
    if (pid > 0) {
        // Parent exits
        printf("Daemon started with PID %d\n", pid);
        _exit(0);
    }
    // Child continues
    setsid();

    // Redirect stdio to /dev/null
    freopen("/dev/null", "r", stdin);
    freopen("/var/log/vision_system.log", "a", stdout);
    freopen("/var/log/vision_system.log", "a", stderr);

    LOG_I(MOD, "Running as daemon, PID=%d", getpid());
    return true;
#else
    return false;
#endif
}

int main(int argc, char* argv[]) {
    std::string configFile = "config.yaml";
    int logLevel = 2; // INFO
    bool daemonMode = false;
    int priority = -10;
    unsigned int cpuAffinity = 0;
    bool setPrio = false;
    bool setAffin = false;

    // Parse arguments
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-c") == 0 && i + 1 < argc) {
            configFile = argv[++i];
        } else if (strcmp(argv[i], "-v") == 0) {
            logLevel = 0; // DEBUG
        } else if (strcmp(argv[i], "-q") == 0) {
            logLevel = 3; // ERROR
        } else if (strcmp(argv[i], "-d") == 0) {
            daemonMode = true;
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            priority = atoi(argv[++i]);
            setPrio = true;
        } else if (strcmp(argv[i], "-a") == 0 && i + 1 < argc) {
            cpuAffinity = (unsigned int)strtoul(argv[++i], nullptr, 0);
            setAffin = true;
        } else if (strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            printUsage(argv[0]);
            return 1;
        }
    }

    // Initialize logger
    Logger::instance().init(logLevel, "/var/log/vision_system.log", 4 * 1024 * 1024);

    LOG_I(MOD, "========================================");
    LOG_I(MOD, "  Embedded Vision System");
    LOG_I(MOD, "  Build: %s %s", __DATE__, __TIME__);
    LOG_I(MOD, "  Config: %s", configFile.c_str());
    LOG_I(MOD, "========================================");

    // Daemon mode
    if (daemonMode) {
        if (!daemonize()) {
            LOG_E(MOD, "Failed to daemonize");
            return 1;
        }
    }

    // Set process priority
    if (setPrio) {
        setProcessPriority(priority);
    }

    // Set CPU affinity
    if (setAffin) {
        setCpuAffinity(cpuAffinity);
    }

    // Install signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGHUP, signalHandler);
    signal(SIGPIPE, SIG_IGN);

    // Initialize exception handler
    ExceptionHandler::instance().init(
        "/var/log/vision_crash.log",
        [](const std::string& module, const std::string& msg) {
            LOG_E("CRASH", "[%s] %s", module.c_str(), msg.c_str());
        }
    );

    // Create and initialize vision system
    VisionSystem system;
    g_system = &system;

    if (!system.initialize(configFile)) {
        LOG_E(MOD, "System initialization failed!");
        return 1;
    }

    // Start the system
    system.start();

    LOG_I(MOD, "System running. Press Ctrl+C to stop.");

    // Main loop: monitor and handle commands
    while (g_running && system.isRunning()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        // Check for watchdog timeout
        auto stats = system.getStats();
        if (stats.processFps < 1.0f && stats.framesProcessed > 100) {
            LOG_W(MOD, "Low FPS detected: %.1f, possible hang", stats.processFps);
        }

        // Memory watchdog
        if (stats.memUsageMB > 300.0f) {
            LOG_E(MOD, "Memory usage critical: %.1f MB, restarting...", stats.memUsageMB);
            system.stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
            if (!system.initialize(configFile)) {
                LOG_E(MOD, "Re-initialization failed, exiting");
                break;
            }
            system.start();
        }
    }

    // Cleanup
    system.stop();
    g_system = nullptr;

    LOG_I(MOD, "System shutdown complete");
    Logger::instance().flush();

    return 0;
}