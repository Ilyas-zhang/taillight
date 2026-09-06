import cv2
import math
import mediapipe as mp


class Detector:
    def __init__(self, static_image_mode=False,
                        max_num_hands=2,
                        model_complexity=1,
                        min_detection_confidence=0.6,
                        min_tracking_confidence=0.6) -> None:
        # 检测手势
        self.mp_hands = mp.solutions.hands
        self.hands = self.mp_hands.Hands(static_image_mode=static_image_mode,
                                        max_num_hands=max_num_hands,
                                        model_complexity=model_complexity,
                                        min_detection_confidence=min_detection_confidence,
                                        min_tracking_confidence=min_tracking_confidence)
        
        self.mp_drawing = mp.solutions.drawing_utils
        self.mp_drawing_styles = mp.solutions.drawing_styles
        self.tipIds = [4, 8, 12, 16, 20]			# 指尖列表
        self.lm_x_point = []
        self.lm_y_point = []
        self.frame_index = 0

        self.results = None
    def detecHands(self, image):
        return self.runDetec(image)
    
    def runDetec(self, image):
        """ 运行检测程序 """
        
        image.flags.writeable = False
        image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)
        self.results = self.hands.process(image)
        image.flags.writeable = True
        image = cv2.cvtColor(image, cv2.COLOR_RGB2BGR)

        if self.results.multi_hand_landmarks:
            for hand_landmarks in self.results.multi_hand_landmarks:
                self.mp_drawing.draw_landmarks(
                    image,
                    hand_landmarks,
                    self.mp_hands.HAND_CONNECTIONS,
                    self.mp_drawing_styles.get_default_hand_landmarks_style(),
                    self.mp_drawing_styles.get_default_hand_connections_style()
                )

        return image
    

    def getBox(self, img) -> list:
        """ 计算手部周围的边界框 """
        bboxs = []
        if not self.results or not self.results.multi_hand_landmarks:
            return bboxs
        image_height, image_width, _ = img.shape
        if self.results.multi_hand_landmarks:
            # 遍历检测到的所有的手
            self.lm_x_point.clear()
            self.lm_y_point.clear()
            # self.handTypes.clear()
            for idx, hand_landmarks in enumerate(self.results.multi_hand_landmarks):
                xList, yList = [], []
                for lm in hand_landmarks.landmark:
                    xList.append(int(lm.x * image_width))
                    yList.append(int(lm.y * image_height))

                xmin, xmax = min(xList), max(xList)
                ymin, ymax = min(yList), max(yList)
                boxW, boxH = xmax - xmin, ymax - ymin
                bbox = xmin, ymin, boxW, boxH

                # 画边界框
                cv2.rectangle(img, (xmin - 20, ymin - 20),
                            (xmax + 20, ymax + 20), (0, 255, 0), 2)

                # 标注左右手
                hand_type = self.results.multi_handedness[idx].classification[0].label
                cv2.putText(img, f"id:{idx} {hand_type}",
                            (xmin - 25, ymin - 20),
                            cv2.FONT_HERSHEY_PLAIN, 1, (0, 0, 255), 1)

                self.lm_x_point.append(xList)
                self.lm_y_point.append(yList)
                bboxs.append(bbox)

        return bboxs


    def fingersIsUp(self) -> list:
        """
        返回竖起和弯曲的手指列表,1表示竖起,0表示弯曲
        """
        multi_fingers = []
        if not self.results or not self.results.multi_hand_landmarks:
            return multi_fingers

        for idx, _ in enumerate(self.results.multi_hand_landmarks):
            hand_type = self.results.multi_handedness[idx].classification[0].label
            fingers = []

            # 大拇指：根据左右手判断
            if hand_type == "Right":
                fingers.append(1 if self.lm_x_point[idx][self.tipIds[0]] <
                               self.lm_x_point[idx][self.tipIds[0] - 1] else 0)
            else:
                fingers.append(1 if self.lm_x_point[idx][self.tipIds[0]] >
                               self.lm_x_point[idx][self.tipIds[0] - 1] else 0)

            # 其余4根手指：指尖 y < 第三个关节 y = 伸直
            for i in range(1, 5):
                fingers.append(1 if self.lm_y_point[idx][self.tipIds[i]] <
                               self.lm_y_point[idx][self.tipIds[i] - 2] else 0)

            multi_fingers.append(fingers)

        return multi_fingers
        
    def getHandType(self):
        """返回每只手的类型列表 ['Right', 'Left', ...]"""
        if not self.results or not self.results.multi_handedness:
            return []
        return [h.classification[0].label for h in self.results.multi_handedness]
    
if __name__ == '__main__':
    detector = Detector()
    cap = cv2.VideoCapture(0)

    while cap.isOpened():
        ret, frame = cap.read()
        if not ret:
            print("无法读取摄像头画面")
            break

        frame = cv2.flip(frame, 1)  # 镜像翻转
        frame = detector.runDetec(frame)
        bboxs = detector.getBox(frame)

        if bboxs:
            fingers = detector.fingersIsUp()
            hand_types = detector.getHandType()
            for i, (ht, fg) in enumerate(zip(hand_types, fingers)):
                status = " ".join(["↑" if f else "·" for f in fg])
                cv2.putText(frame, f"{ht}: {status}",
                            (10, 60 + i * 30),
                            cv2.FONT_HERSHEY_SIMPLEX, 0.7, (255, 255, 0), 2)

        cv2.imshow('Hand Detection', frame)
        if cv2.waitKey(1) & 0xFF == 27:  # 按 ESC 退出
            break

    cap.release()
    cv2.destroyAllWindows()