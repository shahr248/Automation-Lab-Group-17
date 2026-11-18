// קלפי חכמה - מעבדת אוטומציה 1
#include <Wire.h>
#include <IRremote.hpp>
#include <HX711.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// פינים
#define BTN_PIN 2
#define OPEN_BTN_PIN 4
#define IR_PIN 3
#define SERVO_PIN 5
#define LED_PIN 7
#define SCALE_SCK A2
#define SCALE_DT A3
#define TRIG_PIN 8
#define ECHO_PIN 9

// משקל
#define CAL_FACTOR 420.0
#define ENTER_LEVEL 500
#define EXIT_LEVEL 300

#define BTN_PRESSED LOW

// חיישן מרחק בכניסה (ס"מ)
#define NEAR_CM 50
#define FAR_CM 70

// סרוו 360 - 90 זה עצירה
#define SERVO_STOP 90
#define SERVO_OPEN 120
#define SERVO_CLOSE 60
#define GATE_MOVE_TIME 600
#define RAMP_TIME 200  // האצה הדרגתית כדי שהמנוע לא יפיל את המתח

// זמנים
#define CARD_TIMEOUT 10000
#define EXIT_TIME 5000
#define MANUAL_TIME 2000
#define ALERT_TIME 3000
#define BLINK_TIME 300
#define WEIGHT_EVERY 200
#define DIST_EVERY 250

// מצבים
#define FREE 0
#define CARD_OK 1
#define BUSY 2

#define GATE_CLOSED 0
#define GATE_OPEN 1

HX711 scale;
LiquidCrystal_I2C* lcd;
Servo gate;

int state = FREE;
int voters = 0;
int lastBtn = !BTN_PRESSED;
int lastOpenBtn = !BTN_PRESSED;
bool alertOn = false;
bool ledOn = false;
unsigned long alertStart = 0;
unsigned long lastBlink = 0;
unsigned long cardTime = 0;
unsigned long lastWeight = 0;
float weight = 0;
int gateTarget = GATE_CLOSED;
unsigned long gateStart = 0;
unsigned long moveTime = GATE_MOVE_TIME;
bool moving = false;
bool exitOpen = false;
bool noCard = false;
unsigned long exitStart = 0;
unsigned long exitDuration = EXIT_TIME;
bool nearDoor = false;
String shown1 = "";
String shown2 = "";
unsigned long lastDist = 0;

void setup() {
  Serial.begin(9600);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(OPEN_BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  startLcd();

  IrReceiver.begin(IR_PIN);

  scale.begin(SCALE_DT, SCALE_SCK);
  scale.set_scale(CAL_FACTOR);
  scale.tare();

  gate.attach(SERVO_PIN);
  gate.write(SERVO_STOP);

  setFree();
  Serial.println("System ready");
}

void loop() {
  readCard();
  readWeight();
  readDistance();
  readButton();
  readOpenButton();
  checkTimeout();
  blinkAlert();
  moveGate();
}

// הדלקת המסך
void startLcd() {
  Wire.begin();
  Wire.setWireTimeout(3000, true);  // שלא ייתקע אם המסך לא עונה
  byte addr = 0x27;
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() != 0) addr = 0x3F;
  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();
  lcd->backlight();
  Serial.print("LCD address: 0x");
  Serial.println(addr, HEX);
}

// שלט במקום כרטיס
void readCard() {
  if (!IrReceiver.decode()) return;
  int card = IrReceiver.decodedIRData.command;
  bool noise = IrReceiver.decodedIRData.protocol == UNKNOWN;
  bool repeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
  IrReceiver.resume();
  if (noise || repeat) return;

  if (state == FREE) {
    Serial.println("Card " + String(card) + " OK - gate open");
    state = CARD_OK;
    cardTime = millis();
    exitOpen = false;
    stopAlert();
    setGate(GATE_OPEN);
    showText("Card OK", "Please enter");
  } else {
    Serial.println("Card " + String(card) + " rejected - booth busy");
  }
}

// בדיקת משקל - נספר רק מי שהעביר כרטיס
void readWeight() {
  if (millis() - lastWeight < WEIGHT_EVERY) return;
  if (!scale.is_ready()) return;
  lastWeight = millis();
  weight = scale.get_units(1);

  if (state == CARD_OK && weight > ENTER_LEVEL) {
    setBusy();
  } else if (state == FREE && weight > ENTER_LEVEL && !noCard) {
    noCard = true;
    Serial.println("Warning: entered without card");
    showText("No card!", "Please exit");
  } else if (state == FREE && noCard && weight < EXIT_LEVEL) {
    noCard = false;
    showIdle();
  } else if (state == BUSY && weight < EXIT_LEVEL) {
    Serial.print("Voter left, weight=");
    Serial.println(weight);
    setFree();
    openForExit(EXIT_TIME);
    showText("Thank you!", "Please exit");
  }
}

// כפתור צוות - עובד כל עוד אין מישהו בפנים
void readButton() {
  int btn = digitalRead(BTN_PIN);
  if (btn == BTN_PRESSED && lastBtn != BTN_PRESSED) {
    if (state != BUSY) {
      Serial.println("Alert: GO!");
      if (state == CARD_OK) cardTime = millis();  // עוד זמן להיכנס
      startAlert();
    } else {
      Serial.println("Alert ignored - booth busy");
    }
  }
  lastBtn = btn;
}

// כפתור מזכיר - פותח את המחסום ל-2 שניות
void readOpenButton() {
  int btn = digitalRead(OPEN_BTN_PIN);
  if (btn == BTN_PRESSED && lastOpenBtn != BTN_PRESSED) {
    Serial.println("Manual open");
    openForExit(MANUAL_TIME);
    showText("Gate open", "Please exit");
  }
  lastOpenBtn = btn;
}

// סגירת מחסום אחרי זמן
void checkTimeout() {
  if (state == CARD_OK && millis() - cardTime >= CARD_TIMEOUT) {
    Serial.println("Timeout - gate closed");
    setFree();
  }
  if (exitOpen && millis() - exitStart >= exitDuration) {
    exitOpen = false;
    Serial.println("Exit done - gate closed");
    setGate(GATE_CLOSED);
    showIdle();
  }
}

// פתיחה ליציאה
void openForExit(unsigned long duration) {
  exitOpen = true;
  exitStart = millis();
  exitDuration = duration;
  setGate(GATE_OPEN);
  Serial.println("Gate open for exit");
}

// קלפי תפוסה
void setBusy() {
  state = BUSY;
  exitOpen = false;
  noCard = false;
  stopAlert();
  voters++;
  digitalWrite(LED_PIN, HIGH);
  setGate(GATE_CLOSED);
  showStatus("WAIT");
  Serial.print("Voter inside, weight=");
  Serial.print(weight);
  Serial.print(" voters=");
  Serial.println(voters);
}

// קלפי פנויה
void setFree() {
  state = FREE;
  stopAlert();
  digitalWrite(LED_PIN, LOW);
  setGate(GATE_CLOSED);
  showIdle();
}

// התראה
void startAlert() {
  alertOn = true;
  alertStart = millis();
  lastBlink = millis();
  ledOn = true;
  digitalWrite(LED_PIN, HIGH);
  showText("GO!", state == CARD_OK ? "Please enter" : "Next voter");
}

// הבהוב
void blinkAlert() {
  if (!alertOn) return;
  if (millis() - alertStart >= ALERT_TIME) {
    stopAlert();
    if (state == CARD_OK) showText("Card OK", "Please enter");
    else showIdle();
    Serial.println("Alert ended");
    return;
  }
  if (millis() - lastBlink >= BLINK_TIME) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn);
  }
}

// סיום הבהוב
void stopAlert() {
  if (!alertOn) return;
  alertOn = false;
  ledOn = false;
  digitalWrite(LED_PIN, LOW);
}

// הזזת מחסום
void setGate(int target) {
  if (target == gateTarget) return;
  // אם המחסום באמצע תנועה, חוזרים רק את הדרך שכבר עבר
  if (moving) moveTime = millis() - gateStart;
  else moveTime = GATE_MOVE_TIME + RAMP_TIME / 2;
  gateTarget = target;
  gateStart = millis();
  moving = true;
  gate.write(SERVO_STOP);
}

// תנועת המנוע ועצירה
void moveGate() {
  if (!moving) return;
  unsigned long t = millis() - gateStart;
  if (t >= moveTime) {
    gate.write(SERVO_STOP);
    moving = false;
    refreshLcd();  // המנוע יכול לשבש את המסך, מציירים מחדש
    return;
  }
  int full = gateTarget == GATE_OPEN ? SERVO_OPEN : SERVO_CLOSE;
  if (t < RAMP_TIME) gate.write(SERVO_STOP + (full - SERVO_STOP) * (long)t / RAMP_TIME);
  else gate.write(full);
}

// חיישן מרחק - בוחר שעומד בכניסה
void readDistance() {
  if (millis() - lastDist < DIST_EVERY) return;
  lastDist = millis();
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  unsigned long t = pulseIn(ECHO_PIN, HIGH, 25000);
  int cm = (t == 0) ? 999 : t / 58;  // אין הד = אין אף אחד

  if (!nearDoor && cm < NEAR_CM) {
    nearDoor = true;
    Serial.println("Voter at door, distance=" + String(cm) + "cm");
    if (idle()) showIdle();
  } else if (nearDoor && cm > FAR_CM) {
    nearDoor = false;
    Serial.println("Door clear");
    if (idle()) showIdle();
  }
}

// אין כרגע הודעה אחרת על המסך
bool idle() {
  return state == FREE && !noCard && !alertOn && !exitOpen;
}

// מסך המתנה - אם מישהו בכניסה מבקשים כרטיס
void showIdle() {
  if (nearDoor) showText("Welcome!", "Scan your card");
  else showStatus("GO");
}

// מסך רגיל
void showStatus(String s) {
  showText("Ballot: " + s, "Voters: " + String(voters));
}

// כתיבה למסך
void showText(String line1, String line2) {
  shown1 = line1;
  shown2 = line2;
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print(line1);
  lcd->setCursor(0, 1);
  lcd->print(line2);
}

// אתחול המסך מחדש וכתיבת ההודעה האחרונה
void refreshLcd() {
  lcd->init();
  lcd->backlight();
  showText(shown1, shown2);
}
