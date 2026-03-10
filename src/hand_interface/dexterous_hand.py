"""
灵巧手外设接口模块
提供与灵巧手外设通信的抽象接口
支持串口、USB、网络等多种通信方式
预留灵巧手控制的完整API
"""

import time
import threading
import logging
from abc import ABC, abstractmethod
from dataclasses import dataclass, field
from typing import Optional, Dict, List, Tuple, Callable
from enum import Enum

logger = logging.getLogger(__name__)


class HandConnectionType(Enum):
    """通信方式"""
    SERIAL = "serial"
    USB = "usb"
    TCP = "tcp"
    UDP = "udp"
    MOCK = "mock"


class HandState(Enum):
    """灵巧手状态"""
    DISCONNECTED = "disconnected"
    CONNECTING = "connecting"
    CONNECTED = "connected"
    BUSY = "busy"
    ERROR = "error"
    CALIBRATING = "calibrating"


class GripType(Enum):
    """抓取类型"""
    POWER_GRIP = "power_grip"           # 力抓
    PRECISION_GRIP = "precision_grip"   # 精确抓取
    PINCH = "pinch"                     # 捏取
    LATERAL = "lateral"                 # 侧捏
    HOOK = "hook"                       # 钩握
    OPEN = "open"                       # 张开
    CUSTOM = "custom"                   # 自定义


@dataclass
class FingerState:
    """单指状态"""
    angle: float = 0.0            # 关节角度 (度)
    force: float = 0.0            # 指尖力 (N)
    velocity: float = 0.0         # 角速度 (度/秒)
    contact: bool = False         # 是否接触物体
    temperature: float = 25.0     # 温度 (°C)


@dataclass
class HandStatus:
    """灵巧手完整状态"""
    state: HandState = HandState.DISCONNECTED
    fingers: Dict[str, FingerState] = field(default_factory=lambda: {
        'thumb': FingerState(),
        'index': FingerState(),
        'middle': FingerState(),
        'ring': FingerState(),
        'pinky': FingerState()
    })
    wrist_angle: float = 0.0     # 手腕角度
    grip_force: float = 0.0      # 总握力
    battery_level: float = 100.0  # 电量百分比
    error_code: int = 0
    timestamp: float = 0.0


@dataclass
class HandCommand:
    """灵巧手控制指令"""
    grip_type: GripType = GripType.OPEN
    finger_angles: Dict[str, float] = field(default_factory=dict)
    grip_force: float = 0.0       # 目标握力 (N)
    speed: float = 50.0           # 运动速度 (%)
    wrist_angle: float = 0.0      # 手腕目标角度
    timeout: float = 5.0          # 超时时间 (秒)


class HandCommunicator(ABC):
    """通信抽象基类"""

    @abstractmethod
    def connect(self, **kwargs) -> bool:
        pass

    @abstractmethod
    def disconnect(self):
        pass

    @abstractmethod
    def send(self, data: bytes) -> bool:
        pass

    @abstractmethod
    def receive(self, timeout: float = 1.0) -> Optional[bytes]:
        pass

    @abstractmethod
    def is_connected(self) -> bool:
        pass


class SerialCommunicator(HandCommunicator):
    """串口通信"""

    def __init__(self):
        self._serial = None
        self._connected = False

    def connect(self, port: str = '/dev/ttyUSB0',
                baudrate: int = 115200, **kwargs) -> bool:
        try:
            import serial
            self._serial = serial.Serial(
                port=port,
                baudrate=baudrate,
                timeout=kwargs.get('timeout', 1.0),
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE
            )
            self._connected = True
            logger.info(f"串口连接成功: {port}@{baudrate}")
            return True
        except Exception as e:
            logger.error(f"串口连接失败: {e}")
            self._connected = False
            return False

    def disconnect(self):
        if self._serial and self._serial.is_open:
            self._serial.close()
        self._connected = False

    def send(self, data: bytes) -> bool:
        if self._serial and self._serial.is_open:
            self._serial.write(data)
            return True
        return False

    def receive(self, timeout: float = 1.0) -> Optional[bytes]:
        if self._serial and self._serial.is_open:
            self._serial.timeout = timeout
            data = self._serial.readline()
            return data if data else None
        return None

    def is_connected(self) -> bool:
        return self._connected and self._serial is not None and self._serial.is_open


class TCPCommunicator(HandCommunicator):
    """TCP通信"""

    def __init__(self):
        self._socket = None
        self._connected = False

    def connect(self, host: str = '192.168.1.100',
                port: int = 9000, **kwargs) -> bool:
        try:
            import socket
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(kwargs.get('timeout', 5.0))
            self._socket.connect((host, port))
            self._connected = True
            logger.info(f"TCP连接成功: {host}:{port}")
            return True
        except Exception as e:
            logger.error(f"TCP连接失败: {e}")
            return False

    def disconnect(self):
        if self._socket:
            self._socket.close()
        self._connected = False

    def send(self, data: bytes) -> bool:
        if self._socket and self._connected:
            try:
                self._socket.sendall(data)
                return True
            except Exception:
                return False
        return False

    def receive(self, timeout: float = 1.0) -> Optional[bytes]:
        if self._socket and self._connected:
            try:
                self._socket.settimeout(timeout)
                return self._socket.recv(4096)
            except Exception:
                return None
        return None

    def is_connected(self) -> bool:
        return self._connected


class MockCommunicator(HandCommunicator):
    """模拟通信 (开发测试用)"""

    def __init__(self):
        self._connected = False
        self._last_sent = None

    def connect(self, **kwargs) -> bool:
        self._connected = True
        logger.info("模拟灵巧手通信已连接")
        return True

    def disconnect(self):
        self._connected = False

    def send(self, data: bytes) -> bool:
        self._last_sent = data
        logger.debug(f"Mock发送: {len(data)} bytes")
        return True

    def receive(self, timeout: float = 1.0) -> Optional[bytes]:
        # 模拟返回状态
        import json
        status = {
            'state': 'ok',
            'fingers': {
                'thumb': 0, 'index': 0, 'middle': 0,
                'ring': 0, 'pinky': 0
            },
            'force': 0.0
        }
        return json.dumps(status).encode()

    def is_connected(self) -> bool:
        return self._connected


class DexterousHandInterface:
    """
    灵巧手外设接口主类
    提供高级API：抓取、释放、预设手势等
    """

    def __init__(self, config: Optional[Dict] = None):
        config = config or {}

        self.connection_type = HandConnectionType(
            config.get('connection_type', 'mock')
        )
        self.status = HandStatus()
        self._callbacks: Dict[str, List[Callable]] = {
            'on_connect': [],
            'on_disconnect': [],
            'on_grab_complete': [],
            'on_error': [],
            'on_contact': [],
        }

        # 创建通信器
        self._communicator = self._create_communicator()

        # 预设手势
        self._presets: Dict[str, HandCommand] = self._load_presets(config)

        # 状态监控线程
        self._monitor_thread: Optional[threading.Thread] = None
        self._monitoring = False
        self._lock = threading.Lock()

        # 通信协议参数
        self._protocol_version = config.get('protocol_version', '1.0')

        logger.info(f"灵巧手接口初始化: 通信={self.connection_type.value}")

    def _create_communicator(self) -> HandCommunicator:
        """创建通信器"""
        if self.connection_type == HandConnectionType.SERIAL:
            return SerialCommunicator()
        elif self.connection_type == HandConnectionType.TCP:
            return TCPCommunicator()
        elif self.connection_type == HandConnectionType.MOCK:
            return MockCommunicator()
        else:
            logger.warning(f"未支持的通信类型: {self.connection_type}, 使用Mock")
            return MockCommunicator()

    def _load_presets(self, config: Dict) -> Dict[str, HandCommand]:
        """加载预设手势"""
        presets = {
            'open': HandCommand(
                grip_type=GripType.OPEN,
                finger_angles={'thumb': 0, 'index': 0, 'middle': 0, 'ring': 0, 'pinky': 0},
                grip_force=0
            ),
            'power_grip': HandCommand(
                grip_type=GripType.POWER_GRIP,
                finger_angles={'thumb': 90, 'index': 90, 'middle': 90, 'ring': 90, 'pinky': 90},
                grip_force=50
            ),
            'precision_grip': HandCommand(
                grip_type=GripType.PRECISION_GRIP,
                finger_angles={'thumb': 60, 'index': 60, 'middle': 0, 'ring': 0, 'pinky': 0},
                grip_force=20
            ),
            'pinch': HandCommand(
                grip_type=GripType.PINCH,
                finger_angles={'thumb': 45, 'index': 45, 'middle': 0, 'ring': 0, 'pinky': 0},
                grip_force=15
            ),
            'point': HandCommand(
                grip_type=GripType.CUSTOM,
                finger_angles={'thumb': 90, 'index': 0, 'middle': 90, 'ring': 90, 'pinky': 90},
                grip_force=0
            ),
            'thumbs_up': HandCommand(
                grip_type=GripType.CUSTOM,
                finger_angles={'thumb': 0, 'index': 90, 'middle': 90, 'ring': 90, 'pinky': 90},
                grip_force=0
            ),
        }

        # 加载自定义预设
        custom_presets = config.get('custom_presets', {})
        for name, params in custom_presets.items():
            presets[name] = HandCommand(**params)

        return presets

    def connect(self, **kwargs) -> bool:
        """
        连接灵巧手

        Args:
            **kwargs: 通信参数 (port, baudrate, host, etc.)

        Returns:
            是否连接成功
        """
        self.status.state = HandState.CONNECTING
        logger.info("正在连接灵巧手...")

        try:
            success = self._communicator.connect(**kwargs)

            if success:
                self.status.state = HandState.CONNECTED
                self._start_monitoring()
                self._trigger_callback('on_connect')
                logger.info("灵巧手连接成功")
                return True
            else:
                self.status.state = HandState.ERROR
                return False

        except Exception as e:
            self.status.state = HandState.ERROR
            logger.error(f"灵巧手连接异常: {e}")
            self._trigger_callback('on_error', str(e))
            return False

    def disconnect(self):
        """断开连接"""
        self._stop_monitoring()
        self._communicator.disconnect()
        self.status.state = HandState.DISCONNECTED
        self._trigger_callback('on_disconnect')
        logger.info("灵巧手已断开")

    def execute_command(self, command: HandCommand) -> bool:
        """
        执行控制指令

        Args:
            command: 控制指令

        Returns:
            是否成功
        """
        if not self._communicator.is_connected():
            logger.error("灵巧手未连接")
            return False

        self.status.state = HandState.BUSY

        try:
            # 序列化指令
            data = self._serialize_command(command)

            # 发送
            success = self._communicator.send(data)

            if success:
                # 等待确认
                response = self._communicator.receive(timeout=command.timeout)
                if response:
                    self.status.state = HandState.CONNECTED
                    return True

            self.status.state = HandState.CONNECTED
            return success

        except Exception as e:
            self.status.state = HandState.ERROR
            logger.error(f"指令执行异常: {e}")
            return False

    def execute_preset(self, preset_name: str) -> bool:
        """
        执行预设手势

        Args:
            preset_name: 预设名称

        Returns:
            是否成功
        """
        if preset_name not in self._presets:
            logger.error(f"未知预设: {preset_name}, 可用: {list(self._presets.keys())}")
            return False

        logger.info(f"执行预设手势: {preset_name}")
        return self.execute_command(self._presets[preset_name])

    def grip(self, force: float = 50.0,
             grip_type: GripType = GripType.POWER_GRIP) -> bool:
        """
        抓取

        Args:
            force: 目标握力 (N)
            grip_type: 抓取类型
        """
        cmd = HandCommand(
            grip_type=grip_type,
            grip_force=force,
            speed=80
        )
        success = self.execute_command(cmd)
        if success:
            self._trigger_callback('on_grab_complete', grip_type)
        return success

    def release(self, speed: float = 50.0) -> bool:
        """释放"""
        cmd = HandCommand(
            grip_type=GripType.OPEN,
            grip_force=0,
            speed=speed
        )
        return self.execute_command(cmd)

    def set_finger(self, finger: str, angle: float,
                    force: float = 0) -> bool:
        """
        控制单指

        Args:
            finger: 手指名称 (thumb/index/middle/ring/pinky)
            angle: 目标角度
            force: 限制力
        """
        cmd = HandCommand(
            grip_type=GripType.CUSTOM,
            finger_angles={finger: angle},
            grip_force=force
        )
        return self.execute_command(cmd)

    def calibrate(self) -> bool:
        """校准灵巧手"""
        logger.info("开始灵巧手校准...")
        self.status.state = HandState.CALIBRATING

        # 发送校准指令
        cmd = b'\x01\x00\xCA\x11'  # 校准协议头
        success = self._communicator.send(cmd)

        if success:
            response = self._communicator.receive(timeout=10.0)
            if response:
                self.status.state = HandState.CONNECTED
                logger.info("校准完成")
                return True

        self.status.state = HandState.ERROR
        logger.error("校准失败")
        return False

    def get_status(self) -> HandStatus:
        """获取当前状态"""
        with self._lock:
            return self.status

    def _serialize_command(self, command: HandCommand) -> bytes:
        """序列化指令为通信协议数据"""
        import json
        import struct

        payload = {
            'type': command.grip_type.value,
            'fingers': command.finger_angles,
            'force': command.grip_force,
            'speed': command.speed,
            'wrist': command.wrist_angle
        }

        json_data = json.dumps(payload).encode('utf-8')

        # 协议帧: [头(2B)] [长度(4B)] [数据] [校验(2B)]
        header = b'\xAA\x55'
        length = struct.pack('<I', len(json_data))
        checksum = struct.pack('<H', sum(json_data) & 0xFFFF)

        return header + length + json_data + checksum

    def _start_monitoring(self):
        """启动状态监控"""
        self._monitoring = True
        self._monitor_thread = threading.Thread(
            target=self._monitor_loop, daemon=True, name="HandMonitor"
        )
        self._monitor_thread.start()

    def _stop_monitoring(self):
        """停止状态监控"""
        self._monitoring = False
        if self._monitor_thread and self._monitor_thread.is_alive():
            self._monitor_thread.join(timeout=2.0)

    def _monitor_loop(self):
        """状态监控循环"""
        while self._monitoring and self._communicator.is_connected():
            try:
                # 发送状态查询
                self._communicator.send(b'\xAA\x55\x00\x00\x00\x01\x51\x00\x51')

                response = self._communicator.receive(timeout=0.5)
                if response:
                    self._parse_status(response)

            except Exception as e:
                logger.debug(f"状态监控异常: {e}")

            time.sleep(0.1)  # 10Hz监控频率

    def _parse_status(self, data: bytes):
        """解析状态数据"""
        try:
            import json
            status_data = json.loads(data.decode('utf-8', errors='ignore'))

            with self._lock:
                if 'fingers' in status_data:
                    for finger, value in status_data['fingers'].items():
                        if finger in self.status.fingers:
                            self.status.fingers[finger].angle = float(value)

                if 'force' in status_data:
                    self.status.grip_force = float(status_data['force'])

                self.status.timestamp = time.time()

        except Exception:
            pass

    def register_callback(self, event: str, callback: Callable):
        """注册回调"""
        if event in self._callbacks:
            self._callbacks[event].append(callback)

    def _trigger_callback(self, event: str, *args):
        """触发回调"""
        for cb in self._callbacks.get(event, []):
            try:
                cb(*args)
            except Exception as e:
                logger.error(f"回调异常 [{event}]: {e}")

    @property
    def is_connected(self) -> bool:
        return self._communicator.is_connected()

    def __del__(self):
        try:
            self.disconnect()
        except Exception:
            pass