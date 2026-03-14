# bug修复方案



# 全项目审查总结报告

------

## 一、官方 Demo 的代码多了什么（核心差异）

官方 Demo 虽然代码量少，但它完整展示了 **SSNE 硬件平台的正确使用范式**。以下是代码中完全缺失的部分：

### 1. SSNE 硬件生命周期管理

```
官方 Demo 流程：
ssne_init() → sensor_init() → pipeline_init() → ssne_create() → 主循环 → release() → ssne_release()

你的代码流程：
VisionSystem() → V4L2 open() → ONNX Session() → 主循环 → close() → ~VisionSystem()
```

| 官方 Demo 调用的 API                                      | 你的代码中的对应物            | 状态       |
| --------------------------------------------------------- | ----------------------------- | ---------- |
| `ssne_init()` / `ssne_release()`                          | **无**                        | 🔴 完全缺失 |
| `sensor_init()` / `sensor_getframe()`                     | V4L2 `open()`/`ioctl()`       | ❌ 错误替代 |
| `OnlineSetCrop()` / `OnlineSetOutputImage()`              | CPU 软件 `resizeBilinear()`   | ❌ 错误替代 |
| `ssne_create()` / `ssne_inference()` / `ssne_getoutput()` | ONNX Runtime `Session::Run()` | ❌ 错误替代 |
| `osd_init()` / `osd_display()`                            | framebuffer `write()`         | ❌ 错误替代 |
| `GetImageData(kPipeline0, ...)`                           | `ioctl(VIDIOC_DQBUF)`         | ❌ 错误替代 |

### 2. 硬件 Pipeline 预处理

官方 Demo 使用 SSNE 硬件 pipeline 完成图像裁剪、缩放、格式转换（零 CPU 开销）：

```cpp
// Demo: pipeline_image.cpp
OnlineSetCrop(kPipeline0, 0, 720, 370, 910);           // 硬件裁剪
OnlineSetOutputImage(kPipeline0, SSNE_Y_8, 720, 540);  // 硬件缩放+格式转换
```

我的代码使用 CPU 软件完成所有预处理（resize、crop、normalize、灰度→3通道复制），在 90fps 下 **CPU 预算根本不够**。

### 3. NPU 模型部署方式

官方 Demo 直接将模型加载到 NPU：

```cpp
ssne_create(handle, "scrfd_gray.bin");  // 编译后的 NPU 模型
ssne_inference(handle, input_tensor);    // 硬件推理 ~5ms
```

我的代码使用 ONNX Runtime CPU 推理，即使模型能跑，延迟也会是 NPU 的 **10-100 倍**。

### 4. OSD 显示叠加层

官方 Demo 使用 OSD API 实现硬件叠加显示（人脸框、文字等），不影响主处理流水线：

```cpp
osd_init(handle);
osd_bindlayer(handle, layer, width, height, OSD_FORMAT_ARGB8888);
osd_display(handle, layer, buffer);
```

我的代码直接写 framebuffer，且格式假设为灰度（实际大多数 fb 是 RGB565/ARGB8888），**显示会乱码**。

### 5. DMA Buffer 零拷贝

官方 Demo 使用 DMA buffer 在 sensor → pipeline → NPU → OSD 之间零拷贝传递数据：

```cpp
osd_alloc_buffer(handle, dma, size);  // 分配 DMA buffer
// 数据在硬件模块间直接传递，不经过 CPU
```

我的代码全程 `memcpy`，每帧 1280×720 = ~900KB 至少拷贝 3-4 次。

------

## 二、代码故障汇总（按严重度排列）

### 🔴🔴 架构级致命错误（导致无法在开发板上运行的根本原因）

| #    | 故障                                    | 文件                                         | 后果                                                |
| ---- | --------------------------------------- | -------------------------------------------- | --------------------------------------------------- |
| 1    | **完全未调用 SSNE API**                 | 全局                                         | NPU/sensor/pipeline/OSD 全部不可用                  |
| 2    | **使用 V4L2 代替 SSNE sensor**          | `camera_manager.cpp`                         | A1 sensor 未走 SSNE 驱动，无法获取图像              |
| 3    | **使用 ONNX Runtime 代替 NPU**          | `yolov8_detector.cpp`, `depth_estimator.cpp` | ONNX Runtime 在 A1 上可能根本不可用；即使可用也极慢 |
| 4    | **无 ONNX 宏时检测器返回空**            | `yolov8_detector.cpp`                        | 嵌入式环境下功能完全失效                            |
| 5    | **framebuffer 直写格式错误**            | `video_output.cpp`                           | 显示输出乱码                                        |
| 6    | **缺少 `ssne_init()`/`ssne_release()`** | `vision_system.cpp`                          | 硬件未初始化                                        |

### 🔴 编译错误（代码无法构建）

| #    | 故障                                                | 文件                                                         |
| ---- | --------------------------------------------------- | ------------------------------------------------------------ |
| 7    | `captureFrame()` 方法不存在                         | `vision_system.cpp` 调用了 `camera_manager.h` 中未定义的方法 |
| 8    | `BBox` 类型未定义（缺少 include）                   | `vision_system.h`                                            |
| 9    | `CMakeLists.txt` 中 `message([<mode>]...)` 语法错误 | `CMakeLists.txt`                                             |
| 10   | `PROFILE_SCOPE` 宏 `__LINE__` 不展开，变量名冲突    | `profiler.h`                                                 |
| 11   | `safeExecute` 缺少 `#include <thread>`              | `exception_handler.h`                                        |

### 🔴 内存与数据安全 Bug

| #    | 故障                                                         | 文件                 |
| ---- | ------------------------------------------------------------ | -------------------- |
| 12   | `delete` 应为 `delete[]`（new[] 分配的数组）                 | `osd-device.cpp`     |
| 13   | `dequeueFrame()` 中 QBUF 后 buffer 仍被读取（use-after-requeue） | `camera_manager.cpp` |
| 14   | 双缓冲两个 `atomic store` 非原子组合（数据竞态）             | `vision_system.cpp`  |
| 15   | 信号处理函数中调用 `stop()`（复杂对象操作，UB）              | `main.cpp`           |
| 16   | `getPoints()` 返回内部引用无锁保护                           | `point_cloud.cpp`    |

### 🟡 线程安全问题

| #    | 故障                                               | 文件                    |
| ---- | -------------------------------------------------- | ----------------------- |
| 17   | `active_` 读写锁保护不一致                         | `hdr_controller.cpp`    |
| 18   | `emergencyCallback_` / `restartCallback_` 无锁保护 | `exception_handler.cpp` |
| 19   | `stateCallback_` std::function 无锁跨线程读写      | `hand_interface.cpp`    |
| 20   | cooldown 检查 TOCTOU 竞态                          | `exception_handler.cpp` |
| 21   | Logger 析构未加锁                                  | `logger.cpp`            |
| 22   | 日志时间戳锁外生成导致乱序                         | `logger.cpp`            |
| 23   | `getTotalElapsedMs()` 读 `startupTime_` 无锁       | `profiler.cpp`          |

### 🟡 逻辑与功能 Bug

| #    | 故障                                                       | 文件                              |
| ---- | ---------------------------------------------------------- | --------------------------------- |
| 24   | `sleep(0.25)` / `sleep(0.2)` 被截断为 0                    | `osd-device.cpp`, `demo_face.cpp` |
| 25   | `computePercentiles()` p5==0 判断缺陷                      | `hdr_controller.cpp`              |
| 26   | `resizeBilinear()` dstW==1 时除零                          | `image_utils.cpp`                 |
| 27   | `bilateralFilter()` 缺少左右边界处理                       | `image_utils.cpp`                 |
| 28   | `adaptiveThreshold()` 计算了无用积分图（浪费 3.5MB + CPU） | `image_utils.cpp`                 |
| 29   | `clahe()` 注释说双线性插值但实际未实现（块状伪影）         | `image_utils.cpp`                 |
| 30   | NEON resize 只做了 store，计算全是标量（伪加速）           | `image_utils.cpp`                 |
| 31   | `findBestDirection()` 跳过了边界扇区                       | `obstacle_avoidance.cpp`          |
| 32   | `adaptHDR()` 空壳函数，HDRController 未实例化              | `vision_system.cpp`               |
| 33   | `safeExecute` 静默吞掉错误，返回默认值                     | `exception_handler.h`             |

### 🟡 性能问题（90fps 预算 ~11ms/帧）

| #    | 故障                                          | 文件                     | 估算耗时       |
| ---- | --------------------------------------------- | ------------------------ | -------------- |
| 34   | fallback 深度估计全分辨率 3 遍遍历            | `depth_estimator.cpp`    | ~50ms          |
| 35   | `trackPatch()` 4 层嵌套暴力搜索 200 特征点    | `position_estimator.cpp` | ~30ms          |
| 36   | 灰度→3 通道 CPU 复制（320×320×3）             | `yolov8_detector.cpp`    | ~5ms           |
| 37   | 串口帧传输 160×90@115200baud                  | `video_output.cpp`       | ~1.25s/帧      |
| 38   | Profiler 每次 begin/end 都做 map 查找 + mutex | `profiler.cpp`           | ~0.5ms/帧      |
| 39   | ScopedTimer 每次构造分配 std::string 堆内存   | `profiler.h`             | 微秒级但高频   |
| 40   | 每条日志都 flush 到文件（flash 写入）         | `logger.cpp`             | 不定，阻塞风险 |

### 🟢 代码规范 / 死代码

| #    | 故障                                   | 文件                     |
| ---- | -------------------------------------- | ------------------------ |
| 41   | `resizeBuffer_` 声明未使用             | `yolov8_detector.h`      |
| 42   | `computeDescriptor()` 实现但从未调用   | `position_estimator.cpp` |
| 43   | 调试用全局 static tensor + save_tensor | `scrfd_gray.cpp`         |
| 44   | `save_tensor_buffer` 声明无定义        | `scrfd_gray.cpp`         |
| 45   | `img_width`/`img_height` 声明未使用    | `pipeline_image.cpp`     |
| 46   | `static int queryCount` 跨实例共享     | `hand_interface.cpp`     |
| 47   | `cmdQueue_` 无上限                     | `hand_interface.cpp`     |
| 48   | `ensureDir()` 不支持多级目录           | `logger.cpp`             |
| 49   | `M_PI` 可能未定义                      | `obstacle_avoidance.cpp` |
| 50   | GPIO 编号硬编码                        | `camera_manager.cpp`     |

------

## 三、为什么不能在开发板上跑起来 —— 根因链

```
根本原因
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
我的代码是一套 "通用 Linux 视觉系统"，
而目标平台是 "SmartSens A1 SSNE NPU 嵌入式开发板"。
两者的硬件抽象层完全不同。
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

因果链：

① SSNE 硬件未初始化 (缺少 ssne_init)
    │
    ├─② Sensor 不可用 (V4L2 打不开 SSNE 的 sensor 设备)
    │   └─ Camera fallback 到 synthetic mode → 只有假帧
    │
    ├─③ NPU 不可用 (ONNX Runtime 在 A1 上不存在或极慢)
    │   └─ 检测器 ready_=false → 返回空结果
    │   └─ 深度估计 fallback 到 CPU Sobel → 50ms/帧，远超预算
    │
    ├─④ Pipeline 不可用 (CPU 软件预处理替代硬件 pipeline)
    │   └─ resize+crop+normalize 全在 CPU → 帧率暴跌
    │
    └─⑤ OSD 不可用 (framebuffer 直写格式不匹配)
        └─ 显示乱码或黑屏

最终结果：程序可能能"启动"，但只能用合成帧+空检测结果+乱码显示空转。
         本质上没有任何真实的视觉功能在工作。
```

------

## 四、飞凌微 A1 开发板引脚图（ZX-PZ2.54-2-25PZZ 连接器）

```
                    ZX-PZ2.54-2-25PZZ 连接器
                    ========================
                    （从板子正面看，缺口朝上）

          奇数引脚（左列）          偶数引脚（右列）
         ┌──────────────────────┬──────────────────────┐
  Pin  1 │  I2C0 SDAS  ★       │  I2C0 SCLS  ★        │ Pin  2
         ├──────────────────────┼──────────────────────┤
  Pin  3 │  1V8 (电源)          │  1V8 (电源)           │ Pin  4
         ├──────────────────────┼──────────────────────┤
  Pin  5 │  CSI0_D0N            │  CSI0_D0P             │ Pin  6
         ├──────────────────────┼──────────────────────┤
  Pin  7 │  CSI0_D1N            │  CSI0_D1P             │ Pin  8
         ├──────────────────────┼──────────────────────┤
  Pin  9 │  CSI0_CLKN           │  CSI0_CLKP            │ Pin 10
         ├──────────────────────┼──────────────────────┤
  Pin 11 │  CSI0_D2N            │  CSI0_D2P             │ Pin 12
         ├──────────────────────┼──────────────────────┤
  Pin 13 │  CSI0_D3N            │  CSI0_D3P             │ Pin 14
         ├──────────────────────┼──────────────────────┤
  Pin 15 │  UART0 TX  ★        │  UART0 RX  ★         │ Pin 16
         ├──────────────────────┼──────────────────────┤
  Pin 17 │  GPIO8 / PWM  ★     │  GPIO9 / PWM  ★      │ Pin 18
         ├──────────────────────┼──────────────────────┤
  Pin 19 │  GPIO10 / PWM  ★    │  (Reserved)           │ Pin 20
         ├──────────────────────┼──────────────────────┤
  Pin 21 │  (Reserved)          │  (Reserved)           │ Pin 22
         ├──────────────────────┼──────────────────────┤
  Pin 23 │  (Reserved)          │  (Reserved)           │ Pin 24
         ├──────────────────────┼──────────────────────┤
  Pin 25 │  I2C2 SDAM2  ★      │  I2C2 SCLM2  ★       │ Pin 26
         ├──────────────────────┼──────────────────────┤
  Pin 27 │  (Reserved)          │  (Reserved)           │ Pin 28
         ├──────────────────────┼──────────────────────┤
  Pin 29 │  (Reserved)          │  (Reserved)           │ Pin 30
         ├──────────────────────┼──────────────────────┤
  Pin 31 │  (Reserved)          │  (Reserved)           │ Pin 32
         ├──────────────────────┼──────────────────────┤
  Pin 33 │  GND  ⏚             │  GND  ⏚              │ Pin 34
         ├──────────────────────┼──────────────────────┤
  Pin 35 │  CAM0_MCLK           │  (Reserved)           │ Pin 36
         ├──────────────────────┼──────────────────────┤
  Pin 37 │  CAM0_RESET          │  (Reserved)           │ Pin 38
         ├──────────────────────┼──────────────────────┤
  Pin 39 │  CAM0_TRIG0  ★      │  (Reserved)           │ Pin 40
         ├──────────────────────┼──────────────────────┤
  Pin 41 │  CAM0_TRIG1  ★      │  (Reserved)           │ Pin 42
         ├──────────────────────┼──────────────────────┤
  Pin 43 │  CAM0_STROBE0        │  (Reserved)           │ Pin 44
         ├──────────────────────┼──────────────────────┤
  Pin 45 │  CAM0_STROBE1        │  (Reserved)           │ Pin 46
         ├──────────────────────┼──────────────────────┤
  Pin 47 │  GND  ⏚             │  GND  ⏚              │ Pin 48
         ├──────────────────────┼──────────────────────┤
  Pin 49 │  (Reserved)          │  (Reserved)           │ Pin 50
         └──────────────────────┴──────────────────────┘

  ★ = 可分配给外设       ⏚ = 接地
  注意：所有 IO 为 1.8V 电平域！外设模块需加电平转换器！
```

------

## 五、引脚分配方案（零冲突）

```
┌─────────────────────────────────────────────────────────────────┐
│                    I2C0 总线 (Pin 1 + Pin 2)                     │
│                                                                  │
│   ┌──────────┐    ┌──────────┐    ┌──────────┐                  │
│   │  SSD1306  │    │ VL53L1X  │    │ PCA9685  │                  │
│   │   OLED    │    │   ToF    │    │ 伺服驱动  │                 │
│   │ 0x3C      │    │ 0x29     │    │ 0x40     │                  │
│   │ 128×64    │    │ 0-4m     │    │ 16路PWM  │                  │
│   └──────────┘    └──────────┘    └─────┬────┘                  │
│                                         │                        │
│                                    ┌────┴────┐                   │
│                                    │ MG996R  │×5-6               │
│                                    │ 伺服舵机 │                   │
│                                    └─────────┘                   │
├──────────────────────────────────────────────────────────────────┤
│                   UART0 (Pin 15 TX + Pin 16 RX)                  │
│                                                                  │
│   ┌──────────────┐                                               │
│   │ TFmini-S     │  (若已选 VL53L1X ToF，此口可接第二灵巧手      │
│   │ 激光雷达     │   或其他 UART 设备)                            │
│   │ 0.1-12m      │                                               │
│   └──────────────┘                                               │
├──────────────────────────────────────────────────────────────────┤
│              GPIO / PWM (Pin 17, 18, 19, 39)                     │
│                                                                  │
│   Pin 17 (GPIO8)  ──→  PWM_Left   ──→ ┌─────────┐              │
│   Pin 18 (GPIO9)  ──→  DIR_Left   ──→ │ TB6612  │──→ 左电机    │
│   Pin 19 (GPIO10) ──→  PWM_Right  ──→ │ 电机    │──→ 右电机    │
│   Pin 39 (TRIG0)  ──→  DIR_Right  ──→ │ 驱动板  │              │
│                                        └─────────┘              │
├──────────────────────────────────────────────────────────────────┤
│              I2C2 总线 (Pin 25 + Pin 26) — 预留扩展              │
│                                                                  │
│   ┌──────────┐    ┌──────────┐                                   │
│   │ MPU6050  │    │ 第2块     │                                  │
│   │  IMU     │    │ PCA9685  │  (双臂/多手指扩展)                │
│   │ 0x68     │    │ 0x41     │                                   │
│   └──────────┘    └──────────┘                                   │
├──────────────────────────────────────────────────────────────────┤
│                        电源架构                                   │
│                                                                  │
│   板载 1V8 ─→ 仅给逻辑电平转换器 HV 侧                          │
│                     │                                            │
│              ┌──────┴──────┐                                     │
│              │ TXS0102/04  │  电平转换器 (1.8V ↔ 3.3V)          │
│              │ 或 MOSFET   │  淘宝 ~5元                          │
│              └──────┬──────┘                                     │
│                     │                                            │
│   外部 5V 2A ──→ OLED/ToF/PCA9685 VCC                           │
│   外部 7.4V  ──→ 伺服/电机 (大电流独立供电)                      │
│   所有 GND   ──→ Pin 33/34/47/48 (共地)                         │
└──────────────────────────────────────────────────────────────────┘
```

------

## 六、代码修复路线图 + 更多功能开发方案

### 第一阶段：让代码能在开发板上跑起来（2-3 天）

```
优先级 P0 — 必须完成

 ① 新增 ssne_wrapper 模块
    ┌─────────────────────────────────────────────┐
    │  ssne_wrapper.h / .cpp                       │
    │                                              │
    │  class SSNEWrapper {                         │
    │      bool init();           // ssne_init()   │
    │      bool loadModel(path);  // ssne_create() │
    │      bool infer(input);     // ssne_inference │
    │      void getOutput(out);   // ssne_getoutput │
    │      void release();        // ssne_release() │
    │  };                                          │
    └─────────────────────────────────────────────┘

 ② 重写 camera_manager
    - 删除 V4L2 代码
    - 改用 SSNE sensor API: sensor_init() → GetImageData()
    - 删除 synthetic mode

 ③ 重写 yolov8_detector / depth_estimator 推理核心
    - 删除 ONNX Runtime 依赖
    - 改用 ssne_create() + ssne_inference() + ssne_getoutput()
    - 前处理改用 SSNE pipeline: OnlineSetCrop + OnlineSetOutputImage
    - 模型文件需用 SSNE 工具链编译为 .bin 格式

 ④ 重写 video_output
    - 删除 framebuffer 直写
    - 改用 OSD API: osd_init() → osd_bindlayer() → osd_display()

 ⑤ 修复编译错误
    - vision_system.cpp: captureFrame() → getFrame()
    - vision_system.h: 添加 #include "tracker.h" 或前向声明 BBox
    - CMakeLists.txt: 修复 message() 语法
    - profiler.h: 修复 PROFILE_SCOPE 宏
    - exception_handler.h: 添加 #include <thread>
```

### 第二阶段：修复 Bug 和线程安全（1-2 天）

```
优先级 P1

 ⑥ 信号处理：只设 g_running = false，stop() 移到主循环后
 ⑦ 删除运行时自动重启逻辑
 ⑧ osd-device.cpp: delete → delete[]
 ⑨ 修复双缓冲竞态（改用 mutex 或单 atomic index）
 ⑩ 修复所有 callback 无锁保护问题
 ⑪ 修复 image_utils 中的 bilateralFilter 边界、resizeBilinear 除零
```

### 第三阶段：外设集成 + 功能扩展（1-2 周）

```
优先级 P2 — 基于引脚分配方案

┌──────────────────────────────────────────────────────────────┐
│  新增 core/peripheral/ 目录                                   │
│                                                               │
│  ├── i2c_bus.h/cpp          // A1 SDK I2C 封装               │
│  ├── oled_display.h/cpp     // SSD1306 驱动 (I2C0, 0x3C)    │
│  ├── tof_sensor.h/cpp       // VL53L1X 驱动 (I2C0, 0x29)    │
│  ├── servo_controller.h/cpp // PCA9685 驱动 (I2C0, 0x40)    │
│  ├── lidar_sensor.h/cpp     // TFmini-S 驱动 (UART0)        │
│  ├── motor_driver.h/cpp     // TB6612 驱动 (GPIO8-10, TRIG0)│
│  └── level_shifter.h        // 电平转换注意事项文档           │
└──────────────────────────────────────────────────────────────┘

功能集成流水线：

  Sensor(SSNE) ──→ NPU检测 ──→ 深度估计 ──→ ┬─→ OLED 显示状态
                                              ├─→ ToF 补充近距离盲区
                                              ├─→ 点云生成
                                              ├─→ 避障 ──→ TB6612 底盘控制
                                              └─→ 抓取 ──→ PCA9685 灵巧手控制
```

### 各外设集成代码框架

```cpp
// ===== oled_display.h =====
class OLEDDisplay {
public:
    bool init(int i2cBus = 0, uint8_t addr = 0x3C);
    void clear();
    void drawText(int x, int y, const char* text);
    void drawRect(int x1, int y1, int x2, int y2);
    void showStats(float fps, int objCount, float nearestDist);
    void update();  // 刷新到屏幕
    void release();
};

// ===== tof_sensor.h =====
class ToFSensor {
public:
    bool init(int i2cBus = 0, uint8_t addr = 0x29);
    int getDistanceMM();        // 返回距离（毫米），0-4000
    bool isRangeValid();
    void setTimingBudget(int ms);  // 精度 vs 速度权衡
    void release();
};

// ===== servo_controller.h =====
class ServoController {
public:
    bool init(int i2cBus = 0, uint8_t addr = 0x40);
    void setAngle(int channel, float angleDeg);     // 0-270°
    void setFingerGrasp(float openness);             // 0.0=握紧, 1.0=张开
    void setPredefinedPose(GraspPose pose);           // 预定义手势
    void release();
};
enum class GraspPose { OPEN, CLOSE, PINCH, POINT, PEACE };

// ===== motor_driver.h =====
class MotorDriver {
public:
    bool init(int pwmPinL = 8, int dirPinL = 9,
              int pwmPinR = 10, int dirPinR = 39);   // 对应引脚分配
    void setSpeed(float left, float right);           // -1.0 ~ +1.0
    void stop();
    void emergencyStop();
    void release();
};
```

### 第四阶段：高级功能（可选，2-4 周）

```
┌──────────────────────────────────────────────────────────────┐
│  IMU 融合定位 (MPU6050 挂 I2C2)                               │
│  ├── 6 轴姿态估计（加速度计 + 陀螺仪）                         │
│  ├── 与视觉里程计融合 → 更精确的 3D 定位                       │
│  └── 引脚：Pin 25 (SDAM2) + Pin 26 (SCLM2), 地址 0x68       │
│                                                               │
│  视觉伺服闭环                                                  │
│  ├── NPU 检测目标 → 计算 3D 坐标                              │
│  ├── ToF 补充距离 → 精确深度                                   │
│  ├── PCA9685 控制手指 → 实时调整抓取姿态                       │
│  └── OLED 实时反馈状态                                         │
│                                                               │
│  自主导航                                                      │
│  ├── TFmini-S 前向避障 (12m)                                  │
│  ├── NPU 目标检测 ( pedestrian / obstacle )                   │
│  ├── 点云建图 (简化版)                                         │
│  └── TB6612 底盘闭环控制                                       │
└──────────────────────────────────────────────────────────────┘
```

------

## 七、修改优先级总览

```
紧急度          任务                                      工作量
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
🔴 立即     替换 V4L2 → SSNE sensor API                  1 天
🔴 立即     替换 ONNX Runtime → SSNE NPU                 1 天
🔴 立即     添加 ssne_init()/ssne_release()              0.5 天
🔴 立即     替换 framebuffer → SSNE OSD                  0.5 天
🔴 立即     修复 5 个编译错误                             0.5 天
🟡 本周     修复信号处理 / 双缓冲竞态 / delete[]         1 天
🟡 本周     修复所有线程安全问题                          1 天
🟡 本周     修复 image_utils Bug                         0.5 天
🟢 下周     购买外设 + 焊接电平转换器                     1 天
🟢 下周     实现 OLED + ToF + PCA9685 驱动               2-3 天
🟢 月内     实现底盘控制 + 视觉伺服闭环                   1-2 周
🔵 可选     IMU 融合 + 自主导航                          2-4 周
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━
总计基础可用：                                            ~5 天
总计完整功能：                                            ~4-6 周
```