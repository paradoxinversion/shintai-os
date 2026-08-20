// Shintai-OS — SparkFun Pulsed Coherent Radar Sensor (Acconeer XM125, SEN-24540)
// bench bring-up, PRESENCE mode.
//
// Standalone — no other sensors, no BLE, no flash. Gets a live presence
// distance readout onto the serial console ASAP, before any integration
// into the main sketch or CONTRACT.md.
//
// The board ships with the "I2C Presence Detector" application already
// flashed (distance ranging is a SEPARATE onboard firmware image —
// i2c_distance_detector.bin — that has to be reflashed over the module's
// bootloader to switch modes; there is no runtime command to toggle between
// them). This sketch only exercises presence mode.
//
//   arduino-cli lib install "SparkFun XM125 Arduino Library"
//   arduino-cli compile --upload \
//     -b esp32:esp32:adafruit_qtpy_esp32s3_nopsram:CDCOnBoot=cdc,PartitionScheme=tinyuf2,FlashSize=8M \
//     -p /dev/cu.usbmodem101 firmware/xm125-bringup
//
// SNAG — Wire pins. On the QT Py ESP32-S3 the STEMMA QT / Qwiic bus is
//   SDA=41 / SCL=40 (same as the main firmware and every other bringup
//   sketch in this repo). The library's begin() does NOT set pins, so call
//   Wire.begin(41, 40) FIRST or the sensor never answers.
//
// Default I2C address is 0x52 (SFE_XM125_I2C_ADDRESS) — no collision with
// anything currently on the spine (see firmware/i2c-scan's KNOWN[] map).

#include <Wire.h>
#include <SparkFun_Qwiic_XM125_Arduino_Library.h>

#define XM125_RANGE_START_MM 200
#define XM125_RANGE_END_MM   2000

SparkFunXM125Presence radar;

unsigned long lastBeat = 0;

void setup() {
  Serial.begin(115200);
  unsigned long w = millis();
  while (!Serial && millis() - w < 1500) delay(10);   // boot even untethered

  Serial.println("\n=== XM125 Pulsed Coherent Radar — bench bring-up (presence) ===");
  Wire.begin(41, 40);                                  // QT Py Qwiic bus FIRST

  // FATAL loops REPEAT the message (not a single print-then-halt): native-USB
  // re-enum takes >1.5s, so a terminal that attaches after the first print would
  // otherwise see nothing forever.
  if (!radar.begin(SFE_XM125_I2C_ADDRESS, Wire)) {
    while (1) {
      Serial.println("[FATAL] XM125 did not answer @ 0x52 — check Qwiic seating");
      Serial.println("        (run firmware/i2c-scan to confirm it's on the bus)");
      delay(1000);
    }
  }
  Serial.println("[OK] XM125 ready");

  int32_t err = radar.detectorStart(XM125_RANGE_START_MM, XM125_RANGE_END_MM);
  if (err != 0) {
    while (1) {
      Serial.printf("[FATAL] detectorStart failed, err=%ld\n", (long)err);
      delay(1000);
    }
  }
  Serial.printf("[cfg] presence range %d..%dmm\n", XM125_RANGE_START_MM, XM125_RANGE_END_MM);
  Serial.println("Listening…\n");
}

void loop() {
  radar.busyWait();

  uint32_t distanceMm = 0;
  int32_t err = radar.getDistanceValuemm(distanceMm);

  if (err == 0) {
    if (distanceMm > 0) {
      Serial.printf("[presence] YES  distance=%lumm (%.1fcm)\n",
                     (unsigned long)distanceMm, distanceMm * 0.1f);
    } else {
      Serial.println("[presence] no");
    }
  } else {
    Serial.printf("[err] getDistanceValuemm failed, err=%ld\n", (long)err);
  }

  if (millis() - lastBeat > 10000) {
    lastBeat = millis();
    Serial.println("[heartbeat] still listening…");
  }

  delay(300);
}
