import sys
import traceback

# 把所有的错误同时写到 error.log 文件里，防止被吞掉
def excepthook(exc_type, exc_value, exc_tb):
    with open('error.log', 'w', encoding='utf-8') as f:
        traceback.print_exception(exc_type, exc_value, exc_tb, file=f)
    traceback.print_exception(exc_type, exc_value, exc_tb)
sys.excepthook = excepthook

# -*- coding:utf-8 -*-
from collections import deque
import cv2
import numpy as np
import os
import PySimpleGUI as sg
import datetime
import tensorflow as tf
import serial
import serial.tools.list_ports
import time

from hand import Detector
from gestures_config import load_config
from landmarks import LandmarkSmoother, select_primary_hand


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
             sg.Button('退出', key='exit')],
            [sg.Text('串口：'),
             sg.Combo([p.device for p in serial.tools.list_ports.comports()],
                      size=10, key='com_port'),
             sg.Button('连接', key='btn_uart_connect'),
             sg.Text('', key='uart_status', size=(16, 1))]
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

        # ---- UART 通信状态 ----
        self.serial_port = None
        self.uart_connected = False
        self.uart_last_sent_char = None
        self.uart_ack_received = True
        self.uart_send_time = 0
        self.uart_ack_timeout = 0.2       # 200 ms ACK 超时

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

    # ==================== UART 通信 ====================

    @staticmethod
    def act_id_to_uart_char(act_id):
        """手势类别 → UART ASCII 字符映射

        类别 0-8 (one..nine) → '1'-'9'
        类别 9 (ten/fist)   → '0'
        类别 12 (ok)        → 'K'
        类别 10,11 (good/not good) → None (MCU 无对应动作)
        """
        if act_id is None:
            return None
        if 0 <= act_id <= 8:
            return chr(ord('1') + act_id)   # 0->'1', 1->'2', ..., 8->'9'
        elif act_id == 9:
            return '0'                       # ten/fist -> digit 0
        elif act_id == 12:
            return 'K'                       # ok gesture
        return None                          # good, not good, invalid -> no action

    def uart_connect(self, port_name):
        """打开串口连接"""
        try:
            self.serial_port = serial.Serial(
                port=port_name,
                baudrate=115200,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0,           # 非阻塞读
                write_timeout=0
            )
            self.uart_connected = True
            self.uart_last_sent_char = None
            self.uart_ack_received = True
        except serial.SerialException as e:
            sg.popup('串口连接失败: {}'.format(e), title='UART 错误')

    def uart_disconnect(self):
        """关闭串口连接"""
        if self.serial_port is not None and self.serial_port.is_open:
            try:
                self.serial_port.close()
            except:
                pass
        self.serial_port = None
        self.uart_connected = False
        self.uart_last_sent_char = None
        self.uart_ack_received = True

    def uart_process(self, current_char):
        """每帧调用：发送手势字符，检查 ACK。

        协议：
          - current_char 为 None 时（无映射手势），不发送，MCU 保持上次显示
          - 新手势或首次发送：立即发送字符
          - 等待 ACK 中：检查串口是否收到 '#'
          - 超时（>200 ms）未收到 ACK：重发
        """
        if not self.uart_connected:
            return

        # 无映射手势，不发送
        if current_char is None:
            self.uart_last_sent_char = None
            self.uart_ack_received = True
            return

        # 新手势或 ACK 已收到 -> 发送
        if current_char != self.uart_last_sent_char or self.uart_ack_received:
            try:
                self.serial_port.write(current_char.encode('ascii'))
            except serial.SerialException:
                pass
            self.uart_last_sent_char = current_char
            self.uart_ack_received = False
            self.uart_send_time = time.time()
            return

        # 等待 ACK
        if not self.uart_ack_received:
            try:
                if self.serial_port.in_waiting > 0:
                    rx_data = self.serial_port.read(self.serial_port.in_waiting)
                    if b'#' in rx_data:
                        self.uart_ack_received = True
                        return
            except serial.SerialException:
                pass
            # 超时重发
            if (time.time() - self.uart_send_time) > self.uart_ack_timeout:
                try:
                    self.serial_port.write(self.uart_last_sent_char.encode('ascii'))
                except serial.SerialException:
                    pass
                self.uart_send_time = time.time()

    # ==================== 预测 ====================

    def _predict_action(self, frame):
        """ 平滑 + 腕部归一化 + 置信度门槛 + 滑动投票
        返回 (frame, act_id)，act_id 为 None 表示无确认手势
        """
        if not self.detec_hand or self.model is None:
            self._reset_vote_state(clear_confirmed=True)
            return frame, None

        hand = select_primary_hand(self.detector.results)
        if hand is None:
            self._reset_vote_state(clear_confirmed=True)
            return frame, None

        x = self.smoother.to_features(hand).reshape(1, -1)
        y = self.model.predict(x, verbose=0)
        act_id = int(np.argmax(y[0]))
        conf = float(np.max(y[0]))
        num_classes = int(y.shape[-1])

        if num_classes != len(self.actions) or not (0 <= act_id < len(self.actions)):
            cv2.putText(frame, 'Model/label mismatch, retrain', (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 0, 255), 2)
            return frame, None

        if conf < self.conf_threshold:
            cv2.putText(frame, f'Unsure  {conf:.2f}', (10, 30),
                        cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 165, 255), 2)
            cv2.putText(frame, f'Votes: {len(self.vote_window)}/{self.vote_window.maxlen}', (10, 70),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)
            return frame, None

        self.vote_window.append(act_id)
        votes = list(self.vote_window)
        vote_counts = np.bincount(votes, minlength=len(self.actions))
        top_id = int(np.argmax(vote_counts))
        top_votes = int(vote_counts[top_id])
        self.current_stable_id = top_id

        # 投票达标且与上次确认不同 → 确认新手势
        confirmed_id = None
        if top_votes >= self.vote_need and top_id != self.last_confirmed_id:
            confirmed_id = top_id
            self.last_confirmed_id = top_id
            gesture_name = self.actions[top_id]
            print(f"[确认手势] id: {top_id}, 名称: {gesture_name}")

        # 画面叠加信息
        gesture_name = self.actions[act_id]
        progress = min(top_votes / self.vote_need * 100, 100)
        cv2.putText(frame, f'Action: {gesture_name}  {conf:.2f}', (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 1.0, (0, 255, 0), 2)
        cv2.putText(frame, f'Vote: {self.actions[top_id]} {int(progress)}% ({top_votes}/{self.vote_need})', (10, 70),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)

        # 返回当前稳定的手势 id（用于 UART 发送），不在此处发串口
        return frame, self.last_confirmed_id if self.last_confirmed_id >= 0 else None

    # ==================== 主循环 ====================

    def run(self):
        print("[DEBUG] run() 开始")
        while True:
            event, value = self.window.read(timeout=40)
            if event == sg.WIN_CLOSED or event == 'exit':
                print("[DEBUG] 退出事件")
                self.uart_disconnect()
                break

            if event == 'check_hand':
                self.detec_hand = value['check_hand']

            if event == 'btn_uart_connect':
                if self.uart_connected:
                    self.uart_disconnect()
                    self.window['btn_uart_connect'].update('连接')
                    self.window['uart_status'].update('已断开')
                else:
                    port = value.get('com_port', '')
                    if port:
                        self.uart_connect(port)
                        if self.uart_connected:
                            self.window['btn_uart_connect'].update('断开')
                            self.window['uart_status'].update('已连接 {}'.format(port))
                    else:
                        sg.popup('请选择串口', title='UART')

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
                        # 预测 + 投票，返回稳定手势 id
                        frame, act_id = self._predict_action(frame)
                        # 映射为 UART ASCII 字符并通过 ACK 流控发送
                        uart_char = self.act_id_to_uart_char(act_id)
                        self.uart_process(uart_char)
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
