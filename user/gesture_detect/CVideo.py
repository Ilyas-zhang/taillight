"""
  ============ CVideo.py ============
  PC 端手势检测主程序：摄像头画面 → MediaPipe 手部检测 → 二进制手指计数
  → UART 发送 → MCU LED 显示。

  PC-side gesture detection main app: camera → MediaPipe hand detection
  → binary finger counting → UART send → MCU LED display.
"""
import sys
import traceback

# 把所有的错误同时写到 error.log 文件里，防止被吞掉
def excepthook(exc_type, exc_value, exc_tb):
    with open('error.log', 'w', encoding='utf-8') as f:
        traceback.print_exception(exc_type, exc_value, exc_tb, file=f)
    traceback.print_exception(exc_type, exc_value, exc_tb)
sys.excepthook = excepthook

import cv2
import numpy as np
import os
import PySimpleGUI as sg
import datetime
import serial
import serial.tools.list_ports
import time

from hand import Detector
from config import load_config
from landmarks import (LandmarkSmoother, select_primary_hand,
                        get_finger_states, detect_ok_gesture, FingerDebouncer)


class SGCV:
    def __init__(self) -> None:
        self.video_path = os.path.join(os.getcwd(), 'videos')
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

        self.window = sg.Window('手势动作检测', layout, size=(1100, 650), resizable=True)

        # ---- 摄像头 ----
        self.cap = cv2.VideoCapture(self.cameras[0], cv2.CAP_DSHOW)

        # ---- 检测器 ----
        self.detector = Detector()

        # ---- 手势检测状态（二进制计数模式）/ Gesture detection state ----
        cfg = load_config()
        self.smoother = LandmarkSmoother(alpha=cfg["smoother_alpha"])
        self.finger_debouncer = FingerDebouncer(
            window_size=cfg["finger_debounce_window"],
            threshold=cfg["finger_debounce_threshold"])
        self.ok_dist_threshold = cfg["ok_dist_threshold"]
        self._current_handedness = None

        # ---- 数值稳定性 / Value stability ----
        # 连续 N 帧输出相同 UART 字符才确认发送
        self._stable_candidate = None
        self._stable_count = 0
        self._stable_need = cfg["value_stable_need"]
        self._last_confirmed_char = None

        # ---- UART 通信状态 ----
        self.serial_port = None
        self.uart_connected = False
        self.uart_last_sent_char = None
        self.uart_ack_received = True
        self.uart_send_time = 0
        self.uart_ack_timeout = 0.2       # 200 ms ACK 超时
        self._uart_warned = False          # 是否已打印过"未连接"警告

        # ---- 列出可用串口 / List available serial ports ----
        ports = serial.tools.list_ports.comports()
        if ports:
            print(f"[UART] 检测到 {len(ports)} 个串口:")
            for p in ports:
                print(f"  - {p.device}  {p.description}")
        else:
            print("[UART] ⚠ 未检测到任何串口！请确认 MCU 已通过 USB 连接")

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

    def _reset_gesture_state(self):
        """重置所有手势检测状态 / Reset all gesture detection state"""
        self.smoother.reset()
        self.finger_debouncer.reset()
        self._current_handedness = None
        self._stable_candidate = None
        self._stable_count = 0
        self._last_confirmed_char = None

    # ==================== UART 通信 ====================

    def uart_connect(self, port_name):
        """打开串口连接 / Open serial port connection"""
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
            self._uart_warned = False
        except serial.SerialException as e:
            sg.popup('串口连接失败: {}'.format(e), title='UART 错误')

    def uart_disconnect(self):
        """关闭串口连接 / Close serial port connection"""
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
            # 只在首次有确认手势但未连接时提醒一次
            if current_char is not None and not self._uart_warned:
                print(f"[UART] ⚠ 未连接串口，无法发送 '{current_char}' "
                      f"— 请在左侧选择 COM 口并点击「连接」")
                self._uart_warned = True
            return

        # 无映射手势，不发送
        if current_char is None:
            self.uart_last_sent_char = None
            self.uart_ack_received = True
            return

        # 新手势或 ACK 已收到 -> 发送
        if current_char != self.uart_last_sent_char or self.uart_ack_received:
            print(f"[UART] 发送: '{current_char}' (0x{ord(current_char):02X})")
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

    # ==================== 手势检测（二进制计数） ====================

    @staticmethod
    def _binary_to_uart_char(value):
        """
        二进制计数值 → UART ASCII 字符映射
        / Binary finger count value -> UART ASCII character mapping.

        0 (拳头/fist)    → '0'  (兼容旧的 ten/fist 手势)
        1-9              → '1'-'9'
        10               → '0'  (十 / digit 10 shows as 0)
        11-31            → None (MCU 无对应显示)

        OK 手势单独处理，不经过此函数。
        / OK gesture is handled separately; does not go through this function.

        手指位权：拇指=1, 食指=2, 中指=4, 无名指=8, 小指=16
        / Finger bit weights: thumb=1, index=2, middle=4, ring=8, pinky=16
        """
        if 1 <= value <= 9:
            return chr(ord('0') + value)   # 1->'1', 2->'2', ..., 9->'9'
        elif value == 0 or value == 10:
            return '0'
        return None

    def _stabilize_value(self, uart_char):
        """
        连续帧稳定性确认：同一字符连续出现 N 帧才确认。
        / Consecutive-frame stability: same char for N frames to confirm.

        返回确认的字符，未确认时返回上次确认的字符。
        / Returns confirmed char; if not yet confirmed, returns last confirmed char.
        """
        if uart_char == self._stable_candidate:
            self._stable_count += 1
        else:
            self._stable_candidate = uart_char
            self._stable_count = 1

        if self._stable_count >= self._stable_need:
            self._last_confirmed_char = uart_char

        return self._last_confirmed_char

    def _detect_gesture(self, frame):
        """
        平滑 → 手指状态 → 去抖 → OK手势检测 → 二进制计数 → 稳定性确认
        / Smooth → finger states → debounce → OK check → binary count → stabilize

        返回 (frame, uart_char)，uart_char 为 None 表示无确认手势
        / Returns (frame, uart_char); uart_char is None if no confirmed gesture
        """
        if not self.detec_hand:
            self._reset_gesture_state()
            return frame, None

        # ---- 选择主手 / Select primary hand ----
        hand, handedness = select_primary_hand(self.detector.results)
        if hand is None:
            self._reset_gesture_state()
            return frame, None

        # ---- 手型切换时重置平滑器和去抖器 ----
        # / Reset smoother and debouncer when handedness changes
        if handedness != self._current_handedness:
            self._current_handedness = handedness
            self.smoother.reset()
            self.finger_debouncer.reset()

        # ---- EMA 关键点平滑 / EMA landmark smoothing ----
        smoothed = self.smoother.update(hand)  # 21×3 numpy array

        # ---- 手指状态检测 / Finger state detection ----
        raw_states = get_finger_states(smoothed, handedness)

        # ---- 每指独立去抖 / Per-finger debounce ----
        debounced = self.finger_debouncer.update(raw_states)

        # ---- 计算二进制值 / Compute binary value ----
        binary_value = (debounced[0] * 1 + debounced[1] * 2 +
                        debounced[2] * 4 + debounced[3] * 8 +
                        debounced[4] * 16)

        # ---- OK 手势优先检测 / OK gesture takes priority ----
        is_ok = detect_ok_gesture(smoothed, debounced, self.ok_dist_threshold)
        if is_ok:
            uart_char = 'K'
        else:
            uart_char = self._binary_to_uart_char(binary_value)

        # ---- 连续帧稳定性确认 / Consecutive-frame stability ----
        confirmed_char = self._stabilize_value(uart_char)

        # ---- 画面叠加信息 / Visual overlay ----
        finger_names = ['T', 'I', 'M', 'R', 'P']
        state_str = ' '.join(f'{n}:{"↑" if s else "·"}' for n, s in zip(finger_names, debounced))
        cv2.putText(frame, f'Fingers: {state_str}', (10, 30),
                    cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)

        if is_ok:
            cv2.putText(frame, 'Gesture: OK', (10, 60),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 255), 2)
        elif uart_char is not None:
            cv2.putText(frame, f'Value: {binary_value} -> UART: {uart_char}', (10, 60),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 255, 0), 2)
        else:
            cv2.putText(frame, f'Value: {binary_value} (unmapped)', (10, 60),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (0, 165, 255), 2)

        if confirmed_char is not None:
            cv2.putText(frame, f'Confirmed: {confirmed_char}', (10, 90),
                        cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)

        return frame, confirmed_char

    # ==================== 主循环 ====================

    def run(self):
        while True:
            event, value = self.window.read(timeout=40)
            if event == sg.WIN_CLOSED or event == 'exit':
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
                        # 手势检测：手指状态 → OK/二进制计数 → 稳定性确认 → UART 字符
                        frame, uart_char = self._detect_gesture(frame)
                        self.uart_process(uart_char)
                    if self.is_record:
                        self.video_out.write(frame)
                        cv2.putText(frame, 'Recording', (10, 110),
                                    cv2.FONT_HERSHEY_COMPLEX, 1, (255, 0, 0), 3)
                    _, buf = cv2.imencode('.png', frame)
                    self.window['image'].update(data=buf.tobytes())

        if self.cap:
            self.cap.release()
        if self.video_out:
            self.video_out.release()
        self.window.close()


if __name__ == '__main__':
    app = SGCV()
    app.run()
