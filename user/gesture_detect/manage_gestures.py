import os
import sys

import numpy as np

from gestures_config import load_config, save_config, npy_path


def show_status():
    cfg = load_config()
    actions = cfg["actions"]
    print("\n共 {} 类，每类目标帧数: {}，训练轮数: {}".format(
        len(actions), cfg["frames_per_action"], cfg["epochs"]))
    print("模型路径: {}".format(cfg["model_path"]))
    if not actions:
        print("  (名单为空，请先添加手势)")
        return
    print("{:>4}  {:>6}  {:<16} {:<6} {}".format("编号", "串口码", "名称", "状态", "样本数"))
    for i, name in enumerate(actions):
        path = npy_path(name)
        if os.path.isfile(path):
            n = int(np.load(path).shape[0])
            mark = "OK"
        else:
            n = 0
            mark = "缺数据"
        print("{:4d}  {:6d}  {:<16} {:<6} {}".format(i, i + 1, name, mark, n))


def add_action(name):
    name = str(name).strip()
    if not name:
        print("[错误] 手势名不能为空")
        return False
    cfg = load_config()
    if name in cfg["actions"]:
        print("[提示] {} 已存在".format(name))
        return False
    cfg["actions"].append(name)
    save_config(cfg)
    print("[完成] 已添加 {}，编号 {}，串口码 {}".format(name, len(cfg["actions"]) - 1, len(cfg["actions"])))
    return True


def remove_action(name):
    name = str(name).strip()
    cfg = load_config()
    if name not in cfg["actions"]:
        print("[提示] 名单中没有 {}".format(name))
        return False
    cfg["actions"].remove(name)
    save_config(cfg)
    print("[完成] 已从名单删除 {}（npy 文件仍保留）".format(name))
    return True


def missing_actions(cfg=None):
    if cfg is None:
        cfg = load_config()
    return [a for a in cfg["actions"] if not os.path.isfile(npy_path(a))]


def collect(names):
    if not names:
        print("[错误] 没有要采集的手势")
        return
    from gesture import Gesture
    ges = Gesture()
    ges.collectData(target_actions=names)


def train():
    cfg = load_config()
    if not cfg["actions"]:
        print("[错误] 名单为空，请先添加手势")
        return
    missing = missing_actions(cfg)
    if missing:
        print("[错误] 以下手势还没有数据，请先采集: {}".format(", ".join(missing)))
        return
    from gesture import Gesture
    ges = Gesture()
    ges.train()


def evaluate():
    from gesture import Gesture
    ges = Gesture()
    ges.evaluation()


def _print_cli_help():
    print("用法:")
    print("  python manage_gestures.py")
    print("  python manage_gestures.py list")
    print("  python manage_gestures.py add <name>")
    print("  python manage_gestures.py remove <name>")
    print("  python manage_gestures.py collect [names...]")
    print("  python manage_gestures.py collect --missing")
    print("  python manage_gestures.py collect --all")
    print("  python manage_gestures.py train")
    print("  python manage_gestures.py eval")


def run_cli(argv):
    cmd = argv[0]
    if cmd in ("-h", "--help", "help"):
        _print_cli_help()
        return
    if cmd == "list":
        show_status()
        return
    if cmd == "add":
        if len(argv) < 2:
            print("[错误] 请提供手势名，例如: python manage_gestures.py add heart")
            return
        add_action(" ".join(argv[1:]).strip())
        return
    if cmd == "remove":
        if len(argv) < 2:
            print("[错误] 请提供手势名，例如: python manage_gestures.py remove heart")
            return
        remove_action(" ".join(argv[1:]).strip())
        return
    if cmd == "collect":
        cfg = load_config()
        args = argv[1:]
        if not args or args == ["--all"]:
            names = list(cfg["actions"])
        elif args == ["--missing"]:
            names = missing_actions(cfg)
            if not names:
                print("[提示] 数据齐全")
                return
            print("[信息] 将采集缺失手势: {}".format(", ".join(names)))
        else:
            names = args
        collect(names)
        return
    if cmd == "train":
        train()
        return
    if cmd == "eval":
        evaluate()
        return
    print("[错误] 未知命令: {}".format(cmd))
    _print_cli_help()


def run_menu():
    while True:
        print("\n========== 手势管理 ==========")
        print("  1. 查看类别和数据")
        print("  2. 添加手势")
        print("  3. 删除手势")
        print("  4. 采集指定手势")
        print("  5. 采集缺失数据")
        print("  6. 训练模型")
        print("  7. 摄像头评估")
        print("  0. 退出")
        choice = input("> ").strip()
        if choice == "0":
            break
        if choice == "1":
            show_status()
        elif choice == "2":
            add_action(input("新手势名: ").strip())
        elif choice == "3":
            remove_action(input("要删除的名字: ").strip())
        elif choice == "4":
            names = input("名字，空格分隔: ").split()
            collect(names)
        elif choice == "5":
            names = missing_actions()
            if not names:
                print("[提示] 数据齐全")
            else:
                print("[信息] 将采集缺失手势: {}".format(", ".join(names)))
                collect(names)
        elif choice == "6":
            train()
        elif choice == "7":
            evaluate()
        else:
            print("[提示] 无效选项")


if __name__ == "__main__":
    if len(sys.argv) >= 2:
        run_cli(sys.argv[1:])
    else:
        run_menu()
