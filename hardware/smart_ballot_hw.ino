// מערכת קלפי חכמה - מעבדת אוטומציה 1
// גרסת חומרה (ארדואינו אמיתי)
// רכיבים: מקלט שלט IR, חיישן משקל עם HX711, מסך LCD עם I2C, מנוע סרוו, נורה וכפתור

// ספריות להתקנה (Library Manager):
// IRremote של shirriff / ArminJo
// HX711 של Bogdan Necula
// LiquidCrystal I2C של Frank de Brabander
// Servo של Arduino
#include <Wire.h>
#include <IRremote.hpp>
#include <HX711.h>
#include <LiquidCrystal_I2C.h>
#include <Servo.h>

// פינים
#define BTN_PIN 2
#define IR_PIN 3
#define SERVO_PIN 5
#define LED_PIN 7
#define SCALE_SCK A2
#define SCALE_DT A3
// המסך מחובר ל-A4 (SDA) ול-A5 (SCL)

// כיול המשקל - לשנות אחרי בדיקה עם משקל ידוע
#define CAL_FACTOR 420.0
// ספי משקל בגרמים
#define ENTER_LEVEL 500
#define EXIT_LEVEL 300

// הכפתור מחובר בין פין 2 למינוס, עם הנגד הפנימי של הארדואינו - לחיצה = LOW
#define BTN_PRESSED LOW

// המנוע הוא 360 מעלות: 90 = עצירה, מעל 90 לכיוון אחד, מתחת לכיוון השני
#define SERVO_STOP 90
#define SERVO_OPEN 120
#define SERVO_CLOSE 60
#define GATE_MOVE_TIME 600

// זמנים במילישניות
#define CARD_TIMEOUT 10000
#define EXIT_TIME 5000
#define ALERT_TIME 3000
#define BLINK_TIME 300
#define WEIGHT_EVERY 200

// מצבי הקלפי
#define FREE 0
#define CARD_OK 1
#define BUSY 2

// מצבי המחסום
#define GATE_CLOSED 0
#define GATE_OPEN 1

HX711 scale;
LiquidCrystal_I2C* lcd;
Servo gate;

int state = FREE;
int voters = 0;
int lastBtn = !BTN_PRESSED;
bool alertOn = false;
bool ledOn = false;
unsigned long alertStart = 0;
unsigned long lastBlink = 0;
unsigned long cardTime = 0;
unsigned long lastWeight = 0;
float weight = 0;
int gatePos = GATE_CLOSED;
int gateTarget = GATE_CLOSED;
unsigned long gateStart = 0;
bool exitOpen = false;
unsigned long exitStart = 0;

void setup() {
  Serial.begin(9600);
  pinMode(BTN_PIN, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);

  startLcd();

  IrReceiver.begin(IR_PIN);

  scale.begin(SCALE_DT, SCALE_SCK);
  scale.set_scale(CAL_FACTOR);
  scale.tare();  // איפוס - לא לעמוד על המשקל בהדלקה

  gate.attach(SERVO_PIN);
  gate.write(SERVO_STOP);

  setFree();
  Serial.println("System ready");
}

void loop() {
  readCard();
  readWeight();
  readButton();
  checkTimeout();
  blinkAlert();
  moveGate();
}

// מוצא את כתובת המסך (0x27 או 0x3F) ומדליק אותו
void startLcd() {
  Wire.begin();
  byte addr = 0x27;
  Wire.beginTransmission(0x27);
  if (Wire.endTransmission() != 0) addr = 0x3F;
  lcd = new LiquidCrystal_I2C(addr, 16, 2);
  lcd->init();
  lcd->backlight();
  Serial.print("LCD address: 0x");
  Serial.println(addr, HEX);
}

// קורא לחיצה בשלט (במקום כרטיס) ופותח את המחסום אם הקלפי פנויה
void readCard() {
  if (!IrReceiver.decode()) return;
  int card = IrReceiver.decodedIRData.command;
  bool noise = IrReceiver.decodedIRData.protocol == UNKNOWN;
  bool repeat = IrReceiver.decodedIRData.flags & IRDATA_FLAGS_IS_REPEAT;
  IrReceiver.resume();
  if (noise || repeat) return;  // מתעלמים מרעש ומלחיצה ארוכה

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

// בודק את המשקל כל 200 מילישניות ומעדכן אם מישהו נכנס או יצא
void readWeight() {
  if (millis() - lastWeight < WEIGHT_EVERY) return;
  if (!scale.is_ready()) return;
  lastWeight = millis();
  weight = scale.get_units(1);

  if (state != BUSY && weight > ENTER_LEVEL) {
    if (state == FREE) Serial.println("Warning: entered without card");
    setBusy();
  } else if (state == BUSY && weight < EXIT_LEVEL) {
    Serial.print("Voter left, weight=");
    Serial.println(weight);
    setFree();
    openForExit();
  }
}

// כפתור הצוות - מפעיל התראה רק כשהקלפי פנויה
void readButton() {
  int btn = digitalRead(BTN_PIN);
  if (btn == BTN_PRESSED && lastBtn != BTN_PRESSED) {
    if (state == FREE) {
      Serial.println("Alert: GO!");
      startAlert();
    } else {
      Serial.println("Alert ignored - booth busy");
    }
  }
  lastBtn = btn;
}

// סוגר את המחסום אם עבר הזמן ואף אחד לא נכנס, או אחרי שהמצביע יצא
void checkTimeout() {
  if (state == CARD_OK && millis() - cardTime >= CARD_TIMEOUT) {
    Serial.println("Timeout - gate closed");
    setFree();
  }
  if (exitOpen && millis() - exitStart >= EXIT_TIME) {
    exitOpen = false;
    Serial.println("Exit done - gate closed");
    setGate(GATE_CLOSED);
    showStatus("GO");
  }
}

// פותח את המחסום ליציאה, והוא נסגר לבד אחרי 5 שניות
void openForExit() {
  exitOpen = true;
  exitStart = millis();
  setGate(GATE_OPEN);
  showText("Thank you!", "Please exit");
  Serial.println("Gate open for exit");
}

// מישהו בפנים
void setBusy() {
  state = BUSY;
  exitOpen = false;
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

// הקלפי פנויה
void setFree() {
  state = FREE;
  stopAlert();
  digitalWrite(LED_PIN, LOW);
  setGate(GATE_CLOSED);
  showStatus("GO");
}

// מתחיל הבהוב ו-GO! על המסך
void startAlert() {
  alertOn = true;
  alertStart = millis();
  lastBlink = millis();
  ledOn = true;
  digitalWrite(LED_PIN, HIGH);
  showText("GO!", "Next voter");
}

// מהבהב בלי לעצור את התוכנית, ונגמר אחרי 3 שניות
void blinkAlert() {
  if (!alertOn) return;
  if (millis() - alertStart >= ALERT_TIME) {
    stopAlert();
    showStatus("GO");
    Serial.println("Alert ended");
    return;
  }
  if (millis() - lastBlink >= BLINK_TIME) {
    lastBlink = millis();
    ledOn = !ledOn;
    digitalWrite(LED_PIN, ledOn);
  }
}

// עוצר הבהוב
void stopAlert() {
  if (!alertOn) return;
  alertOn = false;
  ledOn = false;
  digitalWrite(LED_PIN, LOW);
}

// מתחיל להזיז את המחסום לפתוח או סגור
void setGate(int target) {
  if (target == gatePos) return;
  gateTarget = target;
  gateStart = millis();
  gate.write(target == GATE_OPEN ? SERVO_OPEN : SERVO_CLOSE);
}

// עוצר את המנוע כשהמחסום הגיע
void moveGate() {
  if (gatePos == gateTarget) return;
  if (millis() - gateStart >= GATE_MOVE_TIME) {
    gate.write(SERVO_STOP);
    gatePos = gateTarget;
  }
}

// מסך רגיל: מצב הקלפי ומספר המצביעים
void showStatus(String s) {
  showText("Ballot: " + s, "Voters: " + String(voters));
}

// כותב שתי שורות למסך
void showText(String line1, String line2) {
  lcd->clear();
  lcd->setCursor(0, 0);
  lcd->print(line1);
  lcd->setCursor(0, 1);
  lcd->print(line2);
}
