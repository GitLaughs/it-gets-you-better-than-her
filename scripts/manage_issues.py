#!/usr/bin/env python3
"""
GitHub Issues 管理脚本 — Bug 修复收尾 + 维护分配 + 扩展分配
========================================================
功能：
  1. 关闭已修复的 Bug Issues（添加修复摘要注释后关闭）
  2. 创建 src 目录代码维护分配 Issues（成员A/B/C）
  3. 创建外设扩展功能分配 Issues（基于引脚分配方案）

用法：
    export GITHUB_TOKEN="ghp_xxxxx"
    python3 scripts/manage_issues.py

    # 仅关闭 Bug Issues：
    python3 scripts/manage_issues.py --close-bugs

    # 仅创建维护 Issues：
    python3 scripts/manage_issues.py --create-maintenance

    # 仅创建扩展 Issues：
    python3 scripts/manage_issues.py --create-extensions

    # 全部执行：
    python3 scripts/manage_issues.py --all
"""
import os
import sys
import json
import argparse
import urllib.request
import urllib.error

REPO = "GitLaughs/it-gets-you-better-than-her"
API_BASE = f"https://api.github.com/repos/{REPO}"

# ═══════════════════════════════════════════════════════════════════════════
# 团队成员 GitHub 用户名（根据实际情况修改）
# ═══════════════════════════════════════════════════════════════════════════
MEMBER_A = "MemberA"   # 队长：核心架构 + 检测 + 跟踪 + 摄像头管理 + 系统集成
MEMBER_B = "MemberB"   # 队友B：深度估计 + 点云 + 定位导航 + 避障算法
MEMBER_C = "MemberC"   # 队友C：视频输出 + 灵巧手 + HDR控制 + 异常处理


# ═══════════════════════════════════════════════════════════════════════════
# 1. 已修复的 Bug Issues（GitHub issue 编号 → 修复摘要）
# ═══════════════════════════════════════════════════════════════════════════
FIXED_BUG_ISSUES = [
    {
        "number": 26,
        "comment": """## ✅ Bug 已修复 — 信号处理非异步安全问题

**修复摘要 (对应内部 Bug #15)**

| 项目 | 内容 |
|------|------|
| 文件 | `src/src/main.cpp` |
| 问题 | 信号处理函数中调用 `stop()`，涉及复杂对象操作，属未定义行为 (UB) |
| 修复 | 信号处理函数只设 `g_running = false`，不再调用非异步信号安全的函数；`stop()` 移到主循环结束后调用 |
| 验证 | 编译通过；信号处理符合 POSIX 异步信号安全规范 |

此 Issue 对应的代码修复已完成，关闭。"""
    },
    {
        "number": 29,
        "comment": """## ✅ Bug 已修复 — VisionSystem 编译错误

**修复摘要 (对应内部 Bug #7, #8, #11)**

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| #7 | `vision_system.cpp` | 调用了 `captureFrame()`，但 `camera_manager.h` 中该方法不存在 | 改为调用正确的 `getFrame()` |
| #8 | `vision_system.h` | `BBox` 类型未定义，缺少头文件包含 | 添加 `#include "tracker.h"` |
| #11 | `exception_handler.h` | `safeExecute` 使用了 `std::thread` 但缺少头文件 | 添加 `#include <thread>` |

所有编译错误均已修复，代码可正常构建。关闭此 Issue。"""
    },
    {
        "number": 30,
        "comment": """## ✅ Bug 已修复 — 双缓冲机制数据竞态 + adaptHDR 空实现

**修复摘要 (对应内部 Bug #14, #32)**

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| #14 | `vision_system.cpp` | `processLoop` 读 `readIdx_` 时两个原子写非原子组合，存在数据竞态 | 读取 `readIdx_` 时加 `frameMu_` 互斥锁，防止双原子写可见性竞态 |
| #32 | `vision_system.cpp` | `adaptHDR()` 是空壳函数，`HDRController` 未实例化，HDR 功能完全失效 | 在 `adaptHDR()` 中实际调用 `HDRController`，完成真正的 HDR 自适应逻辑 |

关闭此 Issue。"""
    },
    {
        "number": 39,
        "comment": """## ✅ Bug 已修复 — point_cloud 无锁引用返回

**修复摘要 (对应内部 Bug #16)**

| 项目 | 内容 |
|------|------|
| 文件 | `src/src/core/point_cloud.cpp` / `point_cloud.h` |
| 问题 | `getPoints()` 返回内部 `vector` 引用，无任何锁保护，多线程下存在数据竞态 |
| 修复 | `getPoints()` 改为持锁后返回 `vector` 副本，不再返回内部引用 |
| 验证 | 调用方不再持有悬空引用；线程安全 |

关闭此 Issue。"""
    },
    {
        "number": 42,
        "comment": """## ✅ Bug 已修复 — obstacle_avoidance 边界扇区被跳过

**修复摘要 (对应内部 Bug #31)**

| 项目 | 内容 |
|------|------|
| 文件 | `src/src/core/obstacle_avoidance.cpp` |
| 问题 | `findBestDirection()` 中边界扇区（索引 0 和最后一个扇区）被 `if` 逻辑跳过，导致边界方向永远不被评估 |
| 修复 | 修正边界条件判断，确保所有扇区（包括边界扇区）均参与最优方向计算 |
| 验证 | 单元测试 `tests/test_obstacle.cpp` 全部通过 |

关闭此 Issue。"""
    },
    {
        "number": 44,
        "comment": """## ✅ Bug 已修复 — hdr_controller 线程安全问题

**修复摘要 (对应内部 Bug #17, #25)**

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| #17 | `hdr_controller.cpp/h` | `active_` 和 `lastSwitchTimeMs_` 读写锁保护不一致，存在数据竞态 | `active_` 和 `lastSwitchTimeMs_` 均改为 `std::atomic`，保证原子性 |
| #25 | `hdr_controller.cpp` | `computePercentiles()` 用 `p5==0` 判断全黑帧，当真实 p5 为 0 时产生误判 | 改用布尔标志变量替换 `p5==0` 判断，正确处理全黑帧 |

关闭此 Issue。"""
    },
    {
        "number": 45,
        "comment": """## ✅ Bug 已修复 — hand_interface 线程安全 + static 成员问题

**修复摘要 (对应内部 Bug #19, #46)**

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| #19 | `hand_interface.cpp` | `stateCallback_` (`std::function`) 跨线程读写无锁保护 | `stateCallback_` 的读写均由 `stateMu_` 互斥锁保护；`setStateCallback()` 加锁 |
| #46 | `hand_interface.cpp/h` | `static int queryCount` 为类静态成员，多实例共享同一计数器，计数错误 | 改为实例成员 `queryCount_`，每个实例独立维护计数 |

关闭此 Issue。"""
    },
    {
        "number": 46,
        "comment": """## ✅ Bug 已修复 — exception_handler 回调死锁 + TOCTOU 竞态

**修复摘要 (对应内部 Bug #18, #20)**

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| #18 | `exception_handler.cpp` | 持锁期间直接调用用户回调，若回调内部再次加锁则死锁 | 先加锁拷贝回调函数，再在锁外调用，避免在锁内执行用户回调 |
| #20 | `exception_handler.cpp` | 冷却时间检测（读取时间）与更新（写入时间）分两次加锁，存在 TOCTOU 竞态 | 将检测与更新合并到单次加锁中，消除 TOCTOU 竞态 |

关闭此 Issue。"""
    },
    {
        "number": 47,
        "comment": """## ✅ Bug 已修复 — image_utils 多项逻辑 Bug

**修复摘要 (对应内部 Bug #26, #27, #28)**

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| #26 | `image_utils.cpp` | `resizeBilinear()` 目标宽/高为 1 时除以 `(dstW-1)=0`，触发除零错误 | 目标尺寸为 1 时特殊处理，跳过除零路径 |
| #27 | `image_utils.cpp` | `bilateralFilter()` 左右边界列未做拷贝，输出图像边界为未初始化数据 | 补全左右边界列的像素拷贝逻辑 |
| #28 | `image_utils.cpp` | `adaptiveThreshold()` 重复计算了同一积分图，浪费 3.5MB 内存和 CPU | 删除重复的积分图计算，仅保留一次 |

关闭此 Issue。"""
    },
    {
        "number": 48,
        "comment": """## ✅ Bug 已修复 — logger 线程安全问题

**修复摘要 (对应内部 Bug #21, #22)**

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| #21 | `logger.cpp` | 析构函数关闭文件前未加锁，若另一线程正在写日志则产生竞态 | 析构函数在关闭文件前先加 `logMu_` 互斥锁 |
| #22 | `logger.cpp` | 时间戳在锁外生成，多线程并发写入时时间戳顺序与实际日志顺序不一致 | 时间戳改为在锁内生成，保证日志顺序与时间戳一致 |

关闭此 Issue。"""
    },
    {
        "number": 49,
        "comment": """## ✅ Bug 已修复 — profiler 宏展开 + 线程安全

**修复摘要 (对应内部 Bug #10, #23)**

| # | 文件 | 问题 | 修复 |
|---|------|------|------|
| #10 | `profiler.h` | `PROFILE_SCOPE` 宏直接拼接 `__LINE__`，未触发二级展开，导致变量名冲突（`timer__LINE__`） | 使用二级宏展开（`CONCAT_IMPL` + `CONCAT`）确保 `__LINE__` 被替换为实际行号 |
| #23 | `profiler.cpp` | `getTotalElapsedMs()` 读取 `startupTime_` 前未加锁，存在数据竞态 | 读取 `startupTime_` 前加 `profMu_` 互斥锁 |

关闭此 Issue。"""
    },
]

# ═══════════════════════════════════════════════════════════════════════════
# 2. 新建：src 目录代码维护分配 Issues
# ═══════════════════════════════════════════════════════════════════════════
MAINTENANCE_ISSUES = [
    {
        "title": "【成员A/队长】维护责任分配 — 核心系统层 (feature/core-system)",
        "assignees": [MEMBER_A],
        "labels": ["maintenance", "core", "成员A"],
        "body": """## 📋 维护职责

成员A（队长）负责维护 `src/` 目录下**核心系统层**的所有代码，确保模块持续可用、测试覆盖完整。

## 📁 负责文件清单

### 主程序
| 文件 | 说明 |
|------|------|
| `src/src/main.cpp` | 程序入口、信号处理、主循环调度 |

### 核心感知模块
| 文件 | 说明 |
|------|------|
| `src/src/core/vision_system.h` | VisionSystem 类声明 |
| `src/src/core/vision_system.cpp` | 主处理流水线、双缓冲、HDR 自适应 |
| `src/src/core/camera_manager.h` | 摄像头管理类声明 |
| `src/src/core/camera_manager.cpp` | SSNE sensor 初始化、帧获取 |
| `src/src/core/yolov8_detector.h` | YOLOv8 检测器类声明 |
| `src/src/core/yolov8_detector.cpp` | NPU 推理、NMS、结果解析 |
| `src/src/core/tracker.h` | 目标跟踪器类声明（含 BBox 定义） |
| `src/src/core/tracker.cpp` | 多目标跟踪算法实现 |

### 配置模块
| 文件 | 说明 |
|------|------|
| `src/src/config/config_loader.h` | 配置加载器类声明 |
| `src/src/config/config_loader.cpp` | YAML 配置文件解析 |
| `src/config.yaml` | 系统运行时配置参数 |

## 🎯 维护要求

- [ ] 确保所有负责文件编译无警告（`-Wall -Wextra`）
- [ ] 为新增/修改功能编写/更新单元测试
- [ ] 代码审查所有涉及上述文件的 Pull Request
- [ ] 维护模块接口的向后兼容性
- [ ] 定期同步 `main` 分支到 `feature/core-system`

## 🔗 关联信息

- **开发分支**: `feature/core-system`
- **前置完成**: Bug #7, #8, #11, #14, #15, #32 已修复 ✅
- **下一步**: 参见扩展 Issue — I2C 总线基础封装

## 📅 周期

持续维护，随项目迭代更新。"""
    },
    {
        "title": "【成员B】维护责任分配 — 空间感知层 (feature/spatial-intelligence)",
        "assignees": [MEMBER_B],
        "labels": ["maintenance", "spatial", "成员B"],
        "body": """## 📋 维护职责

成员B 负责维护 `src/` 目录下**空间感知层**的所有代码，包括深度估计、点云生成、定位导航与避障。

## 📁 负责文件清单

| 文件 | 说明 |
|------|------|
| `src/src/core/depth_estimator.h` | 深度估计器类声明 |
| `src/src/core/depth_estimator.cpp` | 单目深度估计算法（Sobel/MiDaS fallback） |
| `src/src/core/point_cloud.h` | 点云处理类声明 |
| `src/src/core/point_cloud.cpp` | 3D 点云生成（线程安全副本返回）|
| `src/src/core/position_estimator.h` | 位置估计器类声明 |
| `src/src/core/position_estimator.cpp` | 视觉里程计、特征点跟踪 |
| `src/src/core/obstacle_avoidance.h` | 避障算法类声明 |
| `src/src/core/obstacle_avoidance.cpp` | 扇区扫描、最优方向搜索 |

## 🎯 维护要求

- [ ] 确保所有负责文件编译无警告（`-Wall -Wextra`）
- [ ] 为 `point_cloud`、`obstacle_avoidance` 更新/新增单元测试
- [ ] 代码审查所有涉及上述文件的 Pull Request
- [ ] 维护 `getPoints()` 线程安全语义（持锁返回副本）
- [ ] 定期同步 `main` 分支到 `feature/spatial-intelligence`

## 🔗 关联信息

- **开发分支**: `feature/spatial-intelligence`
- **前置完成**: Bug #16, #31 已修复 ✅
- **下一步**: 参见扩展 Issue — ToF 距离传感器 / 激光雷达 / 底盘电机 / IMU

## 📅 周期

持续维护，随项目迭代更新。"""
    },
    {
        "title": "【成员C】维护责任分配 — 外设与工具层 (feature/peripheral-integration)",
        "assignees": [MEMBER_C],
        "labels": ["maintenance", "peripheral", "成员C"],
        "body": """## 📋 维护职责

成员C 负责维护 `src/` 目录下**外设与工具层**的所有代码，包括视频输出、灵巧手接口、HDR 控制及所有工具模块。

## 📁 负责文件清单

### 外设核心模块
| 文件 | 说明 |
|------|------|
| `src/src/core/video_output.h` | 视频输出类声明 |
| `src/src/core/video_output.cpp` | OSD/framebuffer 输出实现 |
| `src/src/core/hand_interface.h` | 灵巧手接口类声明（含 queryCount_ 实例成员）|
| `src/src/core/hand_interface.cpp` | 灵巧手 UART/I2C 通信（线程安全状态回调）|
| `src/src/core/hdr_controller.h` | HDR 控制器类声明（atomic 成员）|
| `src/src/core/hdr_controller.cpp` | 自动曝光/HDR 算法（percentile 布尔标志）|

### 工具模块
| 文件 | 说明 |
|------|------|
| `src/src/utils/exception_handler.h` | 异常处理类声明（含 `#include <thread>`）|
| `src/src/utils/exception_handler.cpp` | 锁外回调调用、TOCTOU 修复 |
| `src/src/utils/image_utils.h` | 图像处理工具类声明 |
| `src/src/utils/image_utils.cpp` | resize/bilateral/threshold 算法 |
| `src/src/utils/logger.h` | 日志类声明 |
| `src/src/utils/logger.cpp` | 线程安全日志（锁内时间戳）|
| `src/src/utils/profiler.h` | 性能分析宏/类声明（二级宏展开）|
| `src/src/utils/profiler.cpp` | 性能统计实现（加锁读 startupTime_）|

## 🎯 维护要求

- [ ] 确保所有负责文件编译无警告（`-Wall -Wextra`）
- [ ] 为 `image_utils`、`logger`、`profiler` 更新/新增单元测试
- [ ] 代码审查所有涉及上述文件的 Pull Request
- [ ] 维护异常处理回调的锁外调用语义
- [ ] 定期同步 `main` 分支到 `feature/peripheral-integration`

## 🔗 关联信息

- **开发分支**: `feature/peripheral-integration`
- **前置完成**: Bug #17, #18, #19, #20, #21, #22, #25, #26, #27, #28, #46 已修复 ✅
- **下一步**: 参见扩展 Issue — OLED 显示驱动 / 伺服控制器

## 📅 周期

持续维护，随项目迭代更新。"""
    },
]

# ═══════════════════════════════════════════════════════════════════════════
# 3. 新建：外设扩展功能分配 Issues（基于引脚分配方案）
# ═══════════════════════════════════════════════════════════════════════════
EXTENSION_ISSUES = [
    {
        "title": "【成员A/队长】扩展 P2 — I2C 总线基础封装 (i2c_bus)",
        "assignees": [MEMBER_A],
        "labels": ["extension", "hardware", "i2c", "成员A", "P2"],
        "body": """## 📋 任务描述

封装 A1 SDK 的 I2C 读写接口，供 OLED、ToF、PCA9685、MPU6050 等所有 I2C 外设共用。

## 🔌 引脚信息

| 总线 | 数据引脚 | 时钟引脚 | 挂载外设 |
|------|----------|----------|---------|
| I2C0 | Pin 1 (SDAS) | Pin 2 (SCLS) | OLED(0x3C), ToF(0x29), PCA9685(0x40) |
| I2C2 | Pin 25 (SDAM2) | Pin 26 (SCLM2) | MPU6050(0x68), 第2块PCA9685(0x41) |

> ⚠️ **注意**：A1 所有 IO 为 **1.8V 电平域**，外接 3.3V/5V 外设时必须加电平转换器（TXS0102/04 或 MOSFET）。

## 🎯 验收标准

- [ ] 新增 `src/src/core/peripheral/i2c_bus.h` / `i2c_bus.cpp`
- [ ] 支持 `open(busNum)` / `close()` / `read(addr, reg, buf, len)` / `write(addr, reg, buf, len)`
- [ ] 支持 I2C0 和 I2C2 两条总线
- [ ] 线程安全（多外设同时访问同一总线时加锁）
- [ ] 错误处理：设备不存在时返回错误码，不崩溃

## 📎 关联文件

- `src/src/core/peripheral/i2c_bus.h` ← 待创建
- `src/src/core/peripheral/i2c_bus.cpp` ← 待创建

## 📅 优先级 / 阶段

- **优先级**: P2（第三阶段：外设集成）
- **分支**: `feature/peripheral-integration`
- **前置依赖**: 无（基础模块，其他外设依赖本模块）"""
    },
    {
        "title": "【成员C】扩展 P2 — OLED 显示驱动 (SSD1306, I2C0 0x3C, Pin 1+2)",
        "assignees": [MEMBER_C],
        "labels": ["extension", "hardware", "display", "i2c", "成员C", "P2"],
        "body": """## 📋 任务描述

实现 SSD1306 OLED 显示屏驱动，用于实时展示系统状态（FPS、目标数量、最近障碍距离等）。

## 🔌 引脚信息

| 参数 | 值 |
|------|----|
| 总线 | I2C0 |
| I2C 地址 | 0x3C |
| 数据引脚 | Pin 1 (I2C0 SDAS) |
| 时钟引脚 | Pin 2 (I2C0 SCLS) |
| 分辨率 | 128×64 像素 |
| 供电 | 外部 5V 2A（经电平转换器） |

> ⚠️ A1 IO 为 1.8V，OLED VCC 需外部 5V，I2C 信号线需加电平转换器（TXS0102/04）。

## 🎯 验收标准

- [ ] 新增 `src/src/core/peripheral/oled_display.h` / `oled_display.cpp`
- [ ] 实现接口：
  - `bool init(int i2cBus = 0, uint8_t addr = 0x3C)`
  - `void clear()`
  - `void drawText(int x, int y, const char* text)`
  - `void drawRect(int x1, int y1, int x2, int y2)`
  - `void showStats(float fps, int objCount, float nearestDist)` — 显示系统统计
  - `void update()` — 刷新到屏幕
  - `void release()`
- [ ] 实物测试：显示 "Hello A1" 字样
- [ ] 集成到 `VisionSystem` 主循环，每秒刷新一次状态

## 📎 关联文件

- `src/src/core/peripheral/oled_display.h` ← 待创建
- `src/src/core/peripheral/oled_display.cpp` ← 待创建
- 依赖: `i2c_bus.h`

## 📅 优先级 / 阶段

- **优先级**: P2（第三阶段）
- **分支**: `feature/peripheral-integration`
- **前置依赖**: I2C 总线基础封装（i2c_bus）"""
    },
    {
        "title": "【成员B】扩展 P2 — ToF 距离传感器驱动 (VL53L1X, I2C0 0x29, Pin 1+2)",
        "assignees": [MEMBER_B],
        "labels": ["extension", "hardware", "sensor", "i2c", "成员B", "P2"],
        "body": """## 📋 任务描述

实现 VL53L1X ToF（飞行时间）距离传感器驱动，补充视觉深度在近距离盲区（0~30cm）的测距精度。

## 🔌 引脚信息

| 参数 | 值 |
|------|----|
| 总线 | I2C0 |
| I2C 地址 | 0x29 |
| 数据引脚 | Pin 1 (I2C0 SDAS) |
| 时钟引脚 | Pin 2 (I2C0 SCLS) |
| 测距范围 | 0.1 ~ 4m |
| 供电 | 外部 5V 2A（经电平转换器） |

> ⚠️ A1 IO 为 1.8V，VL53L1X 的 I2C 信号线需加电平转换器（TXS0102/04）。

## 🎯 验收标准

- [ ] 新增 `src/src/core/peripheral/tof_sensor.h` / `tof_sensor.cpp`
- [ ] 实现接口：
  - `bool init(int i2cBus = 0, uint8_t addr = 0x29)`
  - `int getDistanceMM()` — 返回距离（毫米），0-4000
  - `bool isRangeValid()` — 判断测距结果是否有效
  - `void setTimingBudget(int ms)` — 精度 vs 速度权衡（推荐 33ms）
  - `void release()`
- [ ] 实物测试：0.5m 处测距误差 < 5%
- [ ] 集成到 `ObstacleAvoidance` 模块，近距离时优先使用 ToF 数据

## 📎 关联文件

- `src/src/core/peripheral/tof_sensor.h` ← 待创建
- `src/src/core/peripheral/tof_sensor.cpp` ← 待创建
- 依赖: `i2c_bus.h`

## 📅 优先级 / 阶段

- **优先级**: P2（第三阶段）
- **分支**: `feature/spatial-intelligence`
- **前置依赖**: I2C 总线基础封装（i2c_bus）"""
    },
    {
        "title": "【成员C】扩展 P2 — 伺服控制器驱动 (PCA9685 + MG996R×6, I2C0 0x40, Pin 1+2)",
        "assignees": [MEMBER_C],
        "labels": ["extension", "hardware", "servo", "hand", "i2c", "成员C", "P2"],
        "body": """## 📋 任务描述

实现 PCA9685 16路 PWM 舵机控制器驱动，控制灵巧手 5~6 根手指的 MG996R 伺服舵机。

## 🔌 引脚信息

| 参数 | 值 |
|------|----|
| 总线 | I2C0 |
| I2C 地址 | 0x40（主控板），0x41（扩展板，挂 I2C2）|
| 数据引脚 | Pin 1 (I2C0 SDAS) |
| 时钟引脚 | Pin 2 (I2C0 SCLS) |
| PWM 通道 | 16 路（每手指 1~2 通道）|
| 舵机型号 | MG996R × 5~6（0°~270°）|
| 舵机供电 | **独立 7.4V**（不可用板载电源！）|

> ⚠️ A1 IO 为 1.8V，PCA9685 I2C 信号线需加电平转换器（TXS0102/04）。
> ⚠️ MG996R 舵机需独立 7.4V 2A+ 供电，GND 共地到 Pin 33/34/47/48。

## 🎯 验收标准

- [ ] 新增 `src/src/core/peripheral/servo_controller.h` / `servo_controller.cpp`
- [ ] 实现接口：
  ```cpp
  enum class GraspPose { OPEN, CLOSE, PINCH, POINT, PEACE };
  class ServoController {
      bool init(int i2cBus = 0, uint8_t addr = 0x40);
      void setAngle(int channel, float angleDeg);       // 0-270°
      void setFingerGrasp(float openness);               // 0.0=握紧, 1.0=张开
      void setPredefinedPose(GraspPose pose);             // 预定义手势
      void release();
  };
  ```
- [ ] 实物测试：`OPEN` → `CLOSE` → `PINCH` 手势切换正常
- [ ] 集成到 `HandInterface` 模块，替换旧的 UART 控制逻辑

## 📎 关联文件

- `src/src/core/peripheral/servo_controller.h` ← 待创建
- `src/src/core/peripheral/servo_controller.cpp` ← 待创建
- 修改: `src/src/core/hand_interface.cpp`（集成 servo_controller）
- 依赖: `i2c_bus.h`

## 📅 优先级 / 阶段

- **优先级**: P2（第三阶段）
- **分支**: `feature/peripheral-integration`
- **前置依赖**: I2C 总线基础封装（i2c_bus）"""
    },
    {
        "title": "【成员B】扩展 P2 — 激光雷达驱动 (TFmini-S, UART0, Pin 15+16)",
        "assignees": [MEMBER_B],
        "labels": ["extension", "hardware", "lidar", "uart", "成员B", "P2"],
        "body": """## 📋 任务描述

实现 TFmini-S 激光雷达驱动，提供 0.1~12m 前向测距，用于远距离障碍检测（弥补 ToF 4m 上限）。

## 🔌 引脚信息

| 参数 | 值 |
|------|----|
| 接口 | UART0 |
| TX 引脚 | Pin 15 (UART0 TX) |
| RX 引脚 | Pin 16 (UART0 RX) |
| 波特率 | 115200 baud |
| 测距范围 | 0.1 ~ 12m |
| 输出频率 | 最高 1000Hz（推荐 100Hz）|
| 供电 | 外部 5V 2A |

> ⚠️ A1 UART0 为 1.8V 电平，TFmini-S TX/RX 为 3.3V，需加电平转换器。
> 💡 若已选用 VL53L1X ToF 覆盖近距离，UART0 也可接第二灵巧手控制器。

## 🎯 验收标准

- [ ] 新增 `src/src/core/peripheral/lidar_sensor.h` / `lidar_sensor.cpp`
- [ ] 实现接口：
  - `bool init(const std::string& port = "/dev/ttyS0", int baudRate = 115200)`
  - `int getDistanceCM()` — 返回距离（厘米）
  - `float getStrength()` — 返回信号强度（用于判断测量可靠性）
  - `bool isValid()` — 检测帧校验是否通过
  - `void release()`
- [ ] 帧解析：TFmini-S 9字节帧格式（0x59 0x59 distL distH strL strH mode 0x00 checksum）
- [ ] 实物测试：2m 处测距误差 < 2cm
- [ ] 集成到 `ObstacleAvoidance`，>4m 范围使用激光雷达数据

## 📎 关联文件

- `src/src/core/peripheral/lidar_sensor.h` ← 待创建
- `src/src/core/peripheral/lidar_sensor.cpp` ← 待创建

## 📅 优先级 / 阶段

- **优先级**: P2（第三阶段）
- **分支**: `feature/spatial-intelligence`
- **前置依赖**: 无（直接使用 POSIX serial API）"""
    },
    {
        "title": "【成员B】扩展 P2 — 底盘电机驱动 (TB6612, GPIO8-10+TRIG0, Pin 17-19+39)",
        "assignees": [MEMBER_B],
        "labels": ["extension", "hardware", "motor", "gpio", "成员B", "P2"],
        "body": """## 📋 任务描述

实现 TB6612 双路直流电机驱动，控制底盘左右轮，配合避障算法实现自主移动。

## 🔌 引脚信息

| 信号 | A1 引脚 | GPIO 编号 | 功能 |
|------|---------|-----------|------|
| PWM_Left | Pin 17 | GPIO8 | 左轮速度 (PWM) |
| DIR_Left | Pin 18 | GPIO9 | 左轮方向 |
| PWM_Right | Pin 19 | GPIO10 | 右轮速度 (PWM) |
| DIR_Right | Pin 39 | CAM0_TRIG0 | 右轮方向 |
| STBY | — | — | 接 3.3V（持续使能）|

> ⚠️ A1 GPIO 为 1.8V 电平，TB6612 逻辑输入为 3.3V/5V，需加电平转换器。
> ⚠️ 电机供电需独立 7.4V，GND 共地到 Pin 33/34/47/48。

## 🎯 验收标准

- [ ] 新增 `src/src/core/peripheral/motor_driver.h` / `motor_driver.cpp`
- [ ] 实现接口：
  ```cpp
  class MotorDriver {
      bool init(int pwmPinL = 8, int dirPinL = 9,
                int pwmPinR = 10, int dirPinR = 39);
      void setSpeed(float left, float right);  // -1.0(后退) ~ +1.0(前进)
      void stop();
      void emergencyStop();
      void release();
  };
  ```
- [ ] 实物测试：前进 → 停止 → 左转 → 右转动作正常
- [ ] 集成到 `ObstacleAvoidance`，`findBestDirection()` 输出方向向量直接驱动电机

## 📎 关联文件

- `src/src/core/peripheral/motor_driver.h` ← 待创建
- `src/src/core/peripheral/motor_driver.cpp` ← 待创建
- 修改: `src/src/core/obstacle_avoidance.cpp`（集成 motor_driver）

## 📅 优先级 / 阶段

- **优先级**: P2（第三阶段）
- **分支**: `feature/spatial-intelligence`
- **前置依赖**: 无（直接使用 A1 GPIO/PWM API）"""
    },
    {
        "title": "【成员B】扩展 P3 — IMU 融合定位 (MPU6050, I2C2 0x68, Pin 25+26) [可选]",
        "assignees": [MEMBER_B],
        "labels": ["extension", "hardware", "imu", "i2c", "成员B", "P3", "optional"],
        "body": """## 📋 任务描述

实现 MPU6050 六轴 IMU（加速度计 + 陀螺仪）驱动，与视觉里程计融合，提升 3D 定位精度。

## 🔌 引脚信息

| 参数 | 值 |
|------|----|
| 总线 | I2C2（预留扩展总线）|
| I2C 地址 | 0x68 |
| 数据引脚 | Pin 25 (I2C2 SDAM2) |
| 时钟引脚 | Pin 26 (I2C2 SCLM2) |
| 测量轴 | 3轴加速度 + 3轴陀螺仪 = 6DOF |
| 供电 | 外部 3.3V（经电平转换器）|

> ⚠️ I2C2 总线同时可扩展第二块 PCA9685（双臂扩展，地址 0x41）。

## 🎯 验收标准

- [ ] 新增 `src/src/core/peripheral/imu_sensor.h` / `imu_sensor.cpp`
- [ ] 实现接口：
  - `bool init(int i2cBus = 2, uint8_t addr = 0x68)`
  - `void getAccel(float& ax, float& ay, float& az)` — 单位 m/s²
  - `void getGyro(float& gx, float& gy, float& gz)` — 单位 rad/s
  - `float getPitch()` / `getRoll()` / `getYaw()` — 互补滤波估计姿态角
  - `void calibrate()` — 静止校准零偏
  - `void release()`
- [ ] 与 `PositionEstimator` 融合：加速度积分辅助视觉里程计，减少飘移
- [ ] 实物测试：静止时 pitch/roll < 0.5°

## 📎 关联文件

- `src/src/core/peripheral/imu_sensor.h` ← 待创建
- `src/src/core/peripheral/imu_sensor.cpp` ← 待创建
- 修改: `src/src/core/position_estimator.cpp`（融合 IMU 数据）
- 依赖: `i2c_bus.h`（I2C2 总线）

## 📅 优先级 / 阶段

- **优先级**: P3（第四阶段，可选高级功能）
- **工作量**: ~2-4 周
- **分支**: `feature/spatial-intelligence`
- **前置依赖**: I2C 总线基础封装 + 底盘电机驱动"""
    },
    {
        "title": "【全体】扩展 P2 — 电平转换器接线规范与采购清单",
        "assignees": [MEMBER_A, MEMBER_B, MEMBER_C],
        "labels": ["extension", "hardware", "documentation", "P2"],
        "body": """## 📋 任务描述

A1 开发板所有 IO 引脚均为 **1.8V 电平**，而所有外设（OLED、ToF、PCA9685、TFmini-S、TB6612、MPU6050）均为 3.3V 或 5V 逻辑电平，**必须加电平转换器**，否则损坏开发板 IO 或外设。

## ⚡ 电平转换需求汇总

| 外设 | 接口 | A1电平 | 外设电平 | 转换方向 | 转换器型号 |
|------|------|--------|----------|---------|-----------|
| SSD1306 OLED | I2C0 (Pin 1+2) | 1.8V | 3.3V | 双向 | TXS0102 |
| VL53L1X ToF | I2C0 (Pin 1+2) | 1.8V | 3.3V | 双向 | TXS0102 |
| PCA9685 | I2C0 (Pin 1+2) | 1.8V | 3.3V | 双向 | TXS0102 |
| TFmini-S | UART0 (Pin 15+16) | 1.8V | 3.3V | 双向 | TXS0102 |
| TB6612 | GPIO (Pin 17-19+39) | 1.8V | 3.3V | 单向 | TXS0101 或 MOSFET |
| MPU6050 | I2C2 (Pin 25+26) | 1.8V | 3.3V | 双向 | TXS0102 |

## 🛒 采购清单（参考）

| 型号 | 数量 | 用途 | 参考价（淘宝）|
|------|------|------|--------------|
| TXS0102 模块（2通道双向） | 3 片 | I2C0 (2路) × 2 + UART0 (2路) | ~5元/片 |
| TXS0101 或 N沟道MOSFET | 2 片 | GPIO 单向转换 × 4路 | ~3元/片 |
| 外部 5V 2A 电源 | 1 个 | OLED/ToF/PCA9685/TFmini-S VCC | ~15元 |
| 7.4V LiPo 电池 | 1 个 | MG996R 舵机 + TB6612 电机独立供电 | ~30元 |

## 📐 电源架构

```
板载 1V8 ──→ 电平转换器 HV 侧参考电压（不可直接给外设！）
                    │
             ┌──────┴──────┐
             │ TXS0102/04  │  1.8V ↔ 3.3V 双向转换
             └──────┬──────┘
                    │
外部 5V 2A ──→ OLED / ToF / PCA9685 / TFmini-S VCC
外部 7.4V ──→ MG996R 舵机 + TB6612 电机（大电流独立供电）
所有 GND ──→ Pin 33 / 34 / 47 / 48（共地！）
```

## ✅ 验收标准

- [ ] 采购并到货所有电平转换器和电源模块
- [ ] 完成电平转换电路焊接/接线
- [ ] 用万用表验证各路 IO 电平在转换后符合规格
- [ ] 更新接线图到 `docs/hardware_wiring.md`

## 📅 优先级 / 阶段

- **优先级**: P2（第三阶段，外设开发前必须完成）
- **负责人**: 全体成员共同确认，实际操作可由成员A协调"""
    },
]


def make_request(method, path, data=None, token=None):
    """发送 GitHub API 请求"""
    url = f"{API_BASE}{path}"
    headers = {
        "Authorization": f"token {token}",
        "Accept": "application/vnd.github.v3+json",
        "Content-Type": "application/json",
        "User-Agent": "manage_issues.py/1.0",
        "X-GitHub-Api-Version": "2022-11-28",
    }

    body = json.dumps(data).encode("utf-8") if data else None
    req = urllib.request.Request(url, data=body, headers=headers, method=method)

    # 尝试使用本地代理（Windows 开发环境）
    proxies = [
        None,
        "http://127.0.0.1:7897",
        "http://127.0.0.1:1080",
    ]

    for proxy in proxies:
        try:
            if proxy:
                proxy_handler = urllib.request.ProxyHandler(
                    {"http": proxy, "https": proxy}
                )
                opener = urllib.request.build_opener(proxy_handler)
                resp = opener.open(req, timeout=30)
            else:
                resp = urllib.request.urlopen(req, timeout=30)

            return json.loads(resp.read().decode())
        except urllib.error.HTTPError as e:
            raise e
        except (urllib.error.URLError, OSError):
            continue

    raise urllib.error.URLError("所有网络路径均无法连接 GitHub API")


def close_bug_issues(token, dry_run=False):
    """关闭已修复的 Bug Issues，并添加修复摘要注释"""
    print("\n" + "═" * 60)
    print("阶段 1：关闭已修复的 Bug Issues")
    print("═" * 60)

    closed = 0
    failed = 0

    for issue in FIXED_BUG_ISSUES:
        num = issue["number"]
        comment = issue["comment"]
        print(f"\n  处理 #{num}...")

        if dry_run:
            print(f"  [DRY-RUN] 将为 #{num} 添加注释并关闭")
            closed += 1
            continue

        # 添加修复摘要注释
        try:
            make_request("POST", f"/issues/{num}/comments",
                         {"body": comment}, token)
            print(f"  [✓] #{num} 已添加修复注释")
        except urllib.error.HTTPError as e:
            print(f"  [!] #{num} 添加注释失败: HTTP {e.code}")
            failed += 1
            continue

        # 关闭 Issue
        try:
            make_request("PATCH", f"/issues/{num}",
                         {"state": "closed", "state_reason": "completed"}, token)
            print(f"  [✓] #{num} 已关闭（状态: completed）")
            closed += 1
        except urllib.error.HTTPError as e:
            print(f"  [!] #{num} 关闭失败: HTTP {e.code}")
            failed += 1

    print(f"\n  小计: {closed} 个已关闭, {failed} 个失败")
    return closed, failed


def create_issues_batch(token, issues_list, stage_name, dry_run=False):
    """批量创建 Issues"""
    print("\n" + "═" * 60)
    print(f"阶段 {stage_name}")
    print("═" * 60)

    created = 0
    failed = 0

    for issue in issues_list:
        title = issue["title"]
        print(f"\n  创建: {title[:60]}...")

        if dry_run:
            print(f"  [DRY-RUN] 将创建 Issue")
            created += 1
            continue

        data = {
            "title": issue["title"],
            "body": issue["body"],
            "assignees": issue.get("assignees", []),
            "labels": issue.get("labels", []),
        }

        try:
            result = make_request("POST", "/issues", data, token)
            print(f"  [✓] #{result['number']}: {title[:60]}")
            created += 1
        except urllib.error.HTTPError as e:
            body_text = ""
            try:
                body_text = e.read().decode()[:200]
            except Exception:
                pass
            print(f"  [✗] 创建失败: HTTP {e.code}: {body_text}")
            failed += 1
        except urllib.error.URLError as e:
            print(f"  [✗] 网络错误: {e}")
            failed += 1

    print(f"\n  小计: {created} 个已创建, {failed} 个失败")
    return created, failed


def main():
    parser = argparse.ArgumentParser(description="GitHub Issues 管理脚本")
    parser.add_argument("--close-bugs", action="store_true", help="关闭已修复的 Bug Issues")
    parser.add_argument("--create-maintenance", action="store_true", help="创建 src 维护分配 Issues")
    parser.add_argument("--create-extensions", action="store_true", help="创建外设扩展分配 Issues")
    parser.add_argument("--all", action="store_true", help="执行全部操作")
    parser.add_argument("--dry-run", action="store_true", help="模拟运行，不实际修改 GitHub")
    args = parser.parse_args()

    # 默认执行全部
    if not any([args.close_bugs, args.create_maintenance, args.create_extensions, args.all]):
        args.all = True

    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("错误: 请设置环境变量 GITHUB_TOKEN")
        print("  export GITHUB_TOKEN='ghp_xxxxxxx'")
        sys.exit(1)

    if args.dry_run:
        print("⚠️  DRY-RUN 模式：不会实际修改 GitHub")

    print(f"\n目标仓库: {REPO}")
    print(f"操作计划:")
    if args.all or args.close_bugs:
        print(f"  • 关闭 {len(FIXED_BUG_ISSUES)} 个已修复的 Bug Issues")
    if args.all or args.create_maintenance:
        print(f"  • 创建 {len(MAINTENANCE_ISSUES)} 个 src 维护分配 Issues")
    if args.all or args.create_extensions:
        print(f"  • 创建 {len(EXTENSION_ISSUES)} 个外设扩展分配 Issues")

    if not args.dry_run:
        confirm = input("\n确认执行? (y/N): ").strip().lower()
        if confirm != "y":
            print("已取消")
            sys.exit(0)

    total_closed = 0
    total_created = 0
    total_failed = 0

    if args.all or args.close_bugs:
        c, f = close_bug_issues(token, args.dry_run)
        total_closed += c
        total_failed += f

    if args.all or args.create_maintenance:
        c, f = create_issues_batch(
            token, MAINTENANCE_ISSUES,
            "2：创建 src 目录维护分配 Issues", args.dry_run
        )
        total_created += c
        total_failed += f

    if args.all or args.create_extensions:
        c, f = create_issues_batch(
            token, EXTENSION_ISSUES,
            "3：创建外设扩展功能分配 Issues", args.dry_run
        )
        total_created += c
        total_failed += f

    print("\n" + "═" * 60)
    print("执行完成")
    print("═" * 60)
    print(f"  Bug Issues 已关闭: {total_closed}")
    print(f"  新 Issues 已创建: {total_created}")
    print(f"  失败: {total_failed}")

    if total_failed > 0:
        sys.exit(1)


if __name__ == "__main__":
    main()
