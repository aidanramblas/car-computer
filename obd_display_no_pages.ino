/*
  OBD-II Serial LCD Display
  ---------------------------------
  Hardware:
    - ELM327 OBD-II adapter connected on hardware Serial (pins 0/1),
      9600 baud. Change ELM_BAUD below if yours runs faster (many
      Bluetooth ELM327s default to 38400).
    - "Simple" serial LCD backpack (Parallax/Sparkfun-style) on
      SoftwareSerial pins 2 (RX, unused) / 3 (TX to LCD).

  Displays (2x16 LCD, split into four 8-char fields):
    Row 1: BAT (Battery Voltage) | ECT (Coolant Temp, F)
    Row 2: IAT (Intake Air Temp, F) | MIL (Check Engine Light on/off)

  Swapping a field:
    Each value is fetched by one call (queryPID or queryBatteryVoltage)
    and written by one lcdPrintField() call in loop(). Change the PID,
    formula, and label in that one block to switch what's shown.
*/

#include <SoftwareSerial.h>

SoftwareSerial lcdSerial(2, 3); // RX (unused), TX to LCD backpack
const long ELM_BAUD = 9600;
const unsigned long ELM_TIMEOUT_MS = 3000;

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

void setup() {
  Serial.begin(ELM_BAUD);
  lcdSerial.begin(9600);

  delay(1000); // let the ELM327 finish powering up before the first command

  // Kept minimal on purpose: matches the exact init sequence confirmed
  // working on this adapter via obd_comms_test.ino. Extra AT commands
  // (ATL0/ATS1/ATH0/ATSP0) were tried and caused every query to fail,
  // likely a protocol-forcing quirk on this ELM327 clone - the
  // defaults already give spaces-on, headers-off responses, so they
  // weren't needed anyway.
  // No fixed delay() before each readELMResponse() call - it already
  // blocks until the adapter's '>' prompt arrives (up to 3s), so a
  // flat sleep first was just adding dead time on top of that.
  sendELMCommand("ATZ");  readELMResponse(); // reset
  sendELMCommand("ATE0"); readELMResponse(); // echo off

  lcdBacklightOn();
  lcdCursorOff();
  lcdClear();
  delay(250);

  lcdPrintField(0, 0, 8, "ECT:--");
  lcdPrintField(0, 8, 8, "BAT:--");
  lcdPrintField(1, 0, 8, "IAT:--");
  lcdPrintField(1, 8, 8, "MIL:--");

  // MIL (check engine light) status, PID 0101, byte A bit 7 = MIL on/off
  long milStatusRaw = queryPID("01", 1);
  if (milStatusRaw >= 0) {
    bool milOn = (milStatusRaw & 0x80) != 0;
    lcdPrintField(1, 8, 8, milOn ? "MIL:ON" : "MIL:OFF");
  }
}

void loop() {
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


  delay(1000);
}
