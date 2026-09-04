# -*- coding:utf-8 -*-

import cv2
import numpy as np
import os, sys
import math
import PySimpleGUI as sg
import datetime
import time
import tensorflow as tf
from subprocess import call
import serial
import serial.tools.list_ports

from hand import Detector

if sys.platform == 'linux':
    FLAG = 2
else:               # win
    FLAG = 1        
    import pyaudio
    from ctypes import cast, POINTER
    from comtypes import CLSCTX_ALL
    from pycaw.pycaw import AudioUtilities, IAudioEndpointVolume

    CHUNK = 1024
    FORMAT = pyaudio.paInt16
    CHANNELS = 2
    RATE = 44100


MAX_DB = 200
    

class SGCV:
    def __init__(self) -> None:
        self.video_path = os.path.join(os.getcwd(), 'videos')
        print(self.video_path)
        if not os.path.isdir(self.video_path):
            os.mkdir(self.video_path)

        self.cameras = [i for i in range(self.get_cam_num())]
        self.camera_index = 0
        self.mirror = True      # 默认显示镜像
        self.right_layout = [[sg.Image(filename='', key='image')]]
        self.webcamera_layout = [[sg.Text('镜像显示：'), 
                                  sg.Radio(text='是', group_id='mirror', enable_events=True, default=True, key='is_mirror'),
                                  sg.Radio(text='否', group_id='mirror', enable_events=True, default=False, key='not_mirror')]]
        for cam in self.cameras:
            default = False
            text ='camera {}'.format(cam)
            key = 'camera_{}'.format(cam)
            if cam == self.camera_index:
                default = True
            self.webcamera_layout.append([sg.Radio(text=text, group_id='radio', enable_events=True ,default=default, key=key)])
        box_height = 10
        if len(self.cameras) >= 2:  box_height = 6
        self.record_layout = [[sg.Button('开始录像', key='recording'), sg.Button('保存路径', key='btn_show')],
                              [sg.Listbox(values=self.get_video_names(), size=(37, box_height), key='list_video', expand_x=True)]]
        self.voice_layout = [[sg.Text('扬声器'), sg.Button('开始测试', key='test_sound'), sg.Button('标定', key='calib')],
                             [sg.ProgressBar(100, orientation='h', size=(25, 20), key='progressbar1', expand_x=True)],
                             [sg.Text('麦克风'), sg.Button('开始测试', key='test_voice')],
                             [sg.ProgressBar(100, orientation='h', size=(20, 20), key='progressbar2', expand_x=True)]]
        # self.oper_layout = [[sg.Button("手势识别", key='gesHand'), sg.Button("人脸识别", key='gesFace'), sg.Button('退出', key='exit')]]
        self.oper_layout = [[sg.Text('开启检测：'),
                             sg.Checkbox('手势', default=False, enable_events=True, key='check_hand'),
                             sg.Checkbox('面部', default=False, enable_events=True, key='check_face'),
                             sg.Button('退出', key='exit')],
                            [sg.Text('串口：'),
                             sg.Combo([p.device for p in serial.tools.list_ports.comports()],
                                      size=10, key='com_port'),
                             sg.Button('连接', key='btn_uart_connect'),
                             sg.Text('', key='uart_status', size=(16, 1))]]

        self.deviceFrame = [sg.Frame(title='选择摄像头',layout=self.webcamera_layout, expand_x=True)]
        self.recordFrame = [sg.Frame(title='录像', layout=self.record_layout, expand_x=True)]
        self.voiceFrame = [sg.Frame(title='音量测试', layout=self.voice_layout, expand_x=True)]
        self.op_frame = [sg.Frame(title='操作', layout=self.oper_layout, expand_x=True)]

        self.layout = [[sg.Column(layout=[self.op_frame, self.deviceFrame, self.voiceFrame, self.recordFrame], size=(300, 500)), 
                        sg.VSeparator(), sg.Column(self.right_layout)]]
        
        self.window = sg.Window(title='手势动作检测', layout=self.layout)

        ###################
        self.video_out = None       # 视频录制对象
        self.is_record = False      # 是否在录制视频
        self.is_calib = False       # 是否在标定模式
        self.is_test_sound = False  # 是否使用手势控制音量
        self.is_test_voice = False  # 测试麦克风

        self.detec_hand = False      # 是否要检测手势
        self.detec_face = False      # 是否要检测人脸
        # self.window['image'].set_size((800, 600))
        # self.actions = np.array(['one', 'yeah', 'three', 'four', 'good', 'not good', 'ok', 'palm', 'fist'])
        self.actions = np.array(['one', 'two', 'three', 'four', 'five', 'six', 'seven', 'eight', 'nine' , 'ten', 'good', 'not good', 'ok'])
        if len(self.cameras) > 0:
            self.cap = cv2.VideoCapture(self.cameras[self.camera_index] * FLAG)
        self.detector = Detector(is_face=self.detec_face, is_hand=self.detec_hand)
        model_file = self.resource_path('./Model/action_13.h5')
        if os.path.isfile(model_file):
            self.model = tf.keras.models.load_model(model_file)
        else:
            print('Warning: model not found at {}, gesture recognition disabled. Run gesture.py train() first.'.format(model_file))
            self.model = None

        if sys.platform != 'linux':
            devices = AudioUtilities.GetSpeakers()
            # pycaw 新版本直接提供 EndpointVolume；旧版本仍走 Activate
            if hasattr(devices, 'EndpointVolume'):
                self.volume = devices.EndpointVolume
            else:
                interface = devices.Activate(IAudioEndpointVolume._iid_, CLSCTX_ALL, None)
                self.volume = cast(interface, POINTER(IAudioEndpointVolume))
            global CHUNK,FORMAT,CHANNELS,RATE
            p = pyaudio.PyAudio()
            self.stream = p.open(format=FORMAT,channels=CHANNELS,rate=RATE,input=True,frames_per_buffer=CHUNK)

        # 标定相关变量
        self.stop_time = None
        self.len_max = 0

        # ----- UART 通信状态 / UART communication state -----
        self.serial_port = None           # pyserial Serial 实例
        self.uart_connected = False       # 是否已连接
        self.uart_last_sent_char = None   # 上次发送的 UART 字符
        self.uart_ack_received = True     # 是否已收到 ACK
        self.uart_send_time = 0           # 上次发送时间戳（用于超时重发）
        self.uart_ack_timeout = 0.2       # 200 ms ACK 超时


    def get_video_names(self):
        """ 获取视频列表 """
        if not os.path.isdir(self.video_path):
            os.mkdir(self.video_path)
        try:
            file_list = os.listdir(self.video_path)
        except:
            file_list = []
        fnames = [
            f
            for f in file_list
            if os.path.isfile(os.path.join(self.video_path, f))
            and f.lower().endswith((".avi"))
        ]
        return fnames
    

    def get_cam_num(self):
        num = 0
        max_num = 10
        for device in range(0, max_num, FLAG):
            stream = cv2.VideoCapture(device)
            grabbed = stream.grab()
            stream.release()
            if not grabbed:
                break
            num += 1
        return num


    def resource_path(self, relative_path):
        base_path = getattr(sys, '_MEIPASS', os.path.dirname(os.path.abspath(__file__)))
        return os.path.join(base_path, relative_path)


    def predictAction(self, frame):
        # 使用模型预测动作，输出类别编号 0-12，返回 (frame, act_id)
        # act_id 为 None 表示无手检测或模型不可用
        act_id = None
        if self.detec_hand and self.model is not None:
            if self.detector.results.multi_hand_landmarks:
                x_train = np.array([[res.x, res.y, res.z] for res in self.detector.results.multi_hand_landmarks[0].landmark]).flatten()
                x_train = np.reshape(x_train, (1, -1))
                y_pre = self.model.predict(x_train)
                act_id = int(np.argmax(y_pre[0]))  # 0-12
                cv2.putText(frame, 'Action id: {}'.format(act_id), (10,30), cv2.FONT_HERSHEY_SIMPLEX, 1, (0, 255, 0), 3, cv2.LINE_AA)
        return frame, act_id

    @staticmethod
    def act_id_to_uart_char(act_id):
        """手势类别 → UART 字符映射 / Gesture class -> UART character mapping

        类别 0-8 (one..nine) → '1'-'9'
        类别 9 (ten/fist)   → '0'
        类别 12 (ok)        → 'K'
        类别 10,11 (good/not good) → None (无 MCU 动作)
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
        """打开串口连接 / Open serial port connection"""
        try:
            self.serial_port = serial.Serial(
                port=port_name,
                baudrate=115200,
                bytesize=serial.EIGHTBITS,
                parity=serial.PARITY_NONE,
                stopbits=serial.STOPBITS_ONE,
                timeout=0,           # 非阻塞读 / non-blocking read
                write_timeout=0
            )
            self.uart_connected = True
            self.uart_last_sent_char = None
            self.uart_ack_received = True
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
        Called every GUI frame: send gesture char, check for ACK.

        Protocol:
          - current_char 为 None 时（无映射手势），不发送，MCU 保持上次显示
          - 新手势或首次发送：立即发送字符
          - 等待 ACK 中：检查串口是否收到 '#'
          - 超时（>200 ms）未收到 ACK：重发
        """
        if not self.uart_connected:
            return

        # 无映射手势，不发送 / No mappable gesture, don't send
        if current_char is None:
            self.uart_last_sent_char = None
            self.uart_ack_received = True
            return

        # 新手势或 ACK 已收到 -> 发送 / New gesture or ACK received -> send
        if current_char != self.uart_last_sent_char or self.uart_ack_received:
            try:
                self.serial_port.write(current_char.encode('ascii'))
            except serial.SerialException:
                pass
            self.uart_last_sent_char = current_char
            self.uart_ack_received = False
            self.uart_send_time = time.time()
            return

        # 等待 ACK / Waiting for ACK
        if not self.uart_ack_received:
            # 非阻塞读取所有可用字节 / Non-blocking read all available bytes
            try:
                if self.serial_port.in_waiting > 0:
                    rx_data = self.serial_port.read(self.serial_port.in_waiting)
                    if b'#' in rx_data:
                        self.uart_ack_received = True
                        return
            except serial.SerialException:
                pass
            # 超时重发 / Timeout resend
            if (time.time() - self.uart_send_time) > self.uart_ack_timeout:
                try:
                    self.serial_port.write(self.uart_last_sent_char.encode('ascii'))
                except serial.SerialException:
                    pass
                self.uart_send_time = time.time()
    

    def calib(self, frame):
        """ 标定食指和拇指指尖的距离 """
        if self.detector.results.multi_hand_landmarks:
            land_marks = self.detector.results.multi_hand_landmarks[0]
            normalized_landmarks = self.detector.Normalize_landmarks(image=frame, hand_landmarks=land_marks)
            frame, length = self.detector.Draw_hand_points(frame, normalized_landmarks)
            strRate = 'Start calibration'
            cv2.putText(frame, strRate, (10, 410), cv2.FONT_HERSHEY_COMPLEX, 1.2, (255, 0, 0), 2)
            strRate1 = 'max length = %d'%self.len_max
            cv2.putText(frame, strRate1, (10, 30), cv2.FONT_HERSHEY_COMPLEX, 1.2, (255, 0, 0), 2)

            if length > self.len_max:
                self.len_max = length
        return frame


    def gesCtrlSound(self, frame):
        """ 根据检测到拇指和食指距离调整系统音量 """
        if self.detector.results.multi_hand_landmarks:
            land_marks = self.detector.results.multi_hand_landmarks[0]
            normalized_landmarks = self.detector.Normalize_landmarks(image=frame, hand_landmarks=land_marks)
            try:
                frame, length = self.detector.Draw_hand_points(frame, normalized_landmarks)
                cv2.rectangle(frame, (50, 150), (85, 350), (255, 0, 0), 1)
                if length >self.len_max:
                    length = self.len_max

                vol = int((length) / self.len_max * 100)
                if sys.platform != 'linux':      # win
                    self.volume.SetMasterVolumeLevel(self.detector.vol_tansfer(vol), None)
                else:                   # linux
                    call(["amixer", "-D", "pulse", "sset", "Master", "{}%".format(vol)])
                
                self.window['progressbar1'].update(vol)
 
                cv2.rectangle(frame, (50, 150+200-2*vol), (85, 350), (255, 0, 0), cv2.FILLED)
                percent = int(length / self.len_max * 100)
 
                strRate = str(percent) + '%'
                cv2.putText(frame, strRate, (40, 410), cv2.FONT_HERSHEY_COMPLEX, 1.2, (255, 0, 0), 2)
                cv2.putText(frame, vol, (10, 470), cv2.FONT_HERSHEY_COMPLEX, 1.2, (255, 0, 0), 2)
            except:
                pass
    

    def testVoice(self):
        if sys.platform != 'linux':      # win
            data = self.stream.read(CHUNK)
            audio_data = np.fromstring(data, dtype=np.short)
            max_dB = np.max(audio_data)
            vol = int((max_dB) / MAX_DB * 100)
            self.window['progressbar2'].update(vol)
        else:
            pass


    def run(self):
        # 获取系统扬声器、麦克风的值
        while True:
            if len(self.cameras) == 0:
                sg.popup('没有检测到摄像头，请检查连接', title='错误')
                break

            event, value = self.window.read(timeout=40)
            if event == sg.WIN_CLOSED or event == 'exit':
                self.uart_disconnect()
                break

            # if event == 'gesHand':          # 手势界面
            #     sg.popup('目前可以识别包括1-10以及good、not good、ok在内的13种手势', title='手势说明')

            if event == 'check_hand':           # 开启/关闭 检测手势
                self.detec_hand = value['check_hand']
                self.detector.setModel(face=self.detec_face, hand=self.detec_hand)

            if event == 'check_face':           # 开启/关闭 检测人脸
                self.detec_face = value['check_face']
                self.detector.setModel(face=self.detec_face, hand=self.detec_hand)

            if event == 'btn_uart_connect':     # 串口连接/断开
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

            # 镜像切换
            if event == 'is_mirror':
                self.mirror = True
            if event == 'not_mirror':
                self.mirror = False
            
            # 切换相机事件
            if event in ['camera_{}'.format(i) for i in self.cameras]:
                for i in self.cameras:
                    if value['camera_{}'.format(i)] == True and i != self.camera_index:
                        self.cap.release()
                        self.cap = cv2.VideoCapture(i * FLAG)
                        self.camera_index = i

            # 录像事件
            if event == 'recording':
                if self.is_record:
                    self.video_out.release()
                    self.video_out = None
                    self.is_record = False
                    self.window['recording'].update('开始录制')
                    # 更新列表
                    self.window['list_video'].update(self.get_video_names())
                else:                   # 否则，开始录制
                    filename = datetime.datetime.now().strftime("%Y%m%d%H%M%S")
                    self.video_out = cv2.VideoWriter('videos/{}.avi'.format(filename), cv2.VideoWriter_fourcc(*'XVID'), 20.0, (640,480))
                    self.is_record = True
                    self.window['recording'].update('停止录制')
            
            if event == 'btn_show':
                sg.popup(self.video_path, title='视频保存路径')

            if event == 'test_sound':           # 测试扬声器
                if not self.detec_hand:
                    sg.popup("请先开启手势检测后操作")
                elif self.len_max == 0:
                    sg.popup('请先点击右侧标定按钮进行标定')
                elif self.is_test_sound:        # 使用手势控制扬声器音量大小
                    self.is_test_sound = False
                    self.window['test_sound'].update('开始测试')
                else:
                    self.is_test_sound = True
                    self.window['test_sound'].update('停止测试')
            

            if event == 'test_voice':           # 测试麦克风
                if self.is_test_voice:
                    self.is_test_voice = False
                    self.window['test_voice'].update('开始测试')
                else:
                    self.is_test_voice = True
                    self.window['test_voice'].update('停止测试')


            if event == 'calib':       # 调试扬声器
                if self.detec_hand:
                    sg.popup("点击OK开始标定，请将拇指与食指张开到最大距离")
                    self.is_calib = True
                    self.len_max = 0
                    self.stop_time = datetime.datetime.now() + datetime.timedelta(seconds=5)
                else:
                    sg.popup("请先开启手势检测后操作")

            
            if self.cap is not None:
                ret, frame = self.cap.read()
                if self.mirror:
                    frame = cv2.flip(frame, 1)

                frame = self.detector.runDetec(frame)
                if self.is_calib:           # 如果当前在标定模式
                    if datetime.datetime.now() < self.stop_time:
                        frame = self.calib(frame=frame)
                    else:   # 标定结束
                        sg.popup('标定结束')
                        self.is_calib = False
                        self.window['calib'].update('重新标定')
                        # print("len_max is {}".format(self.len_max))
                elif self.is_test_sound:       # 手势控制音量
                    self.gesCtrlSound(frame=frame)
                else:
                    # 检测人脸和手势，获取 act_id 并通过 UART 发送
                    # Detect face and hand, get act_id and send via UART
                    frame, act_id = self.predictAction(frame=frame)
                    uart_char = self.act_id_to_uart_char(act_id)
                    self.uart_process(uart_char)
                
                if self.is_test_voice:
                    self.testVoice()

                if self.is_record:
                    self.video_out.write(frame)
                    cv2.putText(frame,'Recording',  (10, 70), cv2.FONT_HERSHEY_COMPLEX, 1, (255, 0, 0), 3, cv2.LINE_AA)
                
                flag, frame = cv2.imencode(ext='.png', img=frame)
                self.window['image'].update(data=frame.tobytes())

if __name__ == '__main__':
    sg_demo = SGCV()
    sg_demo.run()
