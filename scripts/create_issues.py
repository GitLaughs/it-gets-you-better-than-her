#!/usr/bin/env python3
"""
一键创建 GitHub Issues — 三人分工任务
用法:
    export GITHUB_TOKEN="ghp_xxxxx"
    python3 scripts/create_issues.py
"""
import os
import sys
import json
import urllib.request
import urllib.error

REPO = "GitLaughs/it-gets-you-better-than-her"
API_BASE = f"https://api.github.com/repos/{REPO}/issues"

# ═══════════════════════════════════════════════════════════════════════════
# 三人分工定义  —  根据实际团队成员修改 GitHub username
# ═══════════════════════════════════════════════════════════════════════════
MEMBER_A = "MemberA"   # TODO: 替换为实际 GitHub 用户名
MEMBER_B = "MemberB"   # TODO: 替换为实际 GitHub 用户名
MEMBER_C = "MemberC"   # TODO: 替换为实际 GitHub 用户名

ISSUES = [
    # ── 成员 A: 感知层（检测 + 跟踪 + 深度 + 点云） ────────────────────────
    {
        "title": "[成员A] YOLOv8 目标检测模块开发",
        "assignees": [MEMBER_A],
        "labels": ["detection", "core", "成员A"],
        "milestone": "M1-感知层",
        "body": """## 📋 任务描述
实现 YOLOv8 目标检测模块，完成模型加载、推理、结果解析。

## 🎯 验收标准
- [ ] 支持 YOLOv8n / YOLOv8s 模型加载
- [ ] 支持 ONNX Runtime 推理（适配 A1 NPU INT8）
- [ ] 输入 720p 帧，输出 `[x1, y1, x2, y2, conf, class_id]` 列表
- [ ] 置信度阈值、NMS 阈值可配置
- [ ] 单帧推理延迟 < 100ms (PC) / 合理范围 (A1)
- [ ] 单元测试 `tests/test_detection.py` 全部通过

## 📎 关联文件
- `src/detection/yolov8_detector.py`
- `src/config/config.yaml`

## 📅 里程碑
- 分支: `feature/detection`
"""
    },
    {
        "title": "[成员A] 目标跟踪模块开发 (DeepSORT/ByteTrack)",
        "assignees": [MEMBER_A],
        "labels": ["tracking", "core", "成员A"],
        "milestone": "M1-感知层",
        "body": """## 📋 任务描述
实现多目标跟踪，保持目标 ID 在连续帧中的一致性。

## 🎯 验收标准
- [ ] 支持 ByteTrack / DeepSORT 其中之一
- [ ] 接受检测模块输出，返回 `[track_id, x1, y1, x2, y2, conf, cls]`
- [ ] 同一目标在 60 帧内 ID 不跳变
- [ ] reset() 方法可用
- [ ] 单元测试 `tests/test_tracking.py` 全部通过

## 📎 关联文件
- `src/tracking/tracker.py`

## 📅 里程碑
- 分支: `feature/tracking`
- 前置依赖: #1 (检测模块)
"""
    },
    {
        "title": "[成员A] 单目深度估计模块开发",
        "assignees": [MEMBER_A],
        "labels": ["depth", "core", "成员A"],
        "milestone": "M1-感知层",
        "body": """## 📋 任务描述
实现单目深度估计（MiDaS / Depth Anything Lite），将 RGB 帧转为稠密深度图。

## 🎯 验收标准
- [ ] 输入 RGB 帧，输出等比例深度图 (H, W) float32
- [ ] 深度值非负，归一化到 [0, 1] 或实际米制
- [ ] 支持 ONNX Runtime 推理
- [ ] 同帧多次推理结果一致（确定性）
- [ ] 单元测试 `tests/test_depth.py` 全部通过

## 📎 关联文件
- `src/depth/monocular_depth.py`

## 📅 里程碑
- 分支: `feature/depth`
"""
    },
    {
        "title": "[成员A] 三维点云生成与可视化",
        "assignees": [MEMBER_A],
        "labels": ["pointcloud", "visualization", "成员A"],
        "milestone": "M2-融合层",
        "body": """## 📋 任务描述
根据深度图 + 相机内参生成三维点云，并实现可视化输出。

## 🎯 验收标准
- [ ] 深度图 + RGB → 彩色点云 (N, 6) [x, y, z, r, g, b]
- [ ] 支持 Open3D / 自定义渲染可视化
- [ ] 支持导出 PLY 格式到 `output/` 目录
- [ ] 可配置下采样率以适配 A1 算力

## 📎 关联文件
- `src/pointcloud/pointcloud_generator.py`

## 📅 里程碑
- 分支: `feature/pointcloud`
- 前置依赖: #3 (深度估计)
"""
    },

    # ── 成员 B: 控制层（定位 + 避障 + 灵巧手 + 摄像头） ────────────────
    {
        "title": "[成员B] 自身定位与目标坐标估计",
        "assignees": [MEMBER_B],
        "labels": ["localization", "core", "成员B"],
        "milestone": "M2-融合层",
        "body": """## 📋 任务描述
根据跟踪结果和深度图，估计自身位置以及每个被跟踪目标的三维坐标。

## 🎯 验收标准
- [ ] 输入 tracks + depth_map，输出自身位姿与目标 3D 坐标
- [ ] 坐标系定义清晰（相机坐标系 / 世界坐标系）
- [ ] 支持相机内参配置

## 📎 关联文件
- `src/localization/position_estimator.py`

## 📅 里程碑
- 分支: `feature/localization`
- 前置依赖: #2 (跟踪), #3 (深度)
"""
    },
    {
        "title": "[成员B] 避障算法模块",
        "assignees": [MEMBER_B],
        "labels": ["navigation", "core", "成员B"],
        "milestone": "M2-融合层",
        "body": """## 📋 任务描述
基于深度图和目标检测结果，实现实时避障决策。

## 🎯 验收标准
- [ ] 输入 depth_map + tracks → 输出避障指令 (前进/左转/右转/停止)
- [ ] 安全距离阈值可配置
- [ ] 危险区域可视化叠加到输出帧

## 📎 关联文件
- `src/navigation/obstacle_avoidance.py`

## 📅 里程碑
- 分支: `feature/navigation`
- 前置依赖: #3 (深度), #5 (定位)
"""
    },
    {
        "title": "[成员B] SC132GS 摄像头管理 + HDR 控制",
        "assignees": [MEMBER_B],
        "labels": ["camera", "hardware", "成员B"],
        "milestone": "M1-感知层",
        "body": """## 📋 任务描述
封装 SC132GS 摄像头采集，支持 HDR 模式自动切换。

## 🎯 验收标准
- [ ] 支持 1280x720@90fps 采集
- [ ] 支持 HDR 模式开关（根据亮度自动切换）
- [ ] 光线充足 / 黑暗两种场景下稳定运行
- [ ] 通过 A1 SDK MIPI CSI 接口驱动
- [ ] 异常断开后自动重连

## 📎 关联文件
- `src/camera/camera_manager.py`
- `src/camera/hdr_controller.py`

## 📅 里程碑
- 分支: `feature/camera`
"""
    },
    {
        "title": "[成员B] 灵巧手外设接口",
        "assignees": [MEMBER_B],
        "labels": ["hand", "hardware", "成员B"],
        "milestone": "M3-外设层",
        "body": """## 📋 任务描述
预留灵巧手控制接口，通过 UART/SPI/I2C 与外设通信。

## 🎯 验收标准
- [ ] 抽象接口类定义（connect / send_command / get_status / disconnect）
- [ ] 支持 UART 通信（A1 P4 接口 Pin 15/16）
- [ ] 提供模拟模式（无实际硬件时可测试）

## 📎 关联文件
- `src/hand_interface/dexterous_hand.py`

## 📅 里程碑
- 分支: `feature/hand-interface`
"""
    },

    # ── 成员 C: 系统层（集成 + 配置 + 异常 + Docker + 文档 + 显示） ──────
    {
        "title": "[成员C] 主程序入口 + 流水线编排",
        "assignees": [MEMBER_C],
        "labels": ["integration", "core", "成员C"],
        "milestone": "M3-外设层",
        "body": """## 📋 任务描述
编写 `main.py`，编排完整检测→跟踪→深度→点云→定位→避障流水线。

## 🎯 验收标准
- [ ] 一键启动: `python3 src/main.py`
- [ ] 支持命令行参数 / config.yaml 配置
- [ ] 稳定运行 ≥ 60s 不崩溃
- [ ] 帧率统计实时输出
- [ ] 优雅退出 (Ctrl+C)

## 📎 关联文件
- `src/main.py`
- `src/config/config.yaml`

## 📅 里程碑
- 分支: `feature/integration`
- 前置依赖: 所有功能模块
"""
    },
    {
        "title": "[成员C] 视频输出到外设屏幕",
        "assignees": [MEMBER_C],
        "labels": ["display", "visualization", "成员C"],
        "milestone": "M3-外设层",
        "body": """## 📋 任务描述
将检测 / 跟踪 / 深度叠加后的帧输出到外设屏幕（MIPI CSI TX）或保存为视频。

## 🎯 验收标准
- [ ] 支持实时帧渲染: bbox + track_id + 深度色图叠加
- [ ] 支持 MIPI TX 输出（A1 SDK）
- [ ] 支持录制保存到 `output/` (MP4)
- [ ] 帧率稳定不卡顿

## 📎 关联文件
- `src/display/video_output.py`

## 📅 里程碑
- 分支: `feature/display`
"""
    },
    {
        "title": "[成员C] 异常处理体系",
        "assignees": [MEMBER_C],
        "labels": ["exception", "reliability", "成员C"],
        "milestone": "M2-融合层",
        "body": """## 📋 任务描述
实现三类异常处理：摄像头/数据异常、推理异常、资源异常。

## 🎯 验收标准
- [ ] 摄像头断连 → 自动重试 → 超时告警
- [ ] 推理失败 → 跳帧 + 日志记录
- [ ] 内存/CPU 超限 → 降级运行 + 告警
- [ ] 所有异常有 现象-定位-修复-验证 闭环记录
- [ ] 自定义异常类体系

## 📎 关联文件
- `src/utils/exception_handler.py`
- `src/utils/logger.py`

## 📅 里程碑
- 分支: `feature/exception-handling`
"""
    },
    {
        "title": "[成员C] Docker 开发环境 + CI + 文档",
        "assignees": [MEMBER_C],
        "labels": ["devops", "documentation", "成员C"],
        "milestone": "M0-环境搭建",
        "body": """## 📋 任务描述
完善 Docker 开发环境、一键脚本、Windows 开发文档。

## 🎯 验收标准
- [ ] `docker compose up -d` 一键启动
- [ ] `scripts/setup_dev.sh` 一键初始化
- [ ] `docs/windows_docker_dev_guide.md` 完整可用
- [ ] README.md 包含快速开始 + 架构 + 评分映射
- [ ] `.gitignore` 完善

## 📎 关联文件
- `docker/`, `scripts/`, `docs/`, `README.md`

## 📅 里程碑
- 分支: `feature/devops`
"""
    },
    {
        "title": "[成员C] 日志系统",
        "assignees": [MEMBER_C],
        "labels": ["logging", "成员C"],
        "milestone": "M0-环境搭建",
        "body": """## 📋 任务描述
实现统一日志系统，支持控制台 + 文件输出。

## 🎯 验收标准
- [ ] 支持 DEBUG / INFO / WARN / ERROR 级别
- [ ] 日志输出到控制台 + `logs/` 目录
- [ ] 日志格式: `[时间] [级别] [模块] 消息`
- [ ] 可配置日志级别

## 📎 关联文件
- `src/utils/logger.py`

## 📅 里程碑
- 分支: `feature/logging`
"""
    },
]


def create_issues():
    token = os.environ.get("GITHUB_TOKEN")
    if not token:
        print("错误: 请设置环境变量 GITHUB_TOKEN")
        print("  export GITHUB_TOKEN='ghp_xxxxxxx'")
        sys.exit(1)

    # 设置代理
    proxy_handler = urllib.request.ProxyHandler({'http': 'http://127.0.0.1:7897', 'https': 'http://127.0.0.1:7897'})
    opener = urllib.request.build_opener(proxy_handler)
    urllib.request.install_opener(opener)

    headers = {
        "Authorization": f"token {token}",
        "Accept": "application/vnd.github.v3+json",
        "Content-Type": "application/json",
    }

    created = 0
    failed = 0

    for issue in ISSUES:
        data = {
            "title": issue["title"],
            "body": issue["body"],
            "assignees": issue.get("assignees", []),
            "labels": issue.get("labels", []),
        }

        req = urllib.request.Request(
            API_BASE,
            data=json.dumps(data).encode("utf-8"),
            headers=headers,
            method="POST",
        )

        try:
            resp = urllib.request.urlopen(req)
            result = json.loads(resp.read().decode())
            print(f"  [✓] #{result['number']}: {issue['title']}")
            created += 1
        except urllib.error.HTTPError as e:
            body = e.read().decode()
            print(f"  [✗] {issue['title']}")
            print(f"       HTTP {e.code}: {body[:200]}")
            failed += 1
        except urllib.error.URLError as e:
            print(f"  [✗] {issue['title']}")
            print(f"       网络错误: {e}")
            failed += 1

    print(f"\n完成: {created} 个已创建, {failed} 个失败")


if __name__ == "__main__":
    print(f"即将向 {REPO} 创建 {len(ISSUES)} 个 Issues...")
    confirm = input("确认? (y/N): ").strip().lower()
    if confirm == "y":
        create_issues()
    else:
        print("已取消")