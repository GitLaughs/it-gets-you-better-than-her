"""
异常处理模块
统一的异常捕获、日志记录、恢复策略
支持分级异常处理和自动重试
"""

import traceback
import time
import threading
import functools
import logging
from typing import Optional, Callable, Dict, List, Type, Any
from enum import Enum
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)


class Severity(Enum):
    """异常严重等级"""
    DEBUG = 0
    INFO = 1
    WARNING = 2
    ERROR = 3
    CRITICAL = 4
    FATAL = 5


class RecoveryAction(Enum):
    """恢复动作"""
    IGNORE = "ignore"
    RETRY = "retry"
    RESTART_MODULE = "restart_module"
    RESTART_SYSTEM = "restart_system"
    EMERGENCY_STOP = "emergency_stop"
    LOG_ONLY = "log_only"
    CALLBACK = "callback"


@dataclass
class ExceptionRecord:
    """异常记录"""
    exception_type: str
    message: str
    severity: Severity
    module: str
    traceback_str: str
    timestamp: float
    recovery_action: RecoveryAction
    resolved: bool = False
    retry_count: int = 0


@dataclass
class ExceptionRule:
    """异常处理规则"""
    exception_types: List[Type[Exception]]
    severity: Severity = Severity.ERROR
    recovery_action: RecoveryAction = RecoveryAction.LOG_ONLY
    max_retries: int = 3
    retry_delay: float = 1.0
    callback: Optional[Callable] = None
    cooldown: float = 0.0  # 同类异常冷却时间


class ExceptionHandler:
    """
    统一异常处理器
    管理所有模块的异常捕获和恢复
    """

    def __init__(self, config: Optional[Dict] = None):
        config = config or {}

        self._records: List[ExceptionRecord] = []
        self._max_records = config.get('max_records', 1000)
        self._rules: Dict[str, ExceptionRule] = {}
        self._lock = threading.Lock()

        # 全局回调
        self._global_callbacks: List[Callable] = []

        # 异常计数
        self._exception_counts: Dict[str, int] = {}
        self._last_exception_time: Dict[str, float] = {}

        # 系统级回调
        self._emergency_stop_callback: Optional[Callable] = None
        self._restart_callback: Optional[Callable] = None

        # 默认规则
        self._setup_default_rules()

        logger.info("异常处理器初始化完成")

    def _setup_default_rules(self):
        """设置默认异常处理规则"""
        self.add_rule('camera_error', ExceptionRule(
            exception_types=[IOError, OSError],
            severity=Severity.ERROR,
            recovery_action=RecoveryAction.RETRY,
            max_retries=5,
            retry_delay=2.0
        ))

        self.add_rule('detection_error', ExceptionRule(
            exception_types=[RuntimeError, ValueError],
            severity=Severity.WARNING,
            recovery_action=RecoveryAction.RETRY,
            max_retries=3,
            retry_delay=0.5
        ))

        self.add_rule('memory_error', ExceptionRule(
            exception_types=[MemoryError],
            severity=Severity.CRITICAL,
            recovery_action=RecoveryAction.RESTART_MODULE,
            max_retries=1
        ))

        self.add_rule('keyboard_interrupt', ExceptionRule(
            exception_types=[KeyboardInterrupt],
            severity=Severity.INFO,
            recovery_action=RecoveryAction.EMERGENCY_STOP
        ))

        self.add_rule('generic', ExceptionRule(
            exception_types=[Exception],
            severity=Severity.ERROR,
            recovery_action=RecoveryAction.LOG_ONLY,
            max_retries=0
        ))

    def add_rule(self, name: str, rule: ExceptionRule):
        """添加异常处理规则"""
        self._rules[name] = rule

    def handle(self, exception: Exception,
               module: str = "unknown",
               context: Optional[Dict] = None) -> RecoveryAction:
        """
        处理异常

        Args:
            exception: 异常对象
            module: 来源模块
            context: 上下文信息

        Returns:
            建议的恢复动作
        """
        tb_str = traceback.format_exc()
        exc_type = type(exception).__name__
        exc_key = f"{module}:{exc_type}"

        # 查找匹配规则
        rule = self._find_rule(exception)

        # 检查冷却
        if rule.cooldown > 0:
            last_time = self._last_exception_time.get(exc_key, 0)
            if time.time() - last_time < rule.cooldown:
                return RecoveryAction.IGNORE

        # 记录
        record = ExceptionRecord(
            exception_type=exc_type,
            message=str(exception),
            severity=rule.severity,
            module=module,
            traceback_str=tb_str,
            timestamp=time.time(),
            recovery_action=rule.recovery_action
        )

        with self._lock:
            self._records.append(record)
            if len(self._records) > self._max_records:
                self._records.pop(0)

            self._exception_counts[exc_key] = (
                self._exception_counts.get(exc_key, 0) + 1
            )
            self._last_exception_time[exc_key] = time.time()

        # 日志
        log_msg = f"[{module}] {exc_type}: {exception}"
        if rule.severity == Severity.CRITICAL or rule.severity == Severity.FATAL:
            logger.critical(log_msg)
            logger.critical(tb_str)
        elif rule.severity == Severity.ERROR:
            logger.error(log_msg)
            logger.debug(tb_str)
        elif rule.severity == Severity.WARNING:
            logger.warning(log_msg)
        else:
            logger.info(log_msg)

        # 执行回调
        for callback in self._global_callbacks:
            try:
                callback(record)
            except Exception:
                pass

        if rule.callback:
            try:
                rule.callback(exception, context)
            except Exception:
                pass

        # 执行恢复动作
        self._execute_recovery(rule.recovery_action, module, context)

        return rule.recovery_action

    def _find_rule(self, exception: Exception) -> ExceptionRule:
        """查找匹配的规则"""
        for name, rule in self._rules.items():
            for exc_type in rule.exception_types:
                if isinstance(exception, exc_type):
                    return rule

        return self._rules.get('generic', ExceptionRule(
            exception_types=[Exception],
            recovery_action=RecoveryAction.LOG_ONLY
        ))

    def _execute_recovery(self, action: RecoveryAction,
                           module: str,
                           context: Optional[Dict]):
        """执行恢复动作"""
        if action == RecoveryAction.EMERGENCY_STOP:
            logger.critical(f"执行紧急停止！触发模块: {module}")
            if self._emergency_stop_callback:
                self._emergency_stop_callback()

        elif action == RecoveryAction.RESTART_SYSTEM:
            logger.critical(f"请求系统重启！触发模块: {module}")
            if self._restart_callback:
                self._restart_callback()

        elif action == RecoveryAction.RESTART_MODULE:
            logger.warning(f"请求重启模块: {module}")
            if context and 'restart_func' in context:
                try:
                    context['restart_func']()
                except Exception as e:
                    logger.error(f"模块重启失败: {e}")

    def set_emergency_stop_callback(self, callback: Callable):
        """设置紧急停止回调"""
        self._emergency_stop_callback = callback

    def set_restart_callback(self, callback: Callable):
        """设置重启回调"""
        self._restart_callback = callback

    def register_global_callback(self, callback: Callable):
        """注册全局异常回调"""
        self._global_callbacks.append(callback)

    def get_records(self, severity: Optional[Severity] = None,
                     module: Optional[str] = None,
                     limit: int = 50) -> List[ExceptionRecord]:
        """获取异常记录"""
        with self._lock:
            records = list(self._records)

        if severity:
            records = [r for r in records if r.severity == severity]
        if module:
            records = [r for r in records if r.module == module]

        return records[-limit:]

    def get_statistics(self) -> Dict:
        """获取异常统计"""
        with self._lock:
            total = len(self._records)
            by_severity = {}
            for record in self._records:
                sev = record.severity.name
                by_severity[sev] = by_severity.get(sev, 0) + 1

            by_module = {}
            for record in self._records:
                mod = record.module
                by_module[mod] = by_module.get(mod, 0) + 1

        return {
            'total': total,
            'by_severity': by_severity,
            'by_module': by_module,
            'exception_counts': dict(self._exception_counts),
        }

    def clear(self):
        """清空记录"""
        with self._lock:
            self._records.clear()
            self._exception_counts.clear()


def safe_execute(func: Optional[Callable] = None,
                  handler: Optional[ExceptionHandler] = None,
                  module: str = "unknown",
                  max_retries: int = 0,
                  retry_delay: float = 1.0,
                  default_return: Any = None):
    """
    安全执行装饰器

    Usage:
        @safe_execute(module="detection", max_retries=3)
        def detect(frame):
            ...

        # 或直接使用全局handler
        @safe_execute
        def process():
            ...
    """
    def decorator(fn):
        @functools.wraps(fn)
        def wrapper(*args, **kwargs):
            nonlocal handler
            if handler is None:
                handler = _global_handler

            last_exception = None
            for attempt in range(max_retries + 1):
                try:
                    return fn(*args, **kwargs)
                except Exception as e:
                    last_exception = e
                    if handler:
                        action = handler.handle(e, module=module)
                        if action == RecoveryAction.EMERGENCY_STOP:
                            raise
                    if attempt < max_retries:
                        logger.info(f"重试 {attempt + 1}/{max_retries}: {fn.__name__}")
                        time.sleep(retry_delay)

            logger.error(f"{fn.__name__} 重试{max_retries}次后仍失败")
            return default_return

        return wrapper

    if func is not None:
        # @safe_execute 不带参数
        return decorator(func)

    return decorator


# 全局异常处理器实例
_global_handler = ExceptionHandler()


def get_global_handler() -> ExceptionHandler:
    """获取全局异常处理器"""
    return _global_handler