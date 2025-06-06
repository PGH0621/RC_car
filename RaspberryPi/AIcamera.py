from flask import Flask, Response
import cv2
import numpy as np
import serial
import time
from picamera2 import Picamera2

# ─────────────────────────────
# 📡 시리얼 통신 초기화
# ─────────────────────────────
ser = serial.Serial('/dev/ttyUSB0', 9600)
time.sleep(2)

# ─────────────────────────────
# 🌐 Flask 앱 초기화
# ─────────────────────────────
app = Flask(__name__)

# ─────────────────────────────
# 📷 PiCamera2 설정
# ─────────────────────────────
picam2 = Picamera2()
picam2.configure(picam2.create_video_configuration(
    main={"format": "RGB888", "size": (160, 92)}))
picam2.start()

# ─────────────────────────────
# 🧠 영상 처리 및 deviation 전송
# ─────────────────────────────
def process_frame(frame):
    height, width, _ = frame.shape
    center_x = width // 2

    # 1) 그레이스케일 + 블러 + 이진화
    gray = cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)
    blur = cv2.GaussianBlur(gray, (5, 5), 0)
    _, binary = cv2.threshold(blur, 0, 255,
                              cv2.THRESH_BINARY_INV + cv2.THRESH_OTSU)

    # 2) 이진화된 흰 픽셀 좌표 전체 추출 (y, x 순서)
    ys, xs = np.where(binary > 0)

    # (예외 처리) 픽셀 수가 너무 적으면 정지 신호
    if len(xs) < 50:
        ser.write(b'S\n')
        print("[Send] S")
        # 디버그 프레임은 원본 그대로 반환
        _, jpeg = cv2.imencode('.jpg', frame)
        return jpeg.tobytes()
    
    # (예외 처리) 픽셀 수가 너무 많많으면 정지 신호
    if len(xs) > 2000:
        ser.write(b'N\n')
        print("[Send] N")
        # 디버그 프레임은 원본 그대로 반환
        _, jpeg = cv2.imencode('.jpg', frame)
        return jpeg.tobytes()

    # 3) 선형 회귀: x = a*y + b
    #    → np.polyfit에 넘길 때는 y_global, xs
    y_global = ys  # ROI 없이 전체 프레임 기준 y
    # np.polyfit(독립변수=y_global, 종속변수=xs, 차수=1)
    a, b_lin = np.polyfit(y_global, xs, 1)

    # 4) 현재(하단) 편차 계산
    y_current = height - 1
    x_current = a * y_current + b_lin
    dev_current = x_current - center_x

    # 5) 예측(상단) 편차 계산
    #    y_future 값을 프레임 높이의 60% 지점 정도로 설정
    y_future = int(height * 0.6)
    x_future = a * y_future + b_lin
    dev_future = x_future - center_x

    # 6) 최종 편차: 현재와 예측을 가중 평균 (가중치는 상황에 따라 조정 가능)
    #    여기서는 예측을 약간 줄이고, 현재 중심 제어를 더 강하게
    w_curr = 0.7
    w_fut  = 0.3
    dev_final = int(dev_current * w_curr + dev_future * w_fut)

    # 7) 시리얼 전송
    message = f"D:{dev_final}\n"
    ser.write(message.encode())
    print(f"[Send] {message.strip()} (curr: {int(dev_current)}, fut: {int(dev_future)})")

    # 8) 디버그 시각화
    debug = frame.copy()
    # - 중심선
    cv2.line(debug, (center_x, 0), (center_x, height), (255, 0, 0), 1)
    # - 하단 예측점
    cv2.circle(debug, (int(x_future), y_future), 4, (0, 0, 255), -1)
    # - 현재 출발점
    cv2.circle(debug, (int(x_current), y_current), 4, (0, 255, 0), -1)

    # - 이진화 ROI 컬러로 덮어 씌우기 (하단 50%만 시각화)
    roi_low = binary[int(height * 0.5):height, :]
    roi_color_low = cv2.cvtColor(roi_low, cv2.COLOR_GRAY2BGR)
    debug[int(height * 0.5):height, :] = roi_color_low

    _, jpeg = cv2.imencode('.jpg', debug)
    return jpeg.tobytes()

# ─────────────────────────────
# 📽️ 스트리밍 제너레이터
# ─────────────────────────────
def generate():
    while True:
        frame = picam2.capture_array()
        yield (b'--frame\r\n'
               b'Content-Type: image/jpeg\r\n\r\n' +
               process_frame(frame) +
               b'\r\n')

# ─────────────────────────────
# 🌐 라인 추적 영상 페이지
# ─────────────────────────────
@app.route('/')
def index():
    return '''
    <!DOCTYPE html>
    <html lang="ko">
    <head>
        <meta charset="UTF-8">
        <title>RC카 라인 추적 실시간 뷰어</title>
        <style>
            body {
                background-color: #121212;
                color: #fff;
                font-family: 'Segoe UI', sans-serif;
                text-align: center;
                margin: 0;
                padding: 0;
            }
            h1 {
                margin: 20px;
                font-weight: 500;
            }
            .video-container {
                display: inline-block;
                background: #222;
                padding: 10px;
                border-radius: 12px;
                box-shadow: 0 0 15px rgba(0, 255, 0, 0.3);
            }
            img {
                max-width: 100%;
                border-radius: 8px;
            }
        </style>
    </head>
    <body>
        <h1>📷 RC카 라인 추적 영상</h1>
        <div class="video-container">
            <img src="/video_feed" alt="Video stream">
        </div>
    </body>
    </html>
    '''

# ─────────────────────────────
# 🔁 비디오 피드 라우트
# ─────────────────────────────
@app.route('/video_feed')
def video_feed():
    return Response(generate(),
                    mimetype='multipart/x-mixed-replace; boundary=frame')

# ─────────────────────────────
# 🚀 서버 실행
# ─────────────────────────────
if __name__ == '__main__':
    app.run(host='0.0.0.0', port=5000, debug=False)
