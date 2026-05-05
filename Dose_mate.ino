#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal.h>
#include <ESP32Servo.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <Keypad.h>

// === LCD ===
LiquidCrystal lcd(2, 4, 5, 18, 13, 12);

// === RTC ===
RTC_DS3231 rtc;

// === Buzzer ===
const int buzzerPin = 35;

// === Servos ===
Servo servos[3];
int servoPins[3] = {10, 11, 38};

// === Keypad ===
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {6, 7, 15, 16};
byte colPins[COLS] = {17, 8, 3, 1};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// === Medicine Struct ===
struct Medicine {
  String name;
  int hour;
  int minute;
  int second;
  int pills;
  int servoNum;
  int pillsInTape;
  int dosesPerDay = 1;
  bool triggered = false;
  bool notifiedLowDays = false;
  unsigned long lastNotifyTime = 0;
};
Medicine meds[5];
int medCount = 0;

// === BLE Setup ===
#define SERVICE_UUID "12345678-1234-1234-1234-1234567890ab"
#define CHARACTERISTIC_UUID "abcd1234-5678-90ab-cdef-1234567890ab"
BLECharacteristic* notifyChar;

bool newDataReceived = false;
String receivedData = "";

class MyCallbacks : public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *pCharacteristic) {
    receivedData = String(pCharacteristic->getValue().c_str());
    newDataReceived = true;
  }
};

unsigned long lastDisplayUpdate = 0;

String readFromKeypad(String label, int length) {
  lcd.clear();
  lcd.print(label);
  String input = "";
  while (input.length() < length) {
    char key = keypad.getKey();
    if (key) {
      input += key;
      lcd.setCursor(0, 1);
      lcd.print(input);
    }
  }
  return input;
}

void setup() {
  Serial.begin(115200);
  lcd.begin(16, 2);
  Wire.begin(21, 40);
  rtc.begin();

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  for (int i = 0; i < 3; i++) {
    servos[i].attach(servoPins[i]);
    servos[i].write(0);
  }

  BLEDevice::init("DoseMate-BLE");
  BLEServer *pServer = BLEDevice::createServer();
  BLEService *pService = pServer->createService(SERVICE_UUID);
  notifyChar = pService->createCharacteristic(CHARACTERISTIC_UUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_NOTIFY);
  notifyChar->setCallbacks(new MyCallbacks());
  pService->start();
  BLEDevice::getAdvertising()->addServiceUUID(SERVICE_UUID);
  BLEDevice::getAdvertising()->start();

  lcd.clear();
  lcd.print("DoseMate Smart");
}

void loop() {
  DateTime now = rtc.now();

  if (millis() - lastDisplayUpdate >= 1000) {
    lastDisplayUpdate = millis();
    lcd.setCursor(0, 0);
    lcd.print("DoseMate Smart  ");
    char line2[17];
    snprintf(line2, sizeof(line2), "%02d:%02d:%02d        ", now.hour(), now.minute(), now.second());
    lcd.setCursor(0, 1);
    lcd.print(line2);
  }

  for (int i = 0; i < medCount; i++) {
    if (!meds[i].triggered && now.hour() == meds[i].hour && now.minute() == meds[i].minute && now.second() == meds[i].second) {
      alertMedicine(i);
    }
  }

  if (newDataReceived) {
    parseBLEData(receivedData);
    newDataReceived = false;
  }

  char key = keypad.getKey();
  if (key == '#' && medCount < 3) {
    String timeStr = readFromKeypad("Enter time(HHMMSS)", 6);
    String pillsStr = readFromKeypad("Pills per dose:", 1);
    String servoStr = readFromKeypad("Servo (0-2):", 1);
    String tapeStr = readFromKeypad("Pills in tape:", 2);
    String dosesStr = readFromKeypad("Doses per day:", 1);

    Medicine m;
    m.name = "KeypadMed";
    m.hour = timeStr.substring(0, 2).toInt();
    m.minute = timeStr.substring(2, 4).toInt();
    m.second = timeStr.substring(4, 6).toInt();
    m.pills = pillsStr.toInt();
    m.servoNum = servoStr.toInt();
    m.pillsInTape = tapeStr.toInt();
    m.dosesPerDay = dosesStr.toInt();
    m.triggered = false;
    m.notifiedLowDays = false;
    m.lastNotifyTime = 0;

    meds[medCount++] = m;
    lcd.clear();
    lcd.print("Saved from keypad");
    delay(2000);
  }
}

void alertMedicine(int i) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Take ");
  lcd.print(meds[i].name.substring(0, 10));
  lcd.setCursor(0, 1);
  lcd.print(meds[i].pills);
  lcd.print(" pills");

  String notifyMsg = "Take " + meds[i].name + ": " + String(meds[i].pills) + " pills";
  notifyChar->setValue(notifyMsg.c_str());
  notifyChar->notify();

  digitalWrite(buzzerPin, HIGH);
  servos[meds[i].servoNum].write(180);
  meds[i].lastNotifyTime = millis();

  bool confirmed = false;
  while (!confirmed) {
    char key = keypad.getKey();
    if (key == '3') {
      confirmed = true;
      digitalWrite(buzzerPin, LOW);
      servos[meds[i].servoNum].write(0);
      lcd.clear();
      lcd.print("Confirmed!");
      delay(2000);

      int pillsPerDay = meds[i].pills * meds[i].dosesPerDay;
      if (pillsPerDay > 0) {
        int daysLeft = meds[i].pillsInTape / pillsPerDay;
        if (daysLeft <= 3 && !meds[i].notifiedLowDays) {
          String warn = "Refill " + meds[i].name + " in " + String(daysLeft) + "d";
          notifyChar->setValue(warn.c_str());
          notifyChar->notify();
          meds[i].notifiedLowDays = true;
        }
      }

      meds[i].triggered = true;
      lcd.clear();
      lcd.print("DoseMate Smart");
    }

    if (!confirmed && millis() - meds[i].lastNotifyTime >= 10000) {
      notifyChar->setValue(notifyMsg.c_str());
      notifyChar->notify();
      meds[i].lastNotifyTime = millis();
    }

    delay(100);
  }
}

void parseBLEData(String data) {
  int first = data.indexOf(',');
  int second = data.indexOf(',', first + 1);
  int third = data.indexOf(',', second + 1);
  int fourth = data.indexOf(',', third + 1);
  int fifth = data.indexOf(',', fourth + 1);

  if (first > 0 && second > first && third > second && fourth > third && fifth > fourth) {
    String name = data.substring(0, first);
    String time = data.substring(first + 1, second);
    int pills = data.substring(second + 1, third).toInt();
    int servoNum = data.substring(third + 1, fourth).toInt();
    int pillsInTape = data.substring(fourth + 1, fifth).toInt();
    int dosesPerDay = data.substring(fifth + 1).toInt();

    int hour = time.substring(0, 2).toInt();
    int minute = time.substring(3, 5).toInt();
    int secondVal = time.substring(6).toInt();

    if (medCount < 5 && servoNum >= 0 && servoNum < 3) {
      Medicine m;
      m.name = name;
      m.hour = hour;
      m.minute = minute;
      m.second = secondVal;
      m.pills = pills;
      m.servoNum = servoNum;
      m.pillsInTape = pillsInTape;
      m.dosesPerDay = dosesPerDay;
      m.triggered = false;
      m.notifiedLowDays = false;
      m.lastNotifyTime = 0;

      meds[medCount++] = m;

      lcd.clear();
      lcd.print("Saved via BLE");
      lcd.setCursor(0, 1);
      lcd.print(name.substring(0, 16));
      delay(2000);
      lcd.clear();
      lcd.print("DoseMate Smart");
    }
  }
}