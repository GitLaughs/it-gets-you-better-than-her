"""配置加载器"""

import os
import yaml
import logging
from typing import Dict, Optional

logger = logging.getLogger(__name__)

_config: Dict = {}


def load_config(config_path: str = "src/config/config.yaml") -> Dict:
    """加载配置文件"""
    global _config

    if not os.path.exists(config_path):
        logger.warning(f"配置文件不存在: {config_path}, 使用默认配置")
        _config = {}
        return _config

    with open(config_path, 'r', encoding='utf-8') as f:
        _config = yaml.safe_load(f) or {}

    logger.info(f"配置已加载: {config_path}")
    return _config


def get_config(section: Optional[str] = None) -> Dict:
    """获取配置"""
    if not _config:
        load_config()

    if section:
        return _config.get(section, {})
    return _config