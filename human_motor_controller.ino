#include <Servo.h>
#include <SoftwareSerial.h>

const byte ESP32_UART_RX_PIN = A0;
const byte ESP32_UART_TX_PIN = A1;
const byte HUMAN_SERVO_PIN = 9;
const byte CONTINUOUS_SERVO_PIN = 10;
const byte BUTTON_SERVO_PIN = 11;
const byte CONTINUOUS_BUTTON_PIN = 2;
const byte POSITION_BUTTON_PIN = 3;
const byte DOORBELL_BUTTON_PIN = 4;
const byte RED_LED_1_PIN = 5;
const byte RED_LED_2_PIN = 6;
const byte BUZZER_PIN = 7;
const byte WATER_PUMP_PIN = 8;
const byte PERSON_STATUS_LED_PIN = 12;

const int SERVO_HOME_ANGLE = 0;
const int SERVO_ACTION_ANGLE = 90;

const int CONTINUOUS_STOP = 90;
const int CONTINUOUS_SPIN = 180;
const unsigned long BUTTON_DEBOUNCE_MS = 30;
const unsigned long DOORBELL_BUZZ_MS = 500;

Servo humanServo;
Servo continuousServo;
Servo buttonServo;
SoftwareSerial espUart(ESP32_UART_RX_PIN, ESP32_UART_TX_PIN);

String uartMessage;
bool discardingUartMessage = false;
int buttonServoAngle = SERVO_HOME_ANGLE;
bool positionButtonReading = HIGH;
bool positionButtonState = HIGH;
unsigned long positionButtonChangedAt = 0;
bool doorbellButtonReading = HIGH;
bool doorbellButtonState = HIGH;
unsigned long doorbellButtonChangedAt = 0;
bool personADetected = false;
bool personBDetected = false;
bool intruderDetected = false;
bool fireDetected = false;
bool doorbellBuzzActive = false;
unsigned long doorbellBuzzStartedAt = 0;

void handleUartMessage(const String& message) {
  if (message == "SIGN:PERSON-A:DETECTED") {
    personADetected = true;
  } else if (message == "SIGN:PERSON-A:CLEAR") {
    personADetected = false;
  } else if (message == "SIGN:PERSON-B:DETECTED") {
    personBDetected = true;
  } else if (message == "SIGN:PERSON-B:CLEAR") {
    personBDetected = false;
  } else if (message == "SIGN:INTRUDER:DETECTED") {
    intruderDetected = true;
  } else if (message == "SIGN:INTRUDER:CLEAR") {
    intruderDetected = false;
  } else if (message == "SIGN:FIRE:DETECTED") {
    fireDetected = true;
  } else if (message == "SIGN:FIRE:CLEAR") {
    fireDetected = false;
  }
}

void readEsp32Uart() {
  while (espUart.available() > 0) {
    const char incoming = static_cast<char>(espUart.read());
    if (incoming == '\n') {
      if (!discardingUartMessage && uartMessage.length() > 0) {
        handleUartMessage(uartMessage);
      }
      uartMessage = "";
      discardingUartMessage = false;
    } else if (incoming != '\r' && !discardingUartMessage &&
               uartMessage.length() < 26) {
      uartMessage += incoming;
    } else if (incoming != '\r') {
      uartMessage = "";
      discardingUartMessage = true;
    }
  }
}

void setup() {
  Serial.begin(115200);
  espUart.begin(9600);

  pinMode(CONTINUOUS_BUTTON_PIN, INPUT_PULLUP);
  pinMode(POSITION_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DOORBELL_BUTTON_PIN, INPUT_PULLUP);
  pinMode(RED_LED_1_PIN, OUTPUT);
  pinMode(RED_LED_2_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(WATER_PUMP_PIN, OUTPUT);
  pinMode(PERSON_STATUS_LED_PIN, OUTPUT);

  positionButtonReading = digitalRead(POSITION_BUTTON_PIN);
  positionButtonState = positionButtonReading;
  doorbellButtonReading = digitalRead(DOORBELL_BUTTON_PIN);
  doorbellButtonState = doorbellButtonReading;

  humanServo.attach(HUMAN_SERVO_PIN);
  continuousServo.attach(CONTINUOUS_SERVO_PIN);
  buttonServo.attach(BUTTON_SERVO_PIN);

  humanServo.write(SERVO_HOME_ANGLE);
  continuousServo.write(CONTINUOUS_STOP);
  buttonServo.write(buttonServoAngle);
  digitalWrite(RED_LED_1_PIN, LOW);
  digitalWrite(RED_LED_2_PIN, LOW);
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(WATER_PUMP_PIN, LOW);
  digitalWrite(PERSON_STATUS_LED_PIN, LOW);
  Serial.println("Arduino ready; ESP32 UART on RX=A0 at 9600 baud.");
}

void loop() {
  readEsp32Uart();

  humanServo.write(
      (personADetected || personBDetected)
          ? SERVO_ACTION_ANGLE
          : SERVO_HOME_ANGLE);

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

  const bool doorbellReading = digitalRead(DOORBELL_BUTTON_PIN);
  if (doorbellReading != doorbellButtonReading) {
    doorbellButtonReading = doorbellReading;
    doorbellButtonChangedAt = millis();
  }
  if (millis() - doorbellButtonChangedAt >= BUTTON_DEBOUNCE_MS &&
      doorbellButtonState != doorbellButtonReading) {
    doorbellButtonState = doorbellButtonReading;
    if (doorbellButtonState == LOW) {
      doorbellBuzzActive = true;
      doorbellBuzzStartedAt = millis();
    }
  }

  if (doorbellBuzzActive &&
      millis() - doorbellBuzzStartedAt >= DOORBELL_BUZZ_MS) {
    doorbellBuzzActive = false;
  }

  digitalWrite(RED_LED_1_PIN, intruderDetected ? HIGH : LOW);
  digitalWrite(RED_LED_2_PIN, intruderDetected ? HIGH : LOW);
  digitalWrite(
      PERSON_STATUS_LED_PIN,
      (personADetected || personBDetected) ? HIGH : LOW);
  digitalWrite(
      BUZZER_PIN,
      (intruderDetected || doorbellBuzzActive) ? HIGH : LOW);
  digitalWrite(WATER_PUMP_PIN, fireDetected ? HIGH : LOW);
}
