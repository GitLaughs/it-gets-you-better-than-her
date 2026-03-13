#!/bin/bash

# 构建目录
BUILD_DIR="build"

# 创建构建目录
mkdir -p $BUILD_DIR

echo "开始编译视觉系统..."

# 编译主程序
g++ -std=c++17 -Wall -Wextra -O3 -DNDEBUG -march=armv7-a -mfpu=neon -mfloat-abi=hard -pthread \
    -I src -I src/core -I src/utils -I src/config \
    src/main.cpp \
    src/core/camera_manager.cpp \
    src/core/hdr_controller.cpp \
    src/core/yolov8_detector.cpp \
    src/core/depth_estimator.cpp \
    src/core/tracker.cpp \
    src/core/obstacle_avoidance.cpp \
    src/core/point_cloud.cpp \
    src/core/position_estimator.cpp \
    src/core/hand_interface.cpp \
    src/core/video_output.cpp \
    src/core/vision_system.cpp \
    src/utils/logger.cpp \
    src/utils/exception_handler.cpp \
    src/utils/image_utils.cpp \
    src/utils/profiler.cpp \
    src/config/config_loader.cpp \
    -o $BUILD_DIR/vision_system \
    -lpthread -lm

# 编译测试程序
echo "编译测试程序..."
g++ -std=c++17 -Wall -Wextra -O3 -DNDEBUG -march=armv7-a -mfpu=neon -mfloat-abi=hard -pthread \
    -I src -I src/core -I src/utils -I src/config \
    tests/test_config.cpp \
    src/utils/logger.cpp \
    src/config/config_loader.cpp \
    -o $BUILD_DIR/test_config \
    -lpthread -lm

g++ -std=c++17 -Wall -Wextra -O3 -DNDEBUG -march=armv7-a -mfpu=neon -mfloat-abi=hard -pthread \
    -I src -I src/core -I src/utils -I src/config \
    tests/test_tracker.cpp \
    src/core/tracker.cpp \
    src/utils/logger.cpp \
    -o $BUILD_DIR/test_tracker \
    -lpthread -lm

g++ -std=c++17 -Wall -Wextra -O3 -DNDEBUG -march=armv7-a -mfpu=neon -mfloat-abi=hard -pthread \
    -I src -I src/core -I src/utils -I src/config \
    tests/test_obstacle.cpp \
    src/core/obstacle_avoidance.cpp \
    src/core/depth_estimator.cpp \
    src/utils/logger.cpp \
    -o $BUILD_DIR/test_obstacle \
    -lpthread -lm

echo "编译完成！"
echo "可执行文件位于: $BUILD_DIR/"
echo "- 主程序: $BUILD_DIR/vision_system"
echo "- 测试程序: $BUILD_DIR/test_config, $BUILD_DIR/test_tracker, $BUILD_DIR/test_obstacle"

echo "\n注意：原有的Demo编译程序保持不变，位于 /app/smartsens_sdk/scripts/ 目录下"
echo "- a1_sc132gs_build.sh: 对应使用 sc132gs 图像传感器的赛题编译脚本"
echo "- build_app.sh: 应用程序编译脚本"
echo "- build_release_sdk.sh: SDK 发布编译脚本"
