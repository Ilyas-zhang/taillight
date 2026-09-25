"""
  ============ landmarks.py ============
  手部关键点工具：主手选择、EMA 平滑、手指状态检测、OK 手势检测、
  每指独立去抖。

  Hand landmark utilities: primary-hand selection, EMA smoothing,
  finger state detection, OK gesture detection, per-finger debounce.
"""
import numpy as np
from collections import deque

# ---------------- 关键点常量 / Landmark constants ----------------
LANDMARK_NUM = 21 * 3       # 63 维特征向量（仅作常量参考）
WRIST_INDEX = 0
MIDDLE_MCP_INDEX = 9

# 指尖 / 关节索引 (Thumb / Index / Middle / Ring / Pinky)
THUMB_TIP  = 4;  THUMB_IP  = 3     # 拇指尖 / IP 关节
INDEX_TIP  = 8;  INDEX_PIP = 6     # 食指尖 / PIP 关节
MIDDLE_TIP = 12; MIDDLE_PIP = 10   # 中指尖 / PIP 关节
RING_TIP   = 16; RING_PIP  = 14   # 无名指尖 / PIP 关节
PINKY_TIP  = 20; PINKY_PIP  = 18   # 小指尖 / PIP 关节


# ---------------- 主手选择 / Primary hand selection ----------------

def _hand_span(hand_landmarks):
    """计算手部边界框面积 / Bounding-box area as size proxy."""
    xs = [lm.x for lm in hand_landmarks.landmark]
    ys = [lm.y for lm in hand_landmarks.landmark]
    return (max(xs) - min(xs)) * (max(ys) - min(ys))


def select_primary_hand(results):
    """
    选择主手：优先右手，否则最大手。
    / Select primary hand: prefer right, else largest by span.

    返回 (hand_landmarks, handedness_label)，无手时返回 (None, None)。
    / Returns (hand_landmarks, handedness_label), or (None, None) if no hand.
    """
    if not results or not results.multi_hand_landmarks:
        return None, None

    hands = list(results.multi_hand_landmarks)
    handedness = []
    if results.multi_handedness:
        handedness = [h.classification[0].label for h in results.multi_handedness]

    # 优先右手 / Prefer right hand
    for idx, label in enumerate(handedness):
        if label == "Right" and idx < len(hands):
            return hands[idx], "Right"

    # 否则最大手 / Otherwise largest hand
    best_idx = max(range(len(hands)), key=lambda i: _hand_span(hands[i]))
    best_label = handedness[best_idx] if best_idx < len(handedness) else "Right"
    return hands[best_idx], best_label


# ---------------- 关键点数组 / Landmark arrays ----------------

def landmarks_to_array(hand_landmarks):
    """MediaPipe 归一化关键点 → 21×3 numpy 数组 (x, y, z)。"""
    arr = np.zeros((21, 3), dtype=np.float32)
    for i, lm in enumerate(hand_landmarks.landmark):
        arr[i] = [lm.x, lm.y, lm.z]
    return arr


# ---------------- EMA 关键点平滑 / EMA landmark smoothing ----------------

class LandmarkSmoother:
    """
    指数移动平均 (EMA) 关键点平滑器。
    / Exponential moving average landmark smoother.
    """

    def __init__(self, alpha=0.3):
        self.alpha = alpha
        self.prev = None

    def reset(self):
        self.prev = None

    def update(self, hand_landmarks):
        """
        输入原始关键点，返回平滑后的 21×3 数组。
        / Input raw landmarks, return smoothed 21×3 array.
        """
        curr = landmarks_to_array(hand_landmarks)
        if self.prev is None:
            self.prev = curr
        else:
            self.prev = (1 - self.alpha) * self.prev + self.alpha * curr
        return self.prev


# ---------------- 手指状态检测 / Finger state detection ----------------

def get_finger_states(landmarks_21x3, handedness):
    """
    从归一化关键点检测手指竖起状态。
    / Detect finger up/down states from normalized landmarks.

    landmarks_21x3: 21×3 numpy 数组 (x, y, z)，可以是平滑后的
    handedness: "Right" 或 "Left"
    返回: [thumb, index, middle, ring, pinky]，每个为 0(弯曲) 或 1(竖起)

    拇指使用 x 方向比较（左右手相反），其余四指使用 y 方向比较
    （指尖 y < PIP 关节 y → 伸直，因为图像 y 轴向下）。
    / Thumb uses x-comparison (handedness-dependent);
    other fingers use y-comparison (tip.y < PIP.y means extended).
    """
    fingers = []

    # 大拇指：根据左右手判断 x 方向
    # Thumb: handedness-dependent x-comparison (tip vs IP joint)
    if handedness == "Right":
        fingers.append(1 if landmarks_21x3[THUMB_TIP, 0] <
                       landmarks_21x3[THUMB_IP, 0] else 0)
    else:
        fingers.append(1 if landmarks_21x3[THUMB_TIP, 0] >
                       landmarks_21x3[THUMB_IP, 0] else 0)

    # 其余4根手指：指尖 y < PIP 关节 y → 伸直
    # Other fingers: tip.y < PIP.y means finger is up (y axis points downward)
    for tip_idx, pip_idx in [(INDEX_TIP, INDEX_PIP), (MIDDLE_TIP, MIDDLE_PIP),
                              (RING_TIP, RING_PIP), (PINKY_TIP, PINKY_PIP)]:
        fingers.append(1 if landmarks_21x3[tip_idx, 1] <
                       landmarks_21x3[pip_idx, 1] else 0)

    return fingers


# ---------------- OK 手势几何检测 / OK gesture geometric detection ----------------

def detect_ok_gesture(landmarks_21x3, finger_states, rel_threshold=0.25):
    """
    检测 OK 手势：拇指尖靠近食指尖 + 其余三指伸直。
    / Detect OK gesture: thumb tip close to index tip + other fingers extended.

    landmarks_21x3: 21×3 numpy 数组 (平滑后的)
    finger_states: [thumb, index, middle, ring, pinky] 去抖后的状态
    rel_threshold: 拇指尖-食指尖距离 / 手部大小 的比值阈值

    距离使用 2D (x, y)，z 坐标不够可靠。
    / Distance uses 2D (x, y); z-coordinate is less reliable.
    """
    # 拇指尖与食指尖的 2D 距离 / 2D distance between thumb tip and index tip
    thumb_tip = landmarks_21x3[THUMB_TIP, :2]
    index_tip = landmarks_21x3[INDEX_TIP, :2]
    tip_dist = float(np.linalg.norm(thumb_tip - index_tip))

    # 手部大小参考：腕(0) 到 中指 MCP(9) 的距离
    # Hand size reference: distance from wrist to middle MCP
    wrist = landmarks_21x3[WRIST_INDEX, :2]
    mid_mcp = landmarks_21x3[MIDDLE_MCP_INDEX, :2]
    hand_size = float(np.linalg.norm(mid_mcp - wrist)) + 1e-6

    # 中指 / 无名指 / 小指必须伸直
    # Middle, ring, pinky must all be extended
    other_fingers_extended = (finger_states[2] == 1 and
                              finger_states[3] == 1 and
                              finger_states[4] == 1)

    return (tip_dist / hand_size) < rel_threshold and other_fingers_extended


# ---------------- 每指独立去抖 / Per-finger debounce ----------------

class FingerDebouncer:
    """
    每根手指独立的滑动窗口去抖。
    / Per-finger independent sliding-window debounce.

    优于整值投票：单指误检只翻转一个 bit，不会导致大幅跳变。
    / Better than whole-value voting: a single finger misdetection
    flips only one bit, avoiding large value jumps.
    """

    def __init__(self, window_size=5, threshold=None):
        """
        window_size: 每根手指的滑动窗口大小
        threshold: 确认为"竖起"需要的票数，默认 ceil(window_size/2)
        """
        self.window_size = window_size
        self.threshold = threshold or (window_size + 1) // 2
        self.windows = [deque(maxlen=window_size) for _ in range(5)]

    def update(self, finger_states):
        """
        输入原始手指状态，返回去抖后的状态。
        / Input raw finger states, return debounced states.

        finger_states: [thumb, index, middle, ring, pinky]，每个为 0 或 1
        返回: 同格式去抖后的列表
        """
        debounced = []
        for i, state in enumerate(finger_states):
            self.windows[i].append(state)
            votes_up = sum(self.windows[i])
            debounced.append(1 if votes_up >= self.threshold else 0)
        return debounced

    def reset(self):
        """清空所有窗口 / Clear all windows"""
        for w in self.windows:
            w.clear()
