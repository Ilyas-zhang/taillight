"""
  ============ config.py ============
  手势检测参数配置（二进制计数模式）。
  / Gesture detection parameter configuration (binary counting mode).
"""
import json
import os

_CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")

_DEFAULTS = {
    "ok_dist_threshold": 0.25,       # OK 手势拇指-食指距离/手部大小 阈值
    "finger_debounce_window": 5,     # 每指去抖滑动窗口大小
    "finger_debounce_threshold": 3,  # 每指确认"竖起"所需票数
    "value_stable_need": 3,          # 连续相同帧数确认阈值
    "smoother_alpha": 0.3,           # EMA 平滑系数
}


def load_config():
    """
    加载配置，缺失字段使用默认值。
    / Load config; missing fields fall back to defaults.
    """
    if not os.path.isfile(_CONFIG_PATH):
        return dict(_DEFAULTS)
    with open(_CONFIG_PATH, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    merged = dict(_DEFAULTS)
    merged.update(cfg)
    return merged
