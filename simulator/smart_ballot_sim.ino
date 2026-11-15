// מערכת קלפי חכמה - מעבדת אוטומציה 1
// גרסת סימולטור (Tinkercad)
// בסימולטור: חיישן כוח במקום משקל, שלט IR במקום RFID, מסך LCD רגיל במקום I2C

#include <LiquidCrystal.h>
#include <IRremote.h>
// לא משתמשים ב-Servo.h כי הוא מתנגש בטיימר עם IRremote

// פינים
#define BTN_PIN 2
#define IR_PIN 3
#define SERVO_PIN 5
#define LED_PIN 7
#define FSR_PIN A0

// ספי משקל (ערך אנלוגי 0-1023)
#define ENTER_LEVEL 300
#define EXIT_LEVEL 200

// זוויות מחסום
#define GATE_CLOSED 0
#define GATE_OPEN 90

// זמנים במילישניות
#define CARD_TIMEOUT 10000
#define ALERT_TIME 3000
#define BLINK_TIME 300

// מצבי הקלפי
#define FREE 0
#define CARD_OK 1
#define BUSY 2

LiquidCrystal lcd(12, 11, 10, 9, 8, 6);

int state = FREE;
int voters = 0;
int lastBtn = LOW;
bool alertOn = false;
bool ledOn = false;
unsigned long alertStart = 0;
unsigned long lastBlink = 0;
unsigned long cardTime = 0;
int gateAngle = GATE_CLOSED;
unsigned long lastPulse = 0;

// אתחול רכיבים
void setup() {
  Serial.begin(9600);
  pinMode(BTN_PIN, INPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(FSR_PIN, INPUT);
  pinMode(SERVO_PIN, OUTPUT);
  IrReceiver.begin(IR_PIN);
  lcd.begin(16, 2);
  setFree();
  Serial.println("System ready");
}

// לולאה ראשית - רק קריאה לפונקציות
void loop() {
  readCard();
  readWeight();
  readButton();
  checkTimeout();
  blinkAlert();
  moveServo();
  delay(10);
}

// קריאת כרטיס בוחר מהשלט
void readCard() {
  if (!IrReceiver.decode()) {
    return;
  }
  int card = IrReceiver.decodedIRData.command;
  IrReceiver.resume();

  if (state == FREE) {
    Serial.print("Card ");
    Serial.print(card);
    Serial.println(" OK - gate open");
    state = CARD_OK;
    cardTime = millis();
    stopAlert();
    gateAngle = GATE_OPEN;
    showText("Card OK", "Please enter");
  } else {
    Serial.print("Card ");
    Serial.print(card);
    Serial.println(" rejected - booth busy");
  }
}

// בדיקת משקל - האם יש בוחר בקלפי
void readWeight() {
  int w = analogRead(FSR_PIN);

  if (state != BUSY && w > ENTER_LEVEL) {
    if (state == FREE) {
      Serial.println("Warning: entered without card");
    }
    setBusy(w);
  } else if (state == BUSY && w < EXIT_LEVEL) {
    Serial.print("Voter left, weight=");
    Serial.println(w);
    setFree();
  }
}

// כפתור נציג - התראת GO! כשהקלפי פנויה
void readButton() {
  int btn = digitalRead(BTN_PIN);

  if (btn == HIGH && lastBtn == LOW) {
    if (state == FREE) {
      Serial.println("Alert: GO!");
      startAlert();
    } else {
      Serial.println("Alert ignored - booth busy");
    }
  }
  lastBtn = btn;
}

// אם הבוחר לא נכנס בזמן - סוגרים את המחסום
void checkTimeout() {
  if (state == CARD_OK && millis() - cardTime >= CARD_TIMEOUT) {
    Serial.println("Timeout - gate closed");
    setFree();
  }
}

// קלפי תפוסה: נורה דולקת, WAIT, מונה עולה
void setBusy(int w) {
  state = BUSY;
  stopAlert();
  voters++;
  digitalWrite(LED_PIN, HIGH);
  gateAngle = GATE_CLOSED;
  showStatus("WAIT");
  Serial.print("Voter inside, weight=");
  Serial.print(w);
  Serial.print(", voters=");
  Serial.println(voters);
}

// קלפי פנויה: נורה כבויה, GO
void setFree() {
  state = FREE;
  stopAlert();
  digitalWrite(LED_PIN, LOW);
  gateAngle = GATE_CLOSED;
  showStatus("GO");
}

// התחלת התראה מהבהבת
void startAlert() {
  alertOn = true;
  alertStart = millis();
  lastBlink = millis();
  ledOn = true;
  digitalWrite(LED_PIN, HIGH);
  showStatus("GO!");
}

// הבהוב הנורה בזמן ההתראה
void blinkAlert() {
  if (!alertOn) {
    return;
  }
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

// עצירת ההתראה
void stopAlert() {
  if (alertOn) {
    alertOn = false;
    digitalWrite(LED_PIN, LOW);
  }
}

// פולס לסרוו כל 20 מילישניות
void moveServo() {
  if (millis() - lastPulse < 20) {
    return;
  }
  lastPulse = millis();
  int pulse = map(gateAngle, 0, 180, 544, 2400);
  digitalWrite(SERVO_PIN, HIGH);
  delayMicroseconds(pulse);
  digitalWrite(SERVO_PIN, LOW);
}

// מצב הקלפי בשורה 1, מספר בוחרים בשורה 2
void showStatus(String s) {
  lcd.clear();
  lcd.print("Ballot: ");
  lcd.print(s);
  lcd.setCursor(0, 1);
  lcd.print("Voters: ");
  lcd.print(voters);
}

// הצגת שתי שורות טקסט
void showText(String line1, String line2) {
  lcd.clear();
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}
