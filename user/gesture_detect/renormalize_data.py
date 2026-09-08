"""
离线重新归一化 Data/static/*.npy 训练数据，
使其与 landmarks.py 的 landmarks_to_features() 特征管线一致。

用法：python renormalize_data.py          # 转换并覆盖
      python renormalize_data.py --check  # 只检查不修改
"""
import numpy as np
import os
import sys

PROJECT_ROOT = os.path.dirname(os.path.abspath(__file__))
DATA_DIR = os.path.join(PROJECT_ROOT, "Data", "static")
BACKUP_DIR = os.path.join(PROJECT_ROOT, "Data", "static_raw_backup")
WRIST_IDX = 0
MID_MCP_IDX = 9


def normalize_sample(row):
    """将一行 63-d 原始 MediaPipe 坐标转为腕部归一化特征，与 landmarks_to_features 一致。"""
    pts = row.reshape(21, 3).astype(np.float32)
    # 1. 减去腕点
    pts = pts - pts[WRIST_IDX]
    # 2. 除以腕→中指MCP距离
    scale = float(np.linalg.norm(pts[MID_MCP_IDX]) + 1e-6)
    pts = pts / scale
    return pts.flatten()


def check_data():
    """检查数据是原始坐标还是已归一化。"""
    files = sorted(f for f in os.listdir(DATA_DIR) if f.endswith('.npy'))
    raw_count = 0
    norm_count = 0
    for fname in files:
        d = np.load(os.path.join(DATA_DIR, fname))
        wrist_x_mean = d[:, 0].mean()
        if abs(wrist_x_mean) < 0.05:
            norm_count += 1
        else:
            raw_count += 1
    print(f"检查结果: {raw_count} 个文件看起来是原始坐标, {norm_count} 个看起来已归一化")
    return raw_count, norm_count


def renormalize():
    """备份原始数据，然后转换所有 .npy 文件。"""
    files = sorted(f for f in os.listdir(DATA_DIR) if f.endswith('.npy'))
    if not files:
        print("Data/static/ 中没有 .npy 文件")
        return

    # 备份原始数据
    os.makedirs(BACKUP_DIR, exist_ok=True)
    print(f"备份原始数据到 {BACKUP_DIR}")
    import shutil
    for fname in files:
        src = os.path.join(DATA_DIR, fname)
        dst = os.path.join(BACKUP_DIR, fname)
        if not os.path.isfile(dst):
            shutil.copy2(src, dst)

    for fname in files:
        path = os.path.join(DATA_DIR, fname)
        d = np.load(path)
        n_samples = d.shape[0]

        # 先检查是否已经归一化过（腕点接近 0）
        wrist_x_mean = d[:, 0].mean()
        if abs(wrist_x_mean) < 0.05:
            print(f"  {fname:20s}  跳过 (已归一化, wrist_x_mean={wrist_x_mean:.4f})")
            continue

        # 逐样本归一化
        new_data = np.zeros_like(d, dtype=np.float32)
        for i in range(n_samples):
            new_data[i] = normalize_sample(d[i])

        # 覆盖保存
        np.save(path, new_data)
        wrist_x_new = new_data[:, 0].mean()
        print(f"  {fname:20s}  转换完成  {n_samples} 样本  wrist_x_mean: {wrist_x_mean:.3f} → {wrist_x_new:.6f}")


if __name__ == '__main__':
    if '--check' in sys.argv:
        check_data()
    else:
        print("=== 检查数据状态 ===")
        raw, norm = check_data()
        if raw == 0:
            print("所有数据已归一化，无需转换。")
        else:
            print(f"\n=== 开始转换 {raw} 个文件 ===")
            renormalize()
            print("\n=== 转换后验证 ===")
            check_data()
            print("\n完成！现在可以运行 python manage_gestures.py train 重新训练模型。")
