// Shintai-OS — Modulino LED Matrix (ABX00152) bench bring-up
//
// Step 2 of bringing up the Modulino matrix: after the I2C scanner confirms the
// module *answers on the bus* (firmware/i2c-scan), this sketch proves the
// Arduino_Modulino LED-matrix driver path actually works on the QT Py ESP32-S3
// by lighting a visible test pattern. Standalone — no sensors, no BLE, no flash.
//
// The Modulino LED Matrix is an 8x12 monochrome panel (96 LEDs), same as the
// UNO R4 WiFi built-in matrix, driven by an on-module MCU over I2C.
//
//   arduino-cli lib install "Arduino_Modulino"     # pulls ArduinoGraphics too
//   arduino-cli compile --upload \
//     -b esp32:esp32:adafruit_qtpy_esp32s3_nopsram:CDCOnBoot=cdc,PartitionScheme=tinyuf2,FlashSize=8M \
//     -p /dev/cu.usbmodem101 firmware/modulino-matrix-bringup
//
// --- Two known snags, both handled here ---
//
// SNAG 1 — Wire pins. Modulino_LED_Matrix::begin() calls _wire->begin() with NO
//   pins. On the QT Py the STEMMA QT bus is SDA=41/SCL=40, so the default
//   Wire.begin() lands on the wrong pins and the matrix never answers. The ESP32
//   core lets a library's hardcoded Wire.begin() still work if the pins are set
//   first: call Wire.setPins(41, 40) BEFORE matrix.begin(). (On a generic
//   ESP32-S3 the library's DEFAULT_WIRE resolves to `Wire`, not `Wire1`, so the
//   default `ModulinoLEDMatrix matrix;` is correct — no explicit-wire ctor needed.)
//
// SNAG 2 — library scope. Arduino_Modulino is written against Arduino's own
//   boards; ESP32-S3 is not its tested path. This sketch is the empirical check
//   that it compiles and runs on the QT Py. If it ever fights the toolchain, the
//   protocol is trivial (a mode string + a 12-byte frame over I2C) and can be
//   spoken with raw Wire calls.
//
// --- Address collision (read before integrating!) ---
//
//   The matrix's default 7-bit I2C address is 0x39 (0x72 is its 8-bit write
//   form). On the Shintai-OS spine 0x39 is ALREADY taken by the APDS9960 gesture
//   sensor (the OLED pane's swipe input), whose address is fixed. On the shared
//   bus the two collide. For this bench bring-up that's fine — only the matrix is
//   plugged in — but before wiring the matrix into the main sketch, readdress it
//   off 0x39 with examples/Utilities/AddressChanger (adapt it to Wire @ 41/40;
//   the LED matrix address is changeable and stored on-module permanently).

#include <Wire.h>
#include "Modulino_LED_Matrix.h"

ModulinoLEDMatrix matrix;   // default: addr 0x39, Wire, MonochromaticVertical

// Full-panel and checkerboard test frames (12 bytes = 96 bits = 8x12 mono).
const uint8_t FRAME_ALL[12]     = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
                                    0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
const uint8_t FRAME_CHECKER[12] = { 0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55,
                                    0xAA, 0x55, 0xAA, 0x55, 0xAA, 0x55 };

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);        // native USB: wait for the host to attach

  Wire.setPins(41, 40);             // SNAG 1: must precede matrix.begin()

  Serial.println("Modulino LED Matrix bring-up — SDA=41 SCL=40, expect @ 0x39");
  if (!matrix.begin()) {            // reads the module; real presence probe
    Serial.println("[FAIL] matrix.begin() — not answering. Check: wiring, that");
    Serial.println("       Wire.setPins(41,40) ran first, and that nothing else");
    Serial.println("       is fighting it at 0x39 (APDS9960 unplugged for bench).");
    while (true) delay(1000);
  }
  Serial.println("[OK] matrix.begin() — Arduino_Modulino runs on the QT Py S3.");
  matrix.clear();
}

void loop() {
  // 1) Walk a single lit pixel across all 96 — proves per-pixel addressing.
  uint8_t frame[12] = {0};
  for (int col = 0; col < 12; col++) {
    for (int bit = 0; bit < 8; bit++) {
      for (int i = 0; i < 12; i++) frame[i] = 0;
      frame[col] = (uint8_t)(1 << bit);
      matrix.setFrame(frame);
      delay(20);
    }
  }
  // 2) Whole panel on, then checkerboard, then clear — legible "it works" blink.
  matrix.setFrame(FRAME_ALL);     delay(500);
  matrix.setFrame(FRAME_CHECKER); delay(500);
  matrix.clear();                 delay(500);
}
