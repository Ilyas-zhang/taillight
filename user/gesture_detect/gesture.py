import os
import cv2
import numpy as np
from sklearn.model_selection import train_test_split
from tensorflow.keras.models import Sequential
from tensorflow.keras.layers import Flatten, Dense, Dropout

from hand import Detector as HandDetector
from gestures_config import load_config, npy_path, data_dir, model_abs_path
from landmarks import LANDMARK_NUM, landmarks_to_features, select_primary_hand


class Gesture():
    def __init__(self) -> None:
        cfg = load_config()
        self.action_type = 'static'
        self.data_path = data_dir()
        os.makedirs(self.data_path, exist_ok=True)

        self.actions = np.array(cfg["actions"])
        self.label_map = {name: i for i, name in enumerate(self.actions)}
        self.action_num = cfg["frames_per_action"]
        self.epochs = cfg["epochs"]
        self.test_size = 0.2
        self.model_path = model_abs_path(cfg)
        self.landmark_num = LANDMARK_NUM
        self.pre_action = -1
        self.action_index = 0

        self.detector = None
        self.cap = None

    def _ensure_detector(self):
        if self.detector is None:
            print("[信息] 正在初始化 HandDetector ...")
            self.detector = HandDetector()

    def open_camera(self):
        self._ensure_detector()
        if self.cap is not None and self.cap.isOpened():
            return True
        print("[信息] 正在打开摄像头...")
        self.cap = cv2.VideoCapture(0)
        if not self.cap.isOpened():
            print("[错误] 无法打开摄像头，请检查设备连接或占用状态！")
            return False
        return True

    def close_camera(self):
        if self.cap is not None:
            self.cap.release()
            self.cap = None
        cv2.destroyAllWindows()

    def collectData(self, target_actions=None):
        """ 采集手势关键点。未指定则采集配置中的全部类别。 """
        if target_actions is None:
            target_actions = list(self.actions)
        else:
            target_actions = [str(a).strip() for a in target_actions if str(a).strip()]

        if not target_actions:
            print("[错误] 没有要采集的手势")
            return

        unknown = [a for a in target_actions if a not in self.label_map]
        if unknown:
            print("[错误] 以下手势不在 gestures.json 中，请先添加: {}".format(", ".join(unknown)))
            return

        if not self.open_camera():
            return

        try:
            for action in target_actions:
                lm_data = np.zeros(self.landmark_num).reshape((1, -1))

                print(f"\n[提示] 准备录制 [{action}] 手势，画面倒计时 5 秒...")
                for countdown in range(5, 0, -1):
                    ret, frame = self.cap.read()
                    if not ret:
                        continue
                    frame = cv2.flip(frame, 1)
                    cv2.putText(frame, f'Ready for [{action}] in {countdown}s', (50, 200),
                                cv2.FONT_HERSHEY_SIMPLEX, 1.2, (0, 255, 0), 3)
                    cv2.imshow('OpenCV Feed', frame)
                    cv2.waitKey(1000)

                print(f"[录制中] 正在采集 [{action}] (目标帧数: {self.action_num})...")
                for frame_num in range(self.action_num):
                    flag, frame = self.cap.read()
                    if not flag or frame is None:
                        continue

                    frame = cv2.flip(frame, 1)
                    frame = self.detector.detecHands(frame)

                    cv2.putText(frame, f'Action: {action} No.: {frame_num}/{self.action_num}', (50, 40),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2, cv2.LINE_AA)
                    cv2.imshow('OpenCV Feed', frame)

                    hand = select_primary_hand(self.detector.results)
                    if hand is not None:
                        tmp = landmarks_to_features(hand)
                        lm_data = np.vstack([lm_data, tmp])

                    if cv2.waitKey(30) & 0xFF == 27:
                        print("手动中断当前手势录制！")
                        break

                samples = lm_data[1:, :]
                save_path = npy_path(action)
                print(f'[完成] [{action}] 录制结束，有效样本: {samples.shape[0]}，保存路径: {save_path}')
                np.save(save_path, samples)
        finally:
            self.close_camera()
            print("\n[全部数据采集完毕！]")

    def missing_actions(self, actions=None):
        if actions is None:
            actions = list(self.actions)
        return [a for a in actions if not os.path.isfile(npy_path(a))]

    def loadDate(self):
        if self.actions.size == 0:
            raise ValueError("gestures.json 中没有任何手势，请先添加类别再采集数据。")

        missing = self.missing_actions()
        if missing:
            raise FileNotFoundError(
                "以下手势还没有采集数据，请先运行 manage_gestures.py collect: {}".format(", ".join(missing))
            )

        X = np.zeros((1, self.landmark_num))
        y = np.zeros((1,))
        for action in self.actions:
            res = np.load(npy_path(action))
            if res.size == 0:
                raise ValueError("手势 [{}] 的数据为空，请重新采集。".format(action))
            lab = np.zeros((res.shape[0],))
            lab.fill(self.label_map[action])
            X = np.vstack([X, res])
            y = np.hstack([y, lab])
        X, y = X[1:, :], y[1:]
        print(X.shape)
        print(y.shape)
        X_train, X_test, y_train, y_test = train_test_split(X, y, test_size=self.test_size)
        rng = np.random.default_rng(42)
        X_train = X_train + rng.normal(0.0, 0.01, size=X_train.shape).astype(X_train.dtype)
        return X_train, X_test, y_train, y_test

    def getModel(self):
        model = Sequential()
        model.add(Flatten(input_shape=(self.landmark_num,)))
        model.add(Dense(128, activation='relu'))
        model.add(Dropout(0.3))
        model.add(Dense(256, activation='relu'))
        model.add(Dropout(0.3))
        model.add(Dense(128, activation='relu'))
        model.add(Dropout(0.3))
        model.add(Dense(32, activation='relu'))
        model.add(Dense(self.actions.shape[0], activation='softmax'))
        model.compile(optimizer='Adam', loss='sparse_categorical_crossentropy', metrics=['accuracy'])
        return model

    def train(self):
        """ 训练过程 """
        X_train, X_test, y_train, y_test = self.loadDate()

        os.makedirs(os.path.dirname(self.model_path), exist_ok=True)
        model = self.getModel()
        model.fit(X_train, y_train, epochs=self.epochs)

        test_loss, test_acc = model.evaluate(X_test, y_test, verbose=2)
        print('\nTest accuracy:{}, loss: {}'.format(test_acc, test_loss))

        model.save(self.model_path)
        print("[完成] 模型已保存: {}".format(self.model_path))

    def evaluation(self):
        import tensorflow as tf

        if not os.path.isfile(self.model_path):
            print("[错误] 找不到模型: {}，请先训练。".format(self.model_path))
            return
        if not self.open_camera():
            return

        model = tf.keras.models.load_model(self.model_path)
        try:
            while self.cap.isOpened():
                flag, frame = self.cap.read()
                frame = cv2.flip(frame, 1)
                if not flag:
                    print("Ignoring empty camera frame.")
                    continue
                frame = self.detector.detecHands(frame)

                hand = select_primary_hand(self.detector.results)
                if hand is not None:
                    x_train = landmarks_to_features(hand).reshape(1, -1)
                    y_pre = model.predict(x_train, verbose=0)
                    act_id = int(np.argmax(y_pre[0]))
                    if 0 <= act_id < len(self.actions):
                        act_name = self.actions[act_id]
                    else:
                        act_name = "?"
                    if act_id == self.pre_action:
                        self.action_index += 1
                    else:
                        self.pre_action = act_id
                        self.action_index = 0
                    cv2.putText(frame, 'Action: {} id: {}, count: {}'.format(act_name, act_id, self.action_index), (10, 30),
                                cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 255, 0), 2, cv2.LINE_AA)

                cv2.imshow("Action", frame)
                if cv2.waitKey(50) & 0xFF == 27:
                    break
        finally:
            self.close_camera()


if __name__ == '__main__':
    print("请使用 manage_gestures.py 管理手势类别、采集数据和训练，例如:")
    print("  python manage_gestures.py")
    print("  python manage_gestures.py add heart")
    print("  python manage_gestures.py collect heart")
    print("  python manage_gestures.py train")
