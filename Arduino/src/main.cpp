#include <Servo.h>
#include <PinChangeInterrupt.h>
#include <math.h> // HSV→RGB 변환 등 필요시

#define ESC_PIN    2
#define CH2_PIN    7   // 수신기 → CH2 (전진/후진)
#define SERVO_PIN  3
#define CH1_PIN    8   // 수신기 → CH1 (좌우 조향)
#define CH5_PIN    10  // 수신기 → CH5 (수동/자율 모드)
#define CH8_PIN    4   // 수신기 → CH8 (RGB LED 제어)
#define CH6_PIN    12  // 조종기 → CH6 (사이렌 ON/OFF)


Servo esc;    // ESC 속도 제어
Servo steer;  // 조향 제어

volatile unsigned long ch1_start = 0;
volatile unsigned long ch2_start = 0;
volatile unsigned long ch5_start = 0;
volatile unsigned long ch8_start = 0;
volatile unsigned long ch6_start = 0;

volatile uint16_t ch1_value = 1500;
volatile uint16_t ch2_value = 1500;
volatile uint16_t ch5_value = 1500;
volatile uint16_t ch8_value = 1500;
volatile uint16_t ch6_value = 1500;


String inputString   = "";
bool   stringComplete = false;

// ─────────── 수동/자율 공용 LED 핀 정의 ───────────
const uint8_t LEFT_LED_PIN   = 13;  // 좌회전 LED
const uint8_t RIGHT_LED_PIN  = 11;  // 우회전 LED

// ─────────── LED 깜빡임용 전역 변수 ───────────
unsigned long leftLastBlinkTime   = 0;
unsigned long rightLastBlinkTime  = 0;
bool         leftLedState         = LOW;
bool         rightLedState        = LOW;

// ─────────── 삼색(RGB) LED 핀 정의 ───────────
// CH8이 위로(>1600) 올라가면 색상 순환
const uint8_t RED_PIN    = 5;   // 빨강 PWM
const uint8_t GREEN_PIN  = 6;   // 초록 PWM
const uint8_t BLUE_PIN   = 9;   // 파랑 PWM

// ─────────── RGB 사이클용 전역 변수 ───────────
unsigned long lastHueUpdateTime   = 0;
int          hue                  = 0;   // 0..359

// ─────────── 스피커용 변수 ───────────
const int SPEAKER_PIN = A0;
bool sirenEnabled = false;

// ─────────── 함수 원형 선언 ───────────
void ch1_interrupt();
void ch2_interrupt();
void ch5_interrupt();
void ch8_interrupt();
void ch6_interrupt();
void serialEvent();

// HSV → RGB 변환 (h: 0..359, s=1, v=1)
void hsv_to_rgb(int h, uint8_t &r, uint8_t &g, uint8_t &b) {
  float hf = (float)h / 60.0f;
  int   i  = floor(hf);
  float f  = hf - i;
  float p  = 0.0f;
  float q  = 1.0f - f;
  float t  = f;

  float rf, gf, bf;
  switch (i) {
    case 0: rf = 1;  gf = t;  bf = 0;  break;
    case 1: rf = q;  gf = 1;  bf = 0;  break;
    case 2: rf = 0;  gf = 1;  bf = t;  break;
    case 3: rf = 0;  gf = q;  bf = 1;  break;
    case 4: rf = t;  gf = 0;  bf = 1;  break;
    default: rf = 1; gf = 0; bf = q;   break;  // case 5
  }
  r = uint8_t(rf * 255.0f);
  g = uint8_t(gf * 255.0f);
  b = uint8_t(bf * 255.0f);
}

void setup() {
  Serial.begin(9600);

  pinMode(CH1_PIN, INPUT);
  pinMode(CH2_PIN, INPUT);
  pinMode(CH5_PIN, INPUT);
  pinMode(CH6_PIN, INPUT);
  pinMode(SPEAKER_PIN, OUTPUT);

  esc.attach(ESC_PIN);
  steer.attach(SERVO_PIN);

  // LED 핀 출력 모드
  pinMode(LEFT_LED_PIN, OUTPUT);
  pinMode(RIGHT_LED_PIN, OUTPUT);
  digitalWrite(LEFT_LED_PIN, LOW);
  digitalWrite(RIGHT_LED_PIN, LOW);

  // RGB LED 핀 출력 설정
  pinMode(RED_PIN, OUTPUT);
  pinMode(GREEN_PIN, OUTPUT);
  pinMode(BLUE_PIN, OUTPUT);
  analogWrite(RED_PIN, 0);
  analogWrite(GREEN_PIN, 0);
  analogWrite(BLUE_PIN, 0);

  // PWM 입력용 인터럽트
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH1_PIN),
    ch1_interrupt, CHANGE
  );
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH2_PIN),
    ch2_interrupt, CHANGE
  );
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH5_PIN),
    ch5_interrupt, CHANGE
  );
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH8_PIN),
    ch8_interrupt, CHANGE
  );
  attachPinChangeInterrupt(
    digitalPinToPinChangeInterrupt(CH6_PIN),
    ch6_interrupt, CHANGE
  );
}

void loop() {
  bool autoMode = (ch5_value > 1500);
  unsigned long now = millis();

  int speed;    // ESC에 보낼 속도
  int angle;    // 서보에 보낼 조향 각도

  if (!autoMode) {
    // ───────────── 수동모드 ─────────────
    speed = constrain(ch2_value, 1440, 1560);
    angle = constrain(ch1_value, 1000, 2000);

    esc.writeMicroseconds(speed);
    steer.writeMicroseconds(angle);
  }
  else {
    // ───────────── 자율주행모드 ─────────────
    if (stringComplete) {
      inputString.trim();

      if (inputString.startsWith("D:")) {
        int deviation = inputString.substring(2).toInt();

        if (deviation > 0 && deviation < 40) {
          angle = constrain(1500 - deviation * 40, 1400, 1600);
          speed = 1560;
          
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation < 0 && deviation > -40) {
          angle = constrain(1500 - deviation * 40, 1400, 1600);
          speed = 1560;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation >= 40 && deviation < 80) {
          angle = 1000;
          speed = 1550;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation <= -40 && deviation > -80) {
          angle = 2000;
          speed = 1550;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        // 너무 큰 편차 시 후진
        else if (deviation >= 80 && deviation < 100) {
          angle = 1800;
          speed = 1440;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation <= -80 && deviation > -100) {
          angle = 1200;
          speed = 1440;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation >=100){
          angle = 1800;
          speed = 1430;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
        else if (deviation <= -100) {
          angle = 1200;
          speed = 1430;
          esc.writeMicroseconds(speed);
          steer.writeMicroseconds(angle);
        }
      }
      else if (inputString == "S" || inputString == "N") {
        angle = 1500;
        speed = 1435;
        esc.writeMicroseconds(speed);
        steer.writeMicroseconds(angle);
      }
      


      inputString = "";
      stringComplete = false;
    }
    
  }

  // ───────────── 수동/자율 공용 LED 제어 ─────────────
  // 실제 속도(speed)와 실제 조향(angle) 값으로 판단
  bool isReversing    = (speed < 1500);
  bool isTurningLeft  = (!isReversing && angle < 1450);
  bool isTurningRight = (!isReversing && angle > 1550);

  if (isReversing) {
    // 후진 구간: 양쪽 LED ON
    if (!leftLedState) {
      leftLedState = HIGH;
      digitalWrite(LEFT_LED_PIN, HIGH);
    }
    if (!rightLedState) {
      rightLedState = HIGH;
      digitalWrite(RIGHT_LED_PIN, HIGH);
    }
  }
  else {
    // 좌/우 회전 또는 직진/정지 판단
    if (isTurningLeft) {
      // 좌회전 구간: 왼쪽 LED 깜빡, 오른쪽 LED 끔
      if (rightLedState) {
        rightLedState = LOW;
        digitalWrite(RIGHT_LED_PIN, LOW);
      }
      if (now - leftLastBlinkTime >= 200) {
        leftLedState = !leftLedState;
        digitalWrite(LEFT_LED_PIN, leftLedState);
        leftLastBlinkTime = now;
      }
    }
    else if (isTurningRight) {
      // 우회전 구간: 오른쪽 LED 깜빡, 왼쪽 LED 끔
      if (leftLedState) {
        leftLedState = LOW;
        digitalWrite(LEFT_LED_PIN, LOW);
      }
      if (now - rightLastBlinkTime >= 200) {
        rightLedState = !rightLedState;
        digitalWrite(RIGHT_LED_PIN, rightLedState);
        rightLastBlinkTime = now;
      }
    }
    else {
      // 직진 또는 정지 구간: LED 모두 끔
      if (leftLedState) {
        leftLedState = LOW;
        digitalWrite(LEFT_LED_PIN, LOW);
      }
      if (rightLedState) {
        rightLedState = LOW;
        digitalWrite(RIGHT_LED_PIN, LOW);
      }
    }
  }

  // CH8이 위로(>1600) 올라가면 RGB LED 빨파빨파 점멸
  static bool policeState = false;
  static unsigned long lastPoliceToggle = 0;

  if (ch8_value > 1600) {
    if (now - lastPoliceToggle >= 300) {  // 300ms마다 색상 토글
      policeState = !policeState;
      lastPoliceToggle = now;

      if (policeState) {
        // 빨강 ON, 파랑 OFF
        digitalWrite(RED_PIN, HIGH);
        digitalWrite(BLUE_PIN, LOW);
      } else {
        // 빨강 OFF, 파랑 ON
        digitalWrite(RED_PIN, LOW);
        digitalWrite(BLUE_PIN, HIGH);
      }
      // 초록 OFF
      digitalWrite(GREEN_PIN, LOW);
    }
  }
  else {
    // CH8 값이 중앙/아래일 때 RGB LED 모두 끄기
    digitalWrite(RED_PIN, LOW);
    digitalWrite(GREEN_PIN, LOW);
    digitalWrite(BLUE_PIN, LOW);
  }

  // CH6 신호 기반 사이렌 제어
  if (ch6_value > 1500) {
    // 사이렌 활성화 상태면 사이렌 패턴 실행
    static unsigned long lastToneTime = 0;
    static int freq = 500;
    static bool up = true;

    if (millis() - lastToneTime >= 5) {
      tone(SPEAKER_PIN, freq);
      lastToneTime = millis();

      if (up) {
        freq += 10;
        if (freq >= 1000) up = false;
      } else {
        freq -= 10;
        if (freq <= 500) up = true;
      }
    }
  } else {
    noTone(SPEAKER_PIN);  // 사이렌 끄기
  }
}

void ch1_interrupt() {
  if (digitalRead(CH1_PIN)) ch1_start = micros();
  else                      ch1_value = micros() - ch1_start;
}

void ch2_interrupt() {
  if (digitalRead(CH2_PIN)) ch2_start = micros();
  else                      ch2_value = micros() - ch2_start;
}

void ch5_interrupt() {
  if (digitalRead(CH5_PIN)) ch5_start = micros();
  else                      ch5_value = micros() - ch5_start;
}

void ch8_interrupt() {
  if (digitalRead(CH8_PIN)) ch8_start = micros();
  else                      ch8_value = micros() - ch8_start;
}

void ch6_interrupt() {
  if (digitalRead(CH6_PIN))
    ch6_start = micros();
  else
    ch6_value = micros() - ch6_start;
}

void serialEvent() {
  while (Serial.available()) {
    char inChar = (char)Serial.read();
    if (inChar == '\n') {
      stringComplete = true;
    } else {
      inputString += inChar;
    }
  }
}
