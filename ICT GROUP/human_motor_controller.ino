#include <Servo.h>

const byte HUMAN_SERVO_PIN = 9;
const byte CONTINUOUS_SERVO_PIN = 10;
const byte BUTTON_SERVO_PIN = 11;
const byte CONTINUOUS_BUTTON_PIN = 2;
const byte POSITION_BUTTON_PIN = 3;

const int SERVO_HOME_ANGLE = 0;
const int SERVO_ACTION_ANGLE = 90;

const int CONTINUOUS_STOP = 90;
const int CONTINUOUS_SPIN = 180;
const unsigned long BUTTON_DEBOUNCE_MS = 30;

Servo humanServo;
Servo continuousServo;
Servo buttonServo;

String uartMessage;
int buttonServoAngle = SERVO_HOME_ANGLE;
bool positionButtonReading = HIGH;
bool positionButtonState = HIGH;
unsigned long positionButtonChangedAt = 0;

void handleUartMessage(const String& message) {
  if (message == "person detected") {
    humanServo.write(SERVO_ACTION_ANGLE);
  } else if (message == "person not detected") {
    humanServo.write(SERVO_HOME_ANGLE);
  }
}

void readEsp32Uart() {
  while (Serial.available() > 0) {
    const char incoming = static_cast<char>(Serial.read());
    if (incoming == '\n' || incoming == '\r') {
      if (uartMessage.length() > 0) {
        handleUartMessage(uartMessage);
        uartMessage = "";
      }
    } else if (uartMessage.length() < 40) {
      uartMessage += incoming;
    } else {
      uartMessage = "";
    }
  }
}

void setup() {
  Serial.begin(9600);

  pinMode(CONTINUOUS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(POSITION_BUTTON_PIN, INPUT_PULLUP);
  positionButtonReading = digitalRead(POSITION_BUTTON_PIN);
  positionButtonState = positionButtonReading;

  humanServo.attach(HUMAN_SERVO_PIN);
  continuousServo.attach(CONTINUOUS_SERVO_PIN);
  buttonServo.attach(BUTTON_SERVO_PIN);

  humanServo.write(SERVO_HOME_ANGLE);
  continuousServo.write(CONTINUOUS_STOP);
  buttonServo.write(buttonServoAngle);
}

void loop() {
  readEsp32Uart();

  
  if (digitalRead(CONTINUOUS_BUTTON_PIN) == LOW) {
    continuousServo.write(CONTINUOUS_SPIN);
  } else {
    continuousServo.write(CONTINUOUS_STOP);
  }

  
  const bool reading = digitalRead(POSITION_BUTTON_PIN);
  if (reading != positionButtonReading) {
    positionButtonReading = reading;
    positionButtonChangedAt = millis();
  }
  if (millis() - positionButtonChangedAt >= BUTTON_DEBOUNCE_MS &&
      positionButtonState != positionButtonReading) {
    positionButtonState = positionButtonReading;
    if (positionButtonState == LOW) {
      buttonServoAngle =
          (buttonServoAngle == SERVO_HOME_ANGLE)
              ? SERVO_ACTION_ANGLE
              : SERVO_HOME_ANGLE;
      buttonServo.write(buttonServoAngle);
    }
  }
}
