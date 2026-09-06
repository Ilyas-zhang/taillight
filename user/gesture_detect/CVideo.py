import sys
import traceback

# 把所有的错误同时写到 error.log 文件里，防止被吞掉
def excepthook(exc_type, exc_value, exc_tb):
    with open('error.log', 'w', encoding='utf-8') as f:
        traceback.print_exception(exc_type, exc_value, exc_tb, file=f)
    traceback.print_exception(exc_type, exc_value, exc_tb)
sys.excepthook = excepthook

# -*- coding:utf-8 -*-
import sys
import traceback
from collections import deque
import cv2
import numpy as np
import os
import PySimpleGUI as sg
import datetime
import tensorflow as tf
import serial
import time

from hand import Detector
from gestures_config import load_config
from landmarks import LandmarkSmoother, select_primary_hand

#串口发送 使用时不要注释这段
ser = serial.Serial(
    port='COM8',
    baudrate=115200,
    timeout=1,
    write_timeout=1,      # 写入超时设置为 1 秒，防止卡死
    rtscts=False,         # 关闭硬件流控
    dsrdtr=False          # 关闭 DSR/DTR 流控
)
ser.setRTS(False)
ser.setDTR(False)

'''
#摄像头调试预留，使用时把这段注释掉
ser = None
'''
def send_result_code(code):
    try:
        ser.write(bytes([code]))
        ser.flush()  # 强制刷新缓冲区发送
        print(f"Sent result code: {code}")
    except Exception as e:
        print(f"[串口发送失败] 错误信息: {e}")

# 全局异常捕获
def _excepthook(exc_type, exc_value, exc_tb):
    with open('error.log', 'w', encoding='utf-8') as f:
        traceback.print_exception(exc_type, exc_value, exc_tb, file=f)
    traceback.print_exception(exc_type, exc_value, exc_tb)
sys.excepthook = _excepthook


class SGCV:
    def __init__(self) -> None:
        print("[DEBUG] SGCV.__init__ 开始")

        self.video_path = os.path.join(os.getcwd(), 'videos')
        print(f"[DEBUG] video_path = {self.video_path}")
        if not os.path.isdir(self.video_path):
            os.mkdir(self.video_path)

        self.cameras = self._get_cam_num()
        self.camera_index = 0
        self.mirror = True
        self.detec_hand = False
        self.is_record = False
        self.video_out = None

        # ---- 右侧画面 ----
        self.right_layout = [[sg.Image(filename='', key='image')]]

        # ---- 左侧面板 ----
        webcam_layout = [
            [sg.Text('镜像显示：'),
             sg.Radio('是', 'mirror', enable_events=True, default=True, key='is_mirror'),
             sg.Radio('否', 'mirror', enable_events=True, default=False, key='not_mirror')]
        ]
        for cam in self.cameras:
            webcam_layout.append([
                sg.Radio(f'camera {cam}', 'radio', enable_events=True,
                         default=(cam == 0), key=f'camera_{cam}')
            ])

        record_layout = [
            [sg.Button('开始录像', key='recording'), sg.Button('保存路径', key='btn_show')],
            [sg.Listbox(values=self._get_video_names(), size=(37, 6), key='list_video', expand_x=True)]
        ]
        oper_layout = [
            [sg.Text('开启检测：'),
             sg.Checkbox('手势', default=False, enable_events=True, key='check_hand'),
             sg.Button('退出', key='exit')]
        ]

        left_column = [sg.Frame('操作', oper_layout)],[sg.Frame('选择摄像头', webcam_layout)],[sg.Frame('录像', record_layout)]

        layout = [
            [sg.Column(left_column, size=(300, 600), scrollable=True),
             sg.VSeparator(),
             sg.Column(self.right_layout)]
        ]

        print("[DEBUG] 准备创建 Window")
        self.window = sg.Window('手势动作检测', layout, size=(1100, 650), resizable=True)
        print("[DEBUG] Window 创建成功")

        # ---- 摄像头 ----
        print("[DEBUG] 初始化摄像头")
        self.cap = cv2.VideoCapture(self.cameras[0], cv2.CAP_DSHOW)
        print(f"[DEBUG] 摄像头打开状态: {self.cap.isOpened()}")

        # ---- 检测器 ----
        print("[DEBUG] 初始化 Detector")
        self.detector = Detector()
        print("[DEBUG] Detector 初始化完成")

        # ---- 动作标签（与 gestures.json 保持一致） ----
        cfg = load_config()
        self.actions = np.array(cfg["actions"])

        # ---- 手势稳定性判断（置信度 + 滑动投票） ----
        self.smoother = LandmarkSmoother(alpha=0.3)
        self.vote_window = deque(maxlen=15)
        self.vote_need = 10
        self.conf_threshold = 0.70
        self.last_confirmed_id = -1
        self.current_stable_id = -1

        # ---- 模型 ----
        model_file = self._resource_path(cfg["model_path"])
        print(f"[DEBUG] 模型路径: {model_file}")
        if os.path.isfile(model_file):
            self.model = tf.keras.models.load_model(model_file)
            print("[DEBUG] 模型加载成功")
        else:
            print("[DEBUG] 模型文件不存在，跳过加载")
            self.model = None

        print("[DEBUG] __init__ 完成，准备进入 run()")

    def _get_cam_num(self):
        cams = []
        for i in range(3):
            cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
            if not cap.isOpened():
                cap.release()
                break
            ret, _ = cap.read()
            cap.release()
            if ret:
                cams.append(i)
            else:
                break
        return cams if cams else [0]

    def _get_video_names(self):
        try:
            return [f for f in os.listdir(self.video_path) if f.lower().endswith('.avi')]
        except:
            return []

    def _resource_path(self, relative_path):
        base = getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))
        return os.path.join(base, relative_path)

    def _reset_vote_state(self, clear_confirmed=False):
        self.vote_window.clear()
        self.current_stable_id = -1
        self.smoother.reset()
        if clear_confirmed:
            self.last_confirmed_id = -1

    def _predict_action(self, frame):
        """ 平滑 + 腕部归一化 + 置信度门槛 + 滑动投票 """
        if not self.detec_hand or self.model is None:
            self._reset_vote_state(clear_confirmed=True)
            return frame

        hand = select_primary_hand(self.detector.results)
        if hand is None:
            self._reset_vote_state(clear_confirmed=True)
            return frame

        x = self.smoother.to_features(hand).reshape(1, -1)
        y = self.model.predict(x, verbose=0)
        act_id = int(np.argmax(y[0]))
        conf = float(np.max(y[0]))
        num_classes = int(y.shape[-1])

        if num_classes != len(self.actions) or not (0 <= act_id < len(self.actions)):
            cv2.putText(frame, 'Model/label mismatch, retrain', (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
            return frame

        if conf < self.conf_threshold:
            cv2.putText(frame, f'Unsure  {conf:.2f}', (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 165, 255), 2)
            cv2.putText(frame, f'Votes: {len(self.vote_window)}/{self.vote_window.maxlen}', (10, 70),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
            return frame

        self.vote_window.append(act_id)
        votes = list(self.vote_window)
        vote_counts = np.bincount(votes, minlength=len(self.actions))
        top_id = int(np.argmax(vote_counts))
        top_votes = int(vote_counts[top_id])
        self.current_stable_id = top_id

        if top_votes >= self.vote_need and top_id != self.last_confirmed_id:
            gesture_code = top_id + 1
            gesture_name = self.actions[top_id]
            print(f"[确认手势] 代码: {gesture_code}, 名称: {gesture_name}")
            send_result_code(gesture_code)
            self.last_confirmed_id = top_id

        gesture_name = self.actions[act_id]
        progress = min(top_votes / self.vote_need * 100, 100)
        cv2.putText(frame, f'Action: {gesture_name}  {conf:.2f}', (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)
        cv2.putText(frame, f'Vote: {self.actions[top_id]} {int(progress)}% ({top_votes}/{self.vote_need})', (10, 70),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)

        return frame

    def run(self):
        print("[DEBUG] run() 开始")
        while True:
            event, value = self.window.read(timeout=40)
            if event == sg.WIN_CLOSED or event == 'exit':
                print("[DEBUG] 退出事件")
                break

            if event == 'check_hand':
                self.detec_hand = value['check_hand']

            if event == 'is_mirror':
                self.mirror = True
            elif event == 'not_mirror':
                self.mirror = False

            for i in self.cameras:
                if event == f'camera_{i}' and i != self.camera_index:
                    self.cap.release()
                    self.cap = cv2.VideoCapture(i, cv2.CAP_DSHOW)
                    self.camera_index = i

            if event == 'recording':
                if self.is_record:
                    self.video_out.release()
                    self.video_out = None
                    self.is_record = False
                    self.window['recording'].update('开始录像')
                    self.window['list_video'].update(self._get_video_names())
                else:
                    fname = datetime.datetime.now().strftime("%Y%m%d%H%M%S")
                    self.video_out = cv2.VideoWriter(
                        os.path.join(self.video_path, f'{fname}.avi'),
                        cv2.VideoWriter_fourcc(*'XVID'), 20.0, (640, 480))
                    self.is_record = True
                    self.window['recording'].update('停止录像')

            if event == 'btn_show':
                sg.popup(self.video_path, title='保存路径')

            # ---- 画面处理 ----
            if self.cap and self.cap.isOpened():
                ret, frame = self.cap.read()
                if ret:
                    if self.mirror:
                        frame = cv2.flip(frame, 1)
                    if self.detec_hand:
                        frame = self.detector.runDetec(frame)
                        frame = self._predict_action(frame)
                    if self.is_record:
                        self.video_out.write(frame)
                        cv2.putText(frame, 'Recording', (10, 110),
                                    cv2.FONT_HERSHEY_COMPLEX, 1, (255, 0, 0), 3)
                    _, buf = cv2.imencode('.png', frame)
                    self.window['image'].update(data=buf.tobytes())

        print("[DEBUG] 清理资源")
        if self.cap:
            self.cap.release()
        if self.video_out:
            self.video_out.release()
        self.window.close()
        print("[DEBUG] 程序结束")


if __name__ == '__main__':
    print("[DEBUG] 程序启动")
    app = SGCV()
    app.run()