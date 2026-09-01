# OBD-II LCD Display

Arduino Uno + ELM327-compatible OBD-II board (SparkFun OBD-II UART) + serial
LCD backpack (Parallax/Sparkfun-style, 16x2), with two buttons for extra
screens.

## Hardware pins
- OBD board: hardware `Serial` (pins 0/1), 9600 baud
- LCD backpack: `SoftwareSerial` on pins 2 (RX, unused) / 3 (TX)
- Upper button: pins 10 (virtual ground) / 11 (read)
- Lower button: pins 8 (virtual ground) / 9 (read)

Buttons use a "virtual ground" trick: one pin is held `LOW` in software,
the other reads `INPUT_PULLUP` and goes `LOW` when pressed.

## Screens
- **Home**: ECT, BAT, IAT, MIL (MIL refreshes on load, not every second to reduce initial startup time)
- **Page 2** (upper button): STFT, engine load, LTFT, throttle %
- **DTC codes** (lower button, any screen): shows stored trouble codes for
  5 seconds, then returns to whichever screen you were on.

## Files
- `obd_display.ino` - main sketch, flash this for normal use
- `lcd_test.ino` - LCD-only bench test, no OBD/car needed
- `obd_comms_test.ino` - OBD link test, shows raw ELM327 responses on screen

## Notes
- The 3-position switch on the board must be **up** (toward the OBD
  board) for the display to get real data - down routes serial to the
  FTDI programming board instead.
- Setup intentionally skips `ATL0`/`ATS1`/`ATH0`/`ATSP0` - these AT
  commands broke every query on this particular ELM327 clone. Only
  `ATZ` + `ATE0` are sent at boot.

