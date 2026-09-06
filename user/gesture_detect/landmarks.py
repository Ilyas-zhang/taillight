import numpy as np

LANDMARK_NUM = 21 * 3
WRIST_INDEX = 0
MIDDLE_MCP_INDEX = 9


def _hand_span(hand_landmarks):
    xs = [lm.x for lm in hand_landmarks.landmark]
    ys = [lm.y for lm in hand_landmarks.landmark]
    return (max(xs) - min(xs)) * (max(ys) - min(ys))


def select_primary_hand(results):
    """Prefer the right hand; otherwise pick the largest visible hand."""
    if not results or not results.multi_hand_landmarks:
        return None

    hands = list(results.multi_hand_landmarks)
    handedness = []
    if results.multi_handedness:
        handedness = [h.classification[0].label for h in results.multi_handedness]

    for idx, label in enumerate(handedness):
        if label == "Right" and idx < len(hands):
            return hands[idx]

    return max(hands, key=_hand_span)


def landmarks_to_array(hand_landmarks):
    return np.array(
        [[lm.x, lm.y, lm.z] for lm in hand_landmarks.landmark],
        dtype=np.float32,
    )


def landmarks_to_features(hand_landmarks):
    """Wrist-centered, scale-normalized 63-d vector."""
    pts = landmarks_to_array(hand_landmarks)
    pts = pts - pts[WRIST_INDEX]
    scale = float(np.linalg.norm(pts[MIDDLE_MCP_INDEX]) + 1e-6)
    pts = pts / scale
    return pts.flatten()


class LandmarkSmoother:
    def __init__(self, alpha=0.3):
        self.alpha = alpha
        self.prev = None

    def reset(self):
        self.prev = None

    def update(self, hand_landmarks):
        curr = landmarks_to_array(hand_landmarks)
        if self.prev is None:
            self.prev = curr
        else:
            self.prev = (1.0 - self.alpha) * self.prev + self.alpha * curr
        return self.prev

    def to_features(self, hand_landmarks):
        pts = self.update(hand_landmarks)
        pts = pts - pts[WRIST_INDEX]
        scale = float(np.linalg.norm(pts[MIDDLE_MCP_INDEX]) + 1e-6)
        pts = pts / scale
        return pts.flatten()
