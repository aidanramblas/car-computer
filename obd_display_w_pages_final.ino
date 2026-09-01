/*
  OBD-II Serial LCD Display
  ---------------------------------
  Hardware:
    - ELM327 OBD-II adapter connected on hardware Serial (pins 0/1),
      9600 baud. Change ELM_BAUD below if yours runs faster (many
      Bluetooth ELM327s default to 38400).
    - "Simple" serial LCD backpack (Parallax/Sparkfun-style) on
      SoftwareSerial pins 2 (RX, unused) / 3 (TX to LCD).
    - Two buttons, each wired across two digital pins rather than to
      a shared ground: one pin is held LOW in software as a "virtual
      ground," the other is read with INPUT_PULLUP and goes LOW when
      pressed.
        Upper button: pins ~10 (virtual ground) / ~11 (read)
        Lower button: pins ~8  (virtual ground) / ~9  (read)

  Main screen (2x16 LCD, four 8-char fields):
    Row 1: ECT (Coolant Temp, F) | BAT (Battery Voltage)
    Row 2: IAT (Intake Air Temp, F) | MIL (Check Engine Light, checked once at boot)

  Upper button: flip to Page 2 (STFT / Load / LTFT / Throttle) and back.
  Lower button: show stored trouble codes (DTCs) for a few seconds, then
                return to whichever screen you were on.

  Swapping a field:
    Each value is fetched by one call (queryPID or queryBatteryVoltage)
    and written by one lcdPrintField() call. Change the PID, formula,
    and label in that one block to switch what's shown.
*/

#include <SoftwareSerial.h>

SoftwareSerial lcdSerial(2, 3); // RX (unused), TX to LCD backpack
const long ELM_BAUD = 9600;
const unsigned long ELM_TIMEOUT_MS = 3000;

// ---------- Button pins ----------
const int BTN_UPPER_GND  = 10; // driven LOW, acts as virtual ground
const int BTN_UPPER_READ = 11; // INPUT_PULLUP, LOW = pressed
const int BTN_LOWER_GND  = 8;
const int BTN_LOWER_READ = 9;
const unsigned long DEBOUNCE_MS = 50;

bool upperLastReading = HIGH;
bool upperStable = HIGH;
unsigned long upperLastChange = 0;

bool lowerLastReading = HIGH;
bool lowerStable = HIGH;
unsigned long lowerLastChange = 0;

// ---------- Screen / timing state ----------
enum ScreenMode { SCREEN_MAIN, SCREEN_PAGE2 };
ScreenMode currentScreen = SCREEN_MAIN;

bool showingDTC = false;
unsigned long dtcShownAt = 0;
const unsigned long DTC_DISPLAY_MS = 5000; // how long the DTC screen stays up

unsigned long lastPollTime = 0;
const unsigned long POLL_INTERVAL_MS = 1000;

String milText = "MIL:--"; // set once in setup(), remembered across screen switches

// ---------- LCD backpack control (Parallax/Sparkfun serial LCD protocol) ----------

void lcdClear()        { lcdSerial.write(12); delay(5); }
void lcdBacklightOn()  { lcdSerial.write(17); }
void lcdCursorOff()    { lcdSerial.write(22); }

// row: 0 or 1, col: 0-15
void lcdSetCursor(uint8_t row, uint8_t col) {
  uint8_t base = (row == 0) ? 128 : 148;
  lcdSerial.write(base + col);
}

// Blank out a field before writing so old digits never linger.
void lcdPrintField(uint8_t row, uint8_t col, uint8_t width, const String &text) {
  lcdSetCursor(row, col);
  for (uint8_t i = 0; i < width; i++) lcdSerial.write(' ');
  lcdSetCursor(row, col);
  lcdSerial.print(text.substring(0, width)); // truncate if it somehow overflows
}

// ---------- ELM327 comms ----------

void sendELMCommand(const String &cmd) {
  while (Serial.available()) Serial.read(); // flush stale bytes
  Serial.print(cmd);
  Serial.print('\r');
}

// Reads until the '>' prompt (end of ELM327 response) or timeout.
// Collapses CR/LF into single spaces so responses tokenize cleanly.
String readELMResponse() {
  String resp = "";
  unsigned long start = millis();
  while (millis() - start < ELM_TIMEOUT_MS) {
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '>') break;
      if (c == '\r' || c == '\n') {
        if (resp.length() && resp[resp.length() - 1] != ' ') resp += ' ';
        continue;
      }
      resp += c;
    }
  }
  resp.trim();
  return resp;
}

// Sends "01<pid>", parses `numDataBytes` hex data bytes after the "41 <pid>"
// header, and returns them combined big-endian. Returns -1 on failure
// (PID not supported, adapter not responding, no link, etc.).
long queryPID(const String &pid, uint8_t numDataBytes) {
  sendELMCommand("01" + pid);
  String resp = readELMResponse();

  String header = "41 " + pid;
  int idx = resp.indexOf(header);
  if (idx == -1) return -1;

  String remainder = resp.substring(idx + header.length());
  remainder.trim();

  long value = 0;
  for (uint8_t i = 0; i < numDataBytes; i++) {
    remainder.trim();
    int sp = remainder.indexOf(' ');
    String byteStr = (sp == -1) ? remainder : remainder.substring(0, sp);
    if (byteStr.length() == 0) return -1;
    value = (value << 8) | strtol(byteStr.c_str(), NULL, 16);
    if (sp == -1) break;
    remainder = remainder.substring(sp + 1);
  }
  return value;
}

// Battery voltage uses the ELM327's own AT command rather than a PID,
// so it works even before/without a running engine.
float queryBatteryVoltage() {
  sendELMCommand("ATRV");
  String resp = readELMResponse();
  resp.replace("V", "");
  resp.trim();
  return resp.toFloat();
}

// Sends Mode 03 (read stored DTCs) and decodes up to 2 codes into a
// string like "P0133 P0171". Returns "NO CODES" if none are stored.
String queryDTCs() {
  sendELMCommand("03");
  String resp = readELMResponse();

  int idx = resp.indexOf("43");
  if (idx == -1) return "NO CODES";

  String remainder = resp.substring(idx + 2);
  remainder.trim();

  String codes = "";
  int foundCount = 0;
  while (foundCount < 2 && remainder.length() > 0) {
    int sp1 = remainder.indexOf(' ');
    if (sp1 == -1) break;
    String byteA = remainder.substring(0, sp1);
    remainder = remainder.substring(sp1 + 1);
    remainder.trim();

    int sp2 = remainder.indexOf(' ');
    String byteB = (sp2 == -1) ? remainder : remainder.substring(0, sp2);
    remainder = (sp2 == -1) ? "" : remainder.substring(sp2 + 1);
    remainder.trim();

    uint8_t a = (uint8_t)strtol(byteA.c_str(), NULL, 16);
    uint8_t b = (uint8_t)strtol(byteB.c_str(), NULL, 16);
    if (a == 0 && b == 0) continue; // empty slot, no code stored here

    char prefix;
    switch ((a >> 6) & 0x03) {
      case 0: prefix = 'P'; break;
      case 1: prefix = 'C'; break;
      case 2: prefix = 'B'; break;
      default: prefix = 'U'; break;
    }
    char code[7];
    snprintf(code, sizeof(code), "%c%d%X%02X", prefix, (a >> 4) & 0x03, a & 0x0F, b);
    if (codes.length() > 0) codes += " ";
    codes += code;
    foundCount++;
  }

  return codes.length() > 0 ? codes : "NO CODES";
}

// ---------- Button handling ----------

// Returns true exactly once, on the frame a debounced press is detected.
bool checkButtonPressed(int readPin, bool &lastReading, bool &stable, unsigned long &lastChange) {
  bool reading = digitalRead(readPin);
  if (reading != lastReading) {
    lastChange = millis();
    lastReading = reading;
  }
  bool pressedEdge = false;
  if ((millis() - lastChange) > DEBOUNCE_MS && reading != stable) {
    stable = reading;
    if (stable == LOW) pressedEdge = true; // LOW = pressed, since INPUT_PULLUP
  }
  return pressedEdge;
}

// ---------- Screen draw / poll functions ----------

// Queries MIL and updates milText, then writes it to the field. Called
// whenever the main screen loads (boot, switching back from Page 2, or
// returning from the DTC screen) - NOT on every regular poll cycle, so
// it stays cheap while still refreshing more often than "once ever."
void refreshMIL() {
  long milStatusRaw = queryPID("01", 1);
  if (milStatusRaw >= 0) {
    bool milOn = (milStatusRaw & 0x80) != 0;
    milText = milOn ? "MIL:ON" : "MIL:OFF";
  }
  lcdPrintField(1, 8, 8, milText);
}

void drawMainPlaceholders() {
  lcdClear();
  lcdPrintField(0, 0, 8, "ECT:--");
  lcdPrintField(0, 8, 8, "BAT:--");
  lcdPrintField(1, 0, 8, "IAT:--");
  lcdPrintField(1, 8, 8, "MIL:--");
  refreshMIL();
}

void pollMainScreen() {
  // Coolant temp, PID 0105, 1 byte, C = A - 40
  long ectRaw = queryPID("05", 1);
  if (ectRaw >= 0) {
    int ectF = (int)round((ectRaw - 40) * 9.0 / 5.0 + 32);
    lcdPrintField(0, 0, 8, "ECT:" + String(ectF));
  }

  // Battery voltage (dropping the "V" unit; text is a full 8 chars,
  // so this sits flush at the right edge with nothing to bump into)
  float batt = queryBatteryVoltage();
  if (batt > 0) {
    lcdPrintField(0, 8, 8, "BAT:" + String(batt, 1));
  }

  // Intake air temp, PID 010F, 1 byte, C = A - 40
  long iatRaw = queryPID("0F", 1);
  if (iatRaw >= 0) {
    int iatF = (int)round((iatRaw - 40) * 9.0 / 5.0 + 32);
    lcdPrintField(1, 0, 8, "IAT:" + String(iatF));
  }

  // MIL is checked once at boot, not here - see setup().
}

void drawPage2Placeholders() {
  lcdClear();
  lcdPrintField(0, 0, 8, "STFT:--");
  lcdPrintField(0, 8, 8, "LOAD:--");
  lcdPrintField(1, 0, 8, "LTFT:--");
  lcdPrintField(1, 8, 8, "THR:--");
}

void pollPage2Screen() {
  // Short term fuel trim (bank 1), PID 0106, 1 byte, % = (A - 128) * 100 / 128
  long stftRaw = queryPID("06", 1);
  if (stftRaw >= 0) {
    int stftPct = (int)round((stftRaw - 128) * 100.0 / 128.0);
    lcdPrintField(0, 0, 8, "STFT:" + String(stftPct));
  }

  // Calculated engine load, PID 0104, 1 byte, % = A * 100 / 255
  long loadRaw = queryPID("04", 1);
  if (loadRaw >= 0) {
    int loadPct = (int)round(loadRaw * 100.0 / 255.0);
    lcdPrintField(0, 8, 8, "LOAD:" + String(loadPct));
  }

  // Long term fuel trim (bank 1), PID 0107, 1 byte, % = (A - 128) * 100 / 128
  long ltftRaw = queryPID("07", 1);
  if (ltftRaw >= 0) {
    int ltftPct = (int)round((ltftRaw - 128) * 100.0 / 128.0);
    lcdPrintField(1, 0, 8, "LTFT:" + String(ltftPct));
  }

  // Throttle position, PID 0111, 1 byte, % = A * 100 / 255
  long throttleRaw = queryPID("11", 1);
  if (throttleRaw >= 0) {
    int throttlePct = (int)round(throttleRaw * 100.0 / 255.0);
    lcdPrintField(1, 8, 8, "THR:" + String(throttlePct));
  }
}

void showDTCScreen() {
  lcdClear();
  String codes = queryDTCs();
  lcdPrintField(0, 0, 16, "DTC CODES:");
  lcdPrintField(1, 0, 16, codes);
}

void redrawCurrentScreen() {
  if (currentScreen == SCREEN_MAIN) {
    drawMainPlaceholders();
  } else {
    drawPage2Placeholders();
  }
}

void setup() {
  Serial.begin(ELM_BAUD);
  lcdSerial.begin(9600);

  pinMode(BTN_UPPER_GND, OUTPUT);
  digitalWrite(BTN_UPPER_GND, LOW);
  pinMode(BTN_UPPER_READ, INPUT_PULLUP);

  pinMode(BTN_LOWER_GND, OUTPUT);
  digitalWrite(BTN_LOWER_GND, LOW);
  pinMode(BTN_LOWER_READ, INPUT_PULLUP);

  delay(1000); // let the ELM327 finish powering up before the first command

  // Kept minimal on purpose: matches the exact init sequence confirmed
  // working on this adapter - extra AT commands (ATL0/ATS1/ATH0/ATSP0)
  // caused every query to fail on this ELM327 clone, and the defaults
  // already give spaces-on, headers-off responses.
  sendELMCommand("ATZ");  readELMResponse(); // reset
  sendELMCommand("ATE0"); readELMResponse(); // echo off

  lcdBacklightOn();
  lcdCursorOff();
  drawMainPlaceholders(); // also queries + displays MIL, see refreshMIL()
  delay(250);

  lastPollTime = millis();
}

void loop() {
  // --- Button checks happen every loop iteration, not gated by polling ---
  if (checkButtonPressed(BTN_UPPER_READ, upperLastReading, upperStable, upperLastChange)) {
    currentScreen = (currentScreen == SCREEN_MAIN) ? SCREEN_PAGE2 : SCREEN_MAIN;
    showingDTC = false;
    redrawCurrentScreen();
    lastPollTime = millis() - POLL_INTERVAL_MS; // poll immediately on switch
  }

  if (checkButtonPressed(BTN_LOWER_READ, lowerLastReading, lowerStable, lowerLastChange)) {
    showingDTC = true;
    dtcShownAt = millis();
    showDTCScreen();
  }

  // --- DTC screen auto-return ---
  if (showingDTC && (millis() - dtcShownAt > DTC_DISPLAY_MS)) {
    showingDTC = false;
    redrawCurrentScreen();
    lastPollTime = millis() - POLL_INTERVAL_MS; // poll immediately on return
  }

  // --- Regular PID polling, only when not showing the DTC screen ---
  if (!showingDTC && (millis() - lastPollTime >= POLL_INTERVAL_MS)) {
    lastPollTime = millis();
    if (currentScreen == SCREEN_MAIN) {
      pollMainScreen();
    } else {
      pollPage2Screen();
    }
  }
}
