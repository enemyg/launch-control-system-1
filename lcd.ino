#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <EEPROM.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ------- Pins -------
#define BUZZER1 A3
#define BUZZER2 8
#define RELAY   5          // Relay IN on D5
#define START_BUTTON 2     // Toggle switch, active LOW

// Green LEDs only
#define LED_G1 3   // Standby / Safe / Error / Nav Align
#define LED_G2 6   // Countdown / Arm Prep / Launch active
#define LED_G3 9   // Mission success / Loop mode indication

// ------------ Global states ------------
const String correctPassword = "1234";
String enteredPassword = "";
bool authenticated = false;

int missionCount = 0;
bool stopLoop = false;
bool aborted = false;

int countdownTime = 10;
int dist0 = 8, dist1 = 4, dist2 = 2, dist3 = 0;

// Custom Icons for LCD
byte missileChar[8]   = {B00100,B01110,B11111,B00100,B01110,B11111,B10101,B00100};
byte radarChar[8]     = {B00000,B00100,B01110,B10101,B00100,B00100,B00000,B00000};
byte targetChar[8]    = {B00000,B00100,B01110,B11111,B01110,B00100,B00000,B00000};
byte explosionChar[8] = {B00000,B10101,B01110,B11111,B01110,B10101,B00000,B00000};
byte successChar[8]   = {B00000,B00001,B00010,B10100,B01000,B00000,B00000,B00000};
byte blockChar[8]     = {B11111,B11111,B11111,B11111,B11111,B11111,B11111,B11111};

// ---------- BUZZER HELPERS ----------
void beepBoth(int freq, int durationMs) {
  long period = 1000000L / freq;
  long cycles = (long)durationMs * 1000L / period;
  for (long i = 0; i < cycles; i++) {
    digitalWrite(BUZZER1, HIGH);
    digitalWrite(BUZZER2, HIGH);
    delayMicroseconds(period / 2);
    digitalWrite(BUZZER1, LOW);
    digitalWrite(BUZZER2, LOW);
    delayMicroseconds(period / 2);
  }
}
void shortBeep()             { beepBoth(2000, 150); }
void doubleBeep()            { shortBeep(); delay(120); shortBeep(); }
void radarBeep()             { beepBoth(1600, 150); }
void continuousBeep(int t)   { beepBoth(2500, t); }
void buzzersOff()            { digitalWrite(BUZZER1, LOW); digitalWrite(BUZZER2, LOW); }

// ---------- LED HELPERS ----------
void allLedsOff() {
  analogWrite(LED_G1, 0);
  analogWrite(LED_G2, 0);
  analogWrite(LED_G3, 0);
}

// “Safe mode / error” flashing using LED_G1
void safeModeFlashGreen() {
  for (int i = 0; i < 6; i++) {
    analogWrite(LED_G1, 255);
    delay(150);
    analogWrite(LED_G1, 0);
    delay(150);
  }
}

// Success effect using LED_G3
void successPulseGreen() {
  for (int k = 0; k < 3; k++) {
    for (int b = 0; b <= 255; b += 15) {
      analogWrite(LED_G3, b);
      delay(15);
    }
    for (int b = 255; b >= 0; b -= 15) {
      analogWrite(LED_G3, b);
      delay(15);
    }
  }
  analogWrite(LED_G3, 0);
}

// ---------- PROGRESS BAR ----------
void progressBar(int msTotal) {
  lcd.setCursor(0, 1);
  for (int i = 0; i < 16; i++) {
    lcd.write(byte(5));
    delay(msTotal / 16);
  }
}

// ---------- EEPROM HELPERS ----------
void loadMissionCount() {
  EEPROM.get(10, missionCount);
  if (missionCount < 0 || missionCount > 10000) missionCount = 0;
}
void saveMissionCount() {
  EEPROM.put(10, missionCount);
}

// ---------- MULTI-PRESS READER ----------
int getPressCount(unsigned long windowMs) {
  int count = 0;
  bool prev = digitalRead(START_BUTTON);
  unsigned long start = millis();

  while (millis() - start < windowMs) {
    bool state = digitalRead(START_BUTTON);
    if (prev == HIGH && state == LOW) {
      count++;
      delay(150);
    }
    prev = state;
  }
  return count;
}

// ---------- SINGLE DIGIT ENTRY ----------
int getSingleDigit(unsigned long waitTime = 2000) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ENTER DIGIT...");
  lcd.setCursor(0, 1);
  lcd.print("Press 1-4 times");

  int count = 0;
  bool prev = HIGH;
  unsigned long start = millis();

  while (millis() - start < waitTime) {
    bool state = digitalRead(START_BUTTON);
    if (prev == HIGH && state == LOW) {
      count++;
      delay(200);
    }
    prev = state;
  }

  if (count >= 1 && count <= 4) return count;
  return -1;
}

// ---------- PASSWORD ENTRY ----------
void enterPassword() {
  enteredPassword = "";
  authenticated = false;
  unsigned long lastActivity = millis();

  allLedsOff();
  analogWrite(LED_G1, 50);   // faint green while entering

  while ((int)enteredPassword.length() < 4) {
    int digit = getSingleDigit();
    if (digit != -1) {
      enteredPassword += String(digit);
      lastActivity = millis();
      shortBeep();

      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("ENTER PASS:");
      lcd.setCursor(0, 1);
      lcd.print(enteredPassword);
      for (int i = enteredPassword.length(); i < 4; i++) lcd.print("_");
      delay(800);
    }

    if (millis() - lastActivity > 10000) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("TIMEOUT!");
      safeModeFlashGreen();
      delay(1500);
      authenticated = false;
      return;
    }
  }

  if (enteredPassword == correctPassword) {
    authenticated = true;
    lcd.clear();
    lcd.print("ACCESS GRANTED");
    allLedsOff();
    analogWrite(LED_G3, 200);
    doubleBeep();
    delay(1500);
    analogWrite(LED_G3, 0);
  } else {
    authenticated = false;
    lcd.clear();
    lcd.print("ACCESS DENIED");
    safeModeFlashGreen();
    continuousBeep(1500);
    delay(1500);
  }
}

// ---------- STANDBY ----------
void standbyMode() {
  allLedsOff();
  analogWrite(LED_G1, 80);  // standby green dim

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("SYSTEM STANDBY");
  lcd.setCursor(0, 1);
  lcd.print("RUNS: ");
  lcd.print(missionCount);

  while (digitalRead(START_BUTTON) == HIGH) {
    // wait for press
  }
  delay(300);
  shortBeep();
}

// ---------- HEALTH CHECK ----------
void healthCheck() {
  allLedsOff();
  analogWrite(LED_G1, 120);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("HEALTH CHECK");
  lcd.setCursor(0,1);
  lcd.print("TESTING BUZZER");
  shortBeep();
  delay(1000);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("HEALTH CHECK");
  lcd.setCursor(0,1);
  lcd.print("TESTING RELAY");
  digitalWrite(RELAY, HIGH);
  delay(400);
  digitalWrite(RELAY, LOW);
  shortBeep();
  delay(1000);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("ALL SYSTEMS");
  lcd.setCursor(0,1);
  lcd.print("OK");
  lcd.write(byte(4));
  doubleBeep();
  delay(1500);

  allLedsOff();
}

// ---------- BOOT & PREP STAGES ----------
void stageInitialBoot() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SYSTEM BOOTING");
  lcd.setCursor(0,1);
  lcd.print("SELF CHECK...");
  shortBeep();
  progressBar(2000);
}

void stageCommsLink() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("LINK TO GCS ");
  lcd.write(byte(1));
  lcd.setCursor(0,1);
  lcd.print("COMMS STABLE");
  radarBeep();
  delay(2000);
}

void stageSystemReady() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("LAUNCH CONTROL");
  lcd.setCursor(0,1);
  lcd.print("SYSTEM READY ");
  lcd.write(byte(0));
  doubleBeep();
  delay(2000);
}

void stageNavAlign() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("NAV ALIGNMENT");
  lcd.setCursor(0,1);
  lcd.print("IN PROGRESS...");
  progressBar(3000);
  radarBeep();
  delay(2000);
}

// ---------- ARM PREP (7 seconds, green + relay) ----------
void stageArmPrep() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("ARM PREP IN");
  lcd.setCursor(0, 1);
  lcd.print("PROGRESS ");
  lcd.write(byte(0));

  for (int i = 0; i < 7; i++) {
    analogWrite(LED_G1, 255);
    analogWrite(LED_G2, 255);
    analogWrite(LED_G3, 255);
    digitalWrite(RELAY, HIGH);

    shortBeep();
    delay(500);

    analogWrite(LED_G1, 0);
    analogWrite(LED_G2, 0);
    analogWrite(LED_G3, 0);
    digitalWrite(RELAY, LOW);

    delay(500);
  }

  digitalWrite(RELAY, LOW);
  allLedsOff();
}

void stageSafetyCheck() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("SAFETY SYSTEM");
  lcd.setCursor(0,1);
  lcd.print("VERIFIED ");
  lcd.write(byte(4));
  doubleBeep();
  delay(2000);
}

// ---------- COUNTDOWN ----------
bool stageCountdown() {
  aborted = false;
  for (int t = countdownTime; t >= 1; t--) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("COUNTDOWN: ");
    lcd.print(t);
    lcd.setCursor(0,1);
    lcd.print("PRESS TO ABORT");

    // Green LED2 as warning
    if (t > 5) analogWrite(LED_G2, 180);
    else       analogWrite(LED_G2, 255);

    unsigned long start = millis();
    while (millis() - start < 900) {
      if (digitalRead(START_BUTTON) == LOW) {
        aborted = true;
        break;
      }
    }

    analogWrite(LED_G2, 0);

    if (aborted) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("LAUNCH ABORTED");
      lcd.setCursor(0,1);
      lcd.print("SAFE MODE");
      continuousBeep(1500);
      safeModeFlashGreen();
      delay(1500);
      return false;
    }

    if (t > 5) shortBeep();
    else doubleBeep();
  }
  return true;
}

// ---------- LAUNCH & FLIGHT ----------
void stageLaunch() {
  lcd.clear();
  lcd.setCursor(1,0);
  lcd.print("LAUNCH ACTIVE");
  lcd.setCursor(0,1);
  lcd.print("IGNITION ");
  lcd.write(byte(0));

  analogWrite(LED_G2, 255);
  digitalWrite(RELAY, HIGH);
  continuousBeep(4000);
  digitalWrite(RELAY, LOW);
  analogWrite(LED_G2, 0);
}

void stageMidAirFlight() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("MID-COURSE");
  lcd.setCursor(0,1);
  lcd.print("FLIGHT STABLE");
  for (int i = 0; i < 5; i++) {
    radarBeep();
    delay(600);
  }
}

void stageTargetSearch() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("TARGET SEARCH ");
  lcd.write(byte(1));
  lcd.setCursor(0,1);
  lcd.print("SCANNING AREA");
  for (int i = 0; i < 5; i++) {
    radarBeep();
    delay(500);
  }
}

void stageTargetLocking() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("TARGET LOCKING");
  lcd.setCursor(0,1);
  lcd.print("ACQ SIGNAL ");
  lcd.write(byte(1));
  for (int i = 0; i < 4; i++) {
    radarBeep();
    delay(400);
  }
}

void stageTargetLocked() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("TARGET LOCKED ");
  lcd.write(byte(2));
  lcd.setCursor(0,1);
  lcd.print("READY TO ATTACK");
  for (int i = 0; i < 3; i++) {
    doubleBeep();
    delay(300);
  }
}

// ---------- TRAJECTORY ----------
void stageMissileTrajectoryAnimation() {
  String frames[4] = {
    "R----->-------->",
    "--->R--------->",
    "--->--->R----->",
    "--------->--->R"
  };
  int distances[4]  = {dist0, dist1, dist2, dist3};
  int signalBars[4] = {4, 6, 8, 10};

  for (int i = 0; i < 4; i++) {
    lcd.clear();
    lcd.setCursor(0,0);
    if (distances[i] > 0) lcd.print("TRACKING...");
    else                  lcd.print("APPROACHING TGT");

    lcd.setCursor(0,1);
    for (int c = 0; c < 16 && c < (int)frames[i].length(); c++) {
      if (frames[i][c] == 'R') lcd.write(byte(0));
      else lcd.print(frames[i][c]);
    }
    delay(800);

    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("DIST: ");
    lcd.print(distances[i]);
    lcd.print("km");

    lcd.setCursor(0,1);
    lcd.print("SIG: ");
    for (int s = 0; s < signalBars[i]; s++) lcd.write(byte(5));
    radarBeep();
    delay(1200);
  }
  buzzersOff();
}

// ---------- IMPACT & SUCCESS ----------
void stageImpact() {
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("TARGET IMPACT ");
  lcd.write(byte(3));
  lcd.setCursor(0,1);
  lcd.print("AMBUSHED");
  continuousBeep(700);
  delay(200);
  continuousBeep(700);
}

void stageMissionSuccess() {
  missionCount++;
  saveMissionCount();

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("MISSION SUCCESS");
  lcd.setCursor(0,1);
  lcd.print("TOTAL RUNS: ");
  lcd.print(missionCount);

  successPulseGreen();
  for (int i = 0; i < 4; i++) {
    shortBeep();
    delay(250);
  }
  buzzersOff();
}

// ---------- RUN ONE MISSION ----------
void runMission() {
  aborted = false;
  stageInitialBoot();
  stageCommsLink();
  stageSystemReady();
  stageNavAlign();
  stageArmPrep();
  stageSafetyCheck();
  if (!stageCountdown()) return;
  stageLaunch();
  stageMidAirFlight();
  stageTargetSearch();
  stageTargetLocking();
  stageTargetLocked();
  stageMissileTrajectoryAnimation();
  stageImpact();
  stageMissionSuccess();
}

// ---------- POST-MISSION MENU ----------
void postMissionMenu() {
  int mode = 0; // 0 = Restart, 1 = Loop

  while (true) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("MODE: ");
    if (mode == 0) lcd.print("RESTART");
    else           lcd.print("LOOP");
    lcd.setCursor(0,1);
    lcd.print("3X=CHANGE 2X=OK");

    int presses = getPressCount(2500);

    if (presses == 3) {
      mode = !mode;
      shortBeep();
    } else if (presses == 2) {
      doubleBeep();
      break;
    } else {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("WAITING FOR");
      lcd.setCursor(0,1);
      lcd.print("USER SELECTION");
      delay(800);
    }
  }

  if (mode == 0) {
    allLedsOff();
    analogWrite(LED_G1, 80);
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("RESTART MODE");
    lcd.setCursor(0,1);
    lcd.print("PRESS START");
    delay(2000);
    return;
  }

  // LOOP MODE
  stopLoop = false;
  while (!stopLoop) {
    runMission();

    for (int t = 10; t >= 1; t--) {
      lcd.clear();
      lcd.setCursor(0,0);
      lcd.print("LOOP MODE ACTIVE");
      lcd.setCursor(0,1);
      lcd.print("STOP IN 10s: ");
      lcd.print(t);

      shortBeep();
      // Indicate loop mode using LED_G3
      analogWrite(LED_G3, 200);
      unsigned long start = millis();
      while (millis() - start < 400) {
        if (digitalRead(START_BUTTON) == LOW) {
          stopLoop = true;
          break;
        }
      }
      analogWrite(LED_G3, 0);

      unsigned long start2 = millis();
      while (!stopLoop && millis() - start2 < 500) {
        if (digitalRead(START_BUTTON) == LOW) {
          stopLoop = true;
          break;
        }
      }
      if (stopLoop) break;
    }
  }

  allLedsOff();
  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("LOOP HALTED");
  lcd.setCursor(0,1);
  lcd.print("RETURNING...");
  delay(1600);
}

// ---------- SETUP & LOOP ----------
void setup() {
  lcd.init();
  lcd.backlight();

  pinMode(BUZZER1, OUTPUT);
  pinMode(BUZZER2, OUTPUT);
  pinMode(RELAY, OUTPUT);
  pinMode(START_BUTTON, INPUT_PULLUP);

  pinMode(LED_G1, OUTPUT);
  pinMode(LED_G2, OUTPUT);
  pinMode(LED_G3, OUTPUT);

  digitalWrite(RELAY, LOW);
  buzzersOff();
  allLedsOff();

  lcd.createChar(0, missileChar);
  lcd.createChar(1, radarChar);
  lcd.createChar(2, targetChar);
  lcd.createChar(3, explosionChar);
  lcd.createChar(4, successChar);
  lcd.createChar(5, blockChar);

  loadMissionCount();
}

void loop() {
  standbyMode();
  enterPassword();
  if (!authenticated) return;
  healthCheck();
  runMission();
  postMissionMenu();
}