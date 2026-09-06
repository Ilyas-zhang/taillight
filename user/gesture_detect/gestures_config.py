import json
import os

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
CONFIG_PATH = os.path.join(PROJECT_ROOT, "gestures.json")
DEFAULT_ACTIONS = [
    "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
    "ten", "good", "not good", "ok",
    "love", "rock", "pinch", "claw", "gun",
]
DEFAULT_CONFIG = {
    "actions": DEFAULT_ACTIONS,
    "frames_per_action": 500,
    "epochs": 80,
    "model_path": os.path.join("Model", "action.h5"),
}


def data_dir():
    path = os.path.join(PROJECT_ROOT, "Data", "static")
    os.makedirs(path, exist_ok=True)
    return path


def npy_path(name):
    return os.path.join(data_dir(), f"{name}.npy")


def _normalize(cfg):
    merged = dict(DEFAULT_CONFIG)
    if isinstance(cfg, dict):
        merged.update(cfg)
    actions = merged.get("actions") or []
    merged["actions"] = [str(a).strip() for a in actions if str(a).strip()]
    merged["frames_per_action"] = int(merged.get("frames_per_action") or 500)
    merged["epochs"] = int(merged.get("epochs") or 80)
    merged["model_path"] = merged.get("model_path") or DEFAULT_CONFIG["model_path"]
    return merged


def save_config(cfg):
    cfg = _normalize(cfg)
    with open(CONFIG_PATH, "w", encoding="utf-8") as f:
        json.dump(cfg, f, ensure_ascii=False, indent=2)
        f.write("\n")
    return cfg


def load_config():
    if not os.path.isfile(CONFIG_PATH):
        return save_config(DEFAULT_CONFIG)
    with open(CONFIG_PATH, "r", encoding="utf-8") as f:
        cfg = json.load(f)
    return _normalize(cfg)


def model_abs_path(cfg=None):
    if cfg is None:
        cfg = load_config()
    path = cfg["model_path"]
    if os.path.isabs(path):
        return path
    return os.path.join(PROJECT_ROOT, path)
