# src/utils/logger.py
"""
A1_Builder 统一日志系统
功能：
  - 控制台彩色输出 + 文件持久化
  - 格式: [时间] [级别] [模块] 消息
  - 支持 DEBUG / INFO / WARN / ERROR / CRITICAL
  - 日志文件按日期自动轮转
  - 线程安全
"""

import os
import sys
import logging
import logging.handlers
from datetime import datetime
from typing import Optional

# ── 尝试导入 colorlog（可选依赖，缺失时退化为普通输出）──────────────────
try:
    import colorlog
    HAS_COLORLOG = True
except ImportError:
    HAS_COLORLOG = False

# ═══════════════════════════════════════════════════════════════════════════
# 常量
# ═══════════════════════════════════════════════════════════════════════════
DEFAULT_LOG_DIR = "logs"
DEFAULT_LOG_LEVEL = "INFO"
DEFAULT_LOG_NAME = "a1_vision"

# 文件日志格式
FILE_FORMAT = "[%(asctime)s] [%(levelname)-8s] [%(name)s/%(module)s] %(message)s"
FILE_DATE_FMT = "%Y-%m-%d %H:%M:%S"

# 控制台日志格式（带颜色）
COLOR_FORMAT = (
    "%(log_color)s[%(asctime)s] [%(levelname)-8s]%(reset)s "
    "%(cyan)s[%(name)s/%(module)s]%(reset)s %(message)s"
)
# 控制台日志格式（无颜色降级）
PLAIN_FORMAT = "[%(asctime)s] [%(levelname)-8s] [%(name)s/%(module)s] %(message)s"
CONSOLE_DATE_FMT = "%H:%M:%S"

# 颜色映射
LOG_COLORS = {
    "DEBUG":    "white",
    "INFO":     "green",
    "WARNING":  "yellow",
    "ERROR":    "red",
    "CRITICAL": "bold_red",
}

# 单个日志文件最大字节数 (10 MB)
MAX_BYTES = 10 * 1024 * 1024
# 保留的历史日志文件个数
BACKUP_COUNT = 5

# ── 全局注册表（防止重复添加 handler）─────────────────────────────────────
_configured_loggers: dict = {}


# ═══════════════════════════════════════════════════════════════════════════
# 核心 API
# ═══════════════════════════════════════════════════════════════════════════

def setup_logger(
    name: str = DEFAULT_LOG_NAME,
    level: str = DEFAULT_LOG_LEVEL,
    log_dir: str = DEFAULT_LOG_DIR,
    console: bool = True,
    file: bool = True,
    log_filename: Optional[str] = None,
) -> logging.Logger:
    """
    创建 / 获取一个已配置好的 Logger。

    Parameters
    ----------
    name : str
        Logger 名称（同名多次调用只配置一次）。
    level : str
        日志级别，支持 DEBUG / INFO / WARNING / ERROR / CRITICAL。
    log_dir : str
        日志文件输出目录，自动创建。
    console : bool
        是否添加控制台 handler。
    file : bool
        是否添加文件 handler。
    log_filename : str | None
        自定义日志文件名，默认为 ``{name}_{date}.log``。

    Returns
    -------
    logging.Logger
    """
    # 已经配置过 → 直接返回（避免重复 handler）
    if name in _configured_loggers:
        logger = _configured_loggers[name]
        # 允许动态调整级别
        logger.setLevel(_parse_level(level))
        return logger

    logger = logging.getLogger(name)
    logger.setLevel(_parse_level(level))
    logger.propagate = False  # 不向 root logger 冒泡

    # ── 控制台 handler ───────────────────────────────────────────────────
    if console:
        console_handler = _build_console_handler(level)
        logger.addHandler(console_handler)

    # ── 文件 handler ─────────────────────────────────────────────────────
    if file:
        file_handler = _build_file_handler(name, level, log_dir, log_filename)
        logger.addHandler(file_handler)

    _configured_loggers[name] = logger
    return logger


def get_logger(name: str = DEFAULT_LOG_NAME) -> logging.Logger:
    """
    获取已存在的 Logger；若尚未配置则用默认参数创建。
    """
    if name in _configured_loggers:
        return _configured_loggers[name]
    return setup_logger(name=name)


# ═══════════════════════════════════════════════════════════════════════════
# 内部工具
# ═══════════════════════════════════════════════════════════════════════════

def _parse_level(level: str) -> int:
    """字符串 → logging 级别常量"""
    mapping = {
        "DEBUG":    logging.DEBUG,
        "INFO":     logging.INFO,
        "WARN":     logging.WARNING,
        "WARNING":  logging.WARNING,
        "ERROR":    logging.ERROR,
        "CRITICAL": logging.CRITICAL,
    }
    return mapping.get(level.upper(), logging.INFO)


def _build_console_handler(level: str) -> logging.Handler:
    """构建控制台 handler（优先使用 colorlog）"""
    handler = logging.StreamHandler(sys.stdout)
    handler.setLevel(_parse_level(level))

    if HAS_COLORLOG:
        formatter = colorlog.ColoredFormatter(
            fmt=COLOR_FORMAT,
            datefmt=CONSOLE_DATE_FMT,
            log_colors=LOG_COLORS,
            secondary_log_colors={},
            style="%",
        )
    else:
        formatter = logging.Formatter(
            fmt=PLAIN_FORMAT,
            datefmt=CONSOLE_DATE_FMT,
        )

    handler.setFormatter(formatter)
    return handler


def _build_file_handler(
    name: str,
    level: str,
    log_dir: str,
    log_filename: Optional[str] = None,
) -> logging.Handler:
    """构建文件 handler（RotatingFileHandler，自动轮转）"""
    os.makedirs(log_dir, exist_ok=True)

    if log_filename is None:
        date_str = datetime.now().strftime("%Y%m%d")
        log_filename = f"{name}_{date_str}.log"

    filepath = os.path.join(log_dir, log_filename)

    handler = logging.handlers.RotatingFileHandler(
        filename=filepath,
        maxBytes=MAX_BYTES,
        backupCount=BACKUP_COUNT,
        encoding="utf-8",
    )
    handler.setLevel(_parse_level(level))

    formatter = logging.Formatter(
        fmt=FILE_FORMAT,
        datefmt=FILE_DATE_FMT,
    )
    handler.setFormatter(formatter)
    return handler


# ═══════════════════════════════════════════════════════════════════════════
# 便捷模块级函数（可选，快速使用）
# ═══════════════════════════════════════════════════════════════════════════

def debug(msg: str, *args, **kwargs):
    get_logger().debug(msg, *args, **kwargs)

def info(msg: str, *args, **kwargs):
    get_logger().info(msg, *args, **kwargs)

def warning(msg: str, *args, **kwargs):
    get_logger().warning(msg, *args, **kwargs)

def error(msg: str, *args, **kwargs):
    get_logger().error(msg, *args, **kwargs)

def critical(msg: str, *args, **kwargs):
    get_logger().critical(msg, *args, **kwargs)


# ═══════════════════════════════════════════════════════════════════════════
# 自测
# ═══════════════════════════════════════════════════════════════════════════
if __name__ == "__main__":
    logger = setup_logger(name="test_logger", level="DEBUG", log_dir="logs")

    logger.debug("这是一条 DEBUG 消息")
    logger.info("这是一条 INFO 消息")
    logger.warning("这是一条 WARNING 消息")
    logger.error("这是一条 ERROR 消息")
    logger.critical("这是一条 CRITICAL 消息")

    # 测试同名二次调用不会重复添加 handler
    logger2 = setup_logger(name="test_logger", level="INFO")
    assert logger is logger2
    assert len(logger2.handlers) == 2  # console + file

    # 测试 get_logger
    logger3 = get_logger("test_logger")
    assert logger3 is logger

    # 测试模块级便捷函数
    info("模块级便捷函数测试 OK")

    print("\n✅ logger.py 自测全部通过")