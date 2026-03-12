cmake_minimum_required(VERSION 3.10)
project(EmbeddedVisionSystem VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=armv7-a -mfpu=neon -mfloat-abi=hard")
set(CMAKE_CXX_FLAGS_DEBUG "-O0 -g -DDEBUG")

# Cross-compilation support
if(DEFINED ENV{CROSS_COMPILE})
    set(CMAKE_C_COMPILER "$ENV{CROSS_COMPILE}gcc")
    set(CMAKE_CXX_COMPILER "$ENV{CROSS_COMPILE}g++")
    message(STATUS "Cross-compiling with: $ENV{CROSS_COMPILE}")
endif()

# Options
option(USE_ONNXRUNTIME "Enable ONNX Runtime for ML inference" OFF)
option(USE_NEON "Enable ARM NEON optimizations" ON)
option(BUILD_TESTS "Build test executables" OFF)
option(ENABLE_PROFILING "Enable performance profiling" OFF)

if(USE_NEON)
    add_definitions(-DUSE_NEON)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -mfpu=neon")
endif()

if(ENABLE_PROFILING)
    add_definitions(-DENABLE_PROFILING)
endif()

# Source files
set(CORE_SOURCES
    src/core/camera_manager.cpp
    src/core/hdr_controller.cpp
    src/core/yolov8_detector.cpp
    src/core/depth_estimator.cpp
    src/core/tracker.cpp
    src/core/obstacle_avoidance.cpp
    src/core/point_cloud.cpp
    src/core/position_estimator.cpp
    src/core/hand_interface.cpp
    src/core/video_output.cpp
    src/core/vision_system.cpp
)

set(UTILS_SOURCES
    src/utils/logger.cpp
    src/utils/exception_handler.cpp
    src/utils/image_utils.cpp
    src/utils/profiler.cpp
)

set(CONFIG_SOURCES
    src/config/config_loader.cpp
)

set(ALL_SOURCES
    src/main.cpp
    ${CORE_SOURCES}
    ${UTILS_SOURCES}
    ${CONFIG_SOURCES}
)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/core
    ${CMAKE_SOURCE_DIR}/src/utils
    ${CMAKE_SOURCE_DIR}/src/config
)

# Main executable
add_executable(vision_system ${ALL_SOURCES})

# Link libraries
target_link_libraries(vision_system
    pthread
    m
)

# V4L2 (always available on Linux)
if(UNIX)
    # V4L2 is part of libc on Linux, no extra linking needed
    target_compile_definitions(vision_system PRIVATE __linux__)
endif()

# ONNX Runtime (optional)
if(USE_ONNXRUNTIME)
    find_path(ONNXRUNTIME_INCLUDE onnxruntime_cxx_api.h
        HINTS
            /usr/local/include/onnxruntime
            /opt/onnxruntime/include
            $ENV{ONNXRUNTIME_ROOT}/include
    )
    find_library(ONNXRUNTIME_LIB onnxruntime
        HINTS
            /usr/local/lib
            /opt/onnxruntime/lib
            $ENV{ONNXRUNTIME_ROOT}/lib
    )

    if(ONNXRUNTIME_INCLUDE AND ONNXRUNTIME_LIB)
        target_include_directories(vision_system PRIVATE ${ONNXRUNTIME_INCLUDE})
        target_link_libraries(vision_system ${ONNXRUNTIME_LIB})
        target_compile_definitions(vision_system PRIVATE USE_ONNXRUNTIME)
        message(STATUS "ONNX Runtime found: ${ONNXRUNTIME_LIB}")
    else()
        message(WARNING "ONNX Runtime not found, ML inference will use fallback")
    endif()
endif()

# Install
install(TARGETS vision_system DESTINATION /opt/vision/bin)
install(FILES config.yaml DESTINATION /opt/vision/etc)
install(DIRECTORY DESTINATION /opt/vision/models)
install(DIRECTORY DESTINATION /opt/vision/log)

# Tests
if(BUILD_TESTS)
    enable_testing()

    add_executable(test_config
        tests/test_config.cpp
        ${CONFIG_SOURCES}
        ${UTILS_SOURCES}
    )
    target_link_libraries(test_config pthread m)
    add_test(NAME test_config COMMAND test_config)

    add_executable(test_tracker
        tests/test_tracker.cpp
        src/core/tracker.cpp
        ${UTILS_SOURCES}
    )
    target_link_libraries(test_tracker pthread m)
    add_test(NAME test_tracker COMMAND test_tracker)

    add_executable(test_obstacle
        tests/test_obstacle.cpp
        src/core/obstacle_avoidance.cpp
        ${UTILS_SOURCES}
    )
    target_link_libraries(test_obstacle pthread m)
    add_test(NAME test_obstacle COMMAND test_obstacle)
endif()

# Print summary
message(STATUS "===========================================")
message(STATUS "  Embedded Vision System Build Config")
message(STATUS "  ONNX Runtime: ${USE_ONNXRUNTIME}")
message(STATUS "  NEON:         ${USE_NEON}")
message(STATUS "  Tests:        ${BUILD_TESTS}")
message(STATUS "  Profiling:    ${ENABLE_PROFILING}")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "===========================================")