// ==========================================
// Smart Ballot System - Automation Lab 1
// Simulator version (Tinkercad)
// Digital scale -> Force Sensor, RFID card -> IR remote
// I2C LCD -> regular 16x2 LCD (I2C LCD conflicts with IRremote in Tinkercad)
// ==========================================

// ----- Libraries -----
#include <LiquidCrystal.h>
#include <IRremote.h>
// Note: Servo.h is not used - it shares a timer with IRremote,
// so the servo is driven with manual pulses (see updateServo)

// ----- Pin definitions -----
#define BUTTON_PIN 2
#define IR_PIN 3
#define SERVO_PIN 5
#define LED_PIN 7
#define WEIGHT_PIN A0
#define LCD_RS 12
#define LCD_EN 11
#define LCD_D4 10
#define LCD_D5 9
#define LCD_D6 8
#define LCD_D7 6

// ----- Constants -----
#define WEIGHT_ENTER_LEVEL 300   // reading above this = voter inside
#define WEIGHT_EXIT_LEVEL 200    // reading below this = booth empty
#define GATE_CLOSED_ANGLE 0
#define GATE_OPEN_ANGLE 90
#define CARD_TIMEOUT_MS 10000    // time to enter after card scan
#define ALERT_DURATION_MS 3000   // length of the GO! alert
#define BLINK_INTERVAL_MS 300    // LED blink speed during alert
#define SERVO_PERIOD_MS 20       // servo needs a pulse every 20 ms

// ----- System states -----
#define STATE_FREE 0
#define STATE_CARD_OK 1
#define STATE_OCCUPIED 2

// ----- Objects -----
LiquidCrystal lcd(LCD_RS, LCD_EN, LCD_D4, LCD_D5, LCD_D6, LCD_D7);

// ----- Variables -----
int systemState = STATE_FREE;
int voterCount = 0;
int lastButtonState = LOW;
bool alertActive = false;
bool ledBlinkState = false;
unsigned long alertStartTime = 0;
unsigned long lastBlinkTime = 0;
unsigned long cardTime = 0;
int gateAngle = GATE_CLOSED_ANGLE;
unsigned long lastServoPulse = 0;

// Runs once: sets up all components and shows GO
void setup() {
  Serial.begin(9600);
  pinMode(BUTTON_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(WEIGHT_PIN, INPUT);
  pinMode(SERVO_PIN, OUTPUT);
  IrReceiver.begin(IR_PIN);
  lcd.begin(16, 2);
  setFreeState();
  Serial.println("Smart ballot system ready");
}

// Main loop: checks every input, no blocking code
void loop() {
  checkCard();
  checkWeight();
  checkButton();
  checkCardTimeout();
  updateAlert();
  updateServo();
  delay(10);
}

// ===== Functions =====

// Reads the IR receiver - a remote key press = voter card scan
void checkCard() {
  if (!IrReceiver.decode()) {
    return;
  }
  int cardId = IrReceiver.decodedIRData.command;
  IrReceiver.resume();
  if (systemState == STATE_FREE) {
    Serial.print("Card ");
    Serial.print(cardId);
    Serial.println(" accepted - gate open");
    systemState = STATE_CARD_OK;
    cardTime = millis();
    stopAlert();
    setGate(GATE_OPEN_ANGLE);
    showMessage("Card OK", "Please enter");
  } else {
    Serial.print("Card ");
    Serial.print(cardId);
    Serial.println(" rejected - booth not free");
  }
}

// Reads the weight sensor and updates the booth state
void checkWeight() {
  int weight = analogRead(WEIGHT_PIN);
  if (systemState != STATE_OCCUPIED && weight > WEIGHT_ENTER_LEVEL) {
    if (systemState == STATE_FREE) {
      Serial.println("Warning: voter entered without card scan");
    }
    setOccupiedState(weight);
  } else if (systemState == STATE_OCCUPIED && weight < WEIGHT_EXIT_LEVEL) {
    Serial.print("Voter left (weight=");
    Serial.print(weight);
    Serial.println(") - booth free");
    setFreeState();
  }
}

// Staff button: sends a GO! alert when the booth is free
void checkButton() {
  int buttonState = digitalRead(BUTTON_PIN);
  if (buttonState == HIGH && lastButtonState == LOW) {
    if (systemState == STATE_FREE) {
      Serial.println("Staff alert: GO!");
      startAlert();
    } else {
      Serial.println("Staff alert ignored - booth not free");
    }
  }
  lastButtonState = buttonState;
}

// Closes the gate if the voter did not enter in time
void checkCardTimeout() {
  if (systemState == STATE_CARD_OK && millis() - cardTime >= CARD_TIMEOUT_MS) {
    Serial.println("No entry after card scan - gate closed");
    setFreeState();
  }
}

// Booth occupied: LED on, LCD WAIT, count voter, close gate
void setOccupiedState(int weight) {
  systemState = STATE_OCCUPIED;
  stopAlert();
  voterCount++;
  digitalWrite(LED_PIN, HIGH);
  setGate(GATE_CLOSED_ANGLE);
  showStatus("WAIT");
  Serial.print("Voter inside (weight=");
  Serial.print(weight);
  Serial.print(") - total voters: ");
  Serial.println(voterCount);
}

// Booth free: LED off, LCD GO, gate closed
void setFreeState() {
  systemState = STATE_FREE;
  stopAlert();
  digitalWrite(LED_PIN, LOW);
  setGate(GATE_CLOSED_ANGLE);
  showStatus("GO");
}

// Starts the blinking GO! alert
void startAlert() {
  alertActive = true;
  alertStartTime = millis();
  lastBlinkTime = millis();
  ledBlinkState = true;
  digitalWrite(LED_PIN, HIGH);
  showStatus("GO!");
}

// Blinks the LED during the alert and ends it after the set time
void updateAlert() {
  if (!alertActive) {
    return;
  }
  if (millis() - alertStartTime >= ALERT_DURATION_MS) {
    stopAlert();
    showStatus("GO");
    Serial.println("Alert ended");
    return;
  }
  if (millis() - lastBlinkTime >= BLINK_INTERVAL_MS) {
    lastBlinkTime = millis();
    ledBlinkState = !ledBlinkState;
    digitalWrite(LED_PIN, ledBlinkState);
  }
}

// Stops the alert and turns the LED off
void stopAlert() {
  if (alertActive) {
    alertActive = false;
    digitalWrite(LED_PIN, LOW);
  }
}

// Sets the gate (servo) target angle
void setGate(int angle) {
  gateAngle = angle;
}

// Sends one control pulse to the servo every 20 ms
void updateServo() {
  if (millis() - lastServoPulse < SERVO_PERIOD_MS) {
    return;
  }
  lastServoPulse = millis();
  int pulseWidth = map(gateAngle, 0, 180, 544, 2400);
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(pulseWidth);
  digitalWrite(SERVO_PIN, LOW);
}

// Shows booth status on line 1 and voter count on line 2
void showStatus(String status) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Ballot: ");
  lcd.print(status);
  lcd.setCursor(0, 1);
  lcd.print("Voters: ");
  lcd.print(voterCount);
}

// Shows two lines of free text on the LCD
void showMessage(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
