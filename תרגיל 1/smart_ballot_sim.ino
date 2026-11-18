// קלפי חכמה - מעבדת אוטומציה 1 (סימולטור)
// אותה לוגיקה כמו בחומרה. ההבדלים: מסך רגיל, חיישן כוח במקום HX711, סרוו רגיל במקום 360
#include <LiquidCrystal.h>
#include <IRremote.h>

// פינים
#define BTN_PIN 2
#define IR_PIN 3
#define OPEN_BTN_PIN 4
#define SERVO_PIN 5
#define LED_PIN 7
#define FSR_PIN A0

// משקל (0-1023)
#define ENTER_LEVEL 300
#define EXIT_LEVEL 200

// כפתור GO מחובר עם נגד למינוס (לחיצה = HIGH), כפתור המזכיר למינוס בלי נגד
#define BTN_PRESSED HIGH
#define OPEN_PRESSED LOW

// זוויות מחסום (בסימולטור הסרוו רגיל)
#define ANGLE_CLOSED 0
#define ANGLE_OPEN 90

// זמנים
#define CARD_TIMEOUT 10000
#define EXIT_TIME 5000
#define MANUAL_TIME 2000
#define ALERT_TIME 3000
#define BLINK_TIME 300
#define WEIGHT_EVERY 200

// מצבים
#define FREE 0
#define CARD_OK 1
#define BUSY 2

#define GATE_CLOSED 0
#define GATE_OPEN 1

LiquidCrystal lcd(12, 11, 10, 9, 8, 6);

int state = FREE;
int voters = 0;
int lastBtn = !BTN_PRESSED;
int lastOpenBtn = !OPEN_PRESSED;
bool alertOn = false;
bool ledOn = false;
unsigned long alertStart = 0;
unsigned long lastBlink = 0;
unsigned long cardTime = 0;
unsigned long lastWeight = 0;
int weight = 0;
int gateTarget = GATE_CLOSED;
unsigned long lastPulse = 0;
bool exitOpen = false;
bool noCard = false;
unsigned long exitStart = 0;
unsigned long exitDuration = EXIT_TIME;

void setup() {
  Serial.begin(9600);
  pinMode(BTN_PIN, INPUT);
  pinMode(OPEN_BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  pinMode(SERVO_PIN, OUTPUT);

  startLcd();

  IrReceiver.begin(IR_PIN);

  setFree();
  Serial.println("System ready");
}

void loop() {
  readCard();
  readWeight();
  readButton();
  readOpenButton();
  checkTimeout();
  blinkAlert();
  moveGate();
}

// הדלקת המסך
void startLcd() {
  lcd.begin(16, 2);
}

// שלט במקום כרטיס
void readCard() {
  if (!IrReceiver.decode()) return;
  int card = IrReceiver.decodedIRData.command;
  IrReceiver.resume();

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
  lastWeight = millis();
  weight = analogRead(FSR_PIN);

  if (state == CARD_OK && weight > ENTER_LEVEL) {
    setBusy();
  } else if (state == FREE && weight > ENTER_LEVEL && !noCard) {
    noCard = true;
    Serial.println("Warning: entered without card");
    showText("No card!", "Please exit");
  } else if (state == FREE && noCard && weight < EXIT_LEVEL) {
    noCard = false;
    showStatus("GO");
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
  if (btn == OPEN_PRESSED && lastOpenBtn != OPEN_PRESSED) {
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
    showStatus("GO");
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
  showStatus("GO");
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
    else showStatus("GO");
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
  gateTarget = target;
}

// פולס לסרוו כל 20 מילישניות (בלי ספריית סרוו כי היא מתנגשת עם השלט בטינקרקד)
void moveGate() {
  if (millis() - lastPulse < 20) return;
  lastPulse = millis();
  int angle = gateTarget == GATE_OPEN ? ANGLE_OPEN : ANGLE_CLOSED;
  int pulse = map(angle, 0, 180, 544, 2400);
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(pulse);
  digitalWrite(SERVO_PIN, LOW);
}

// מסך רגיל
void showStatus(String s) {
  showText("Ballot: " + s, "Voters: " + String(voters));
}

// כתיבה למסך
void showText(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
