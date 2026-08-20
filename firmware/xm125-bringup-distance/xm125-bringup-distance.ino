// Shintai-OS — SparkFun Pulsed Coherent Radar Sensor (Acconeer XM125, SEN-24540)
// bench bring-up, DISTANCE mode — the "multiple entities" spike.
//
// Standalone — no other sensors, no BLE, no flash. Requires the module to be
// REFLASHED to the I2C Distance Detector application first (the board ships
// running I2C Presence Detector, which xm125-bringup/ exercises) — see
// specs/zokyo/hitokage.md "Decisions" for the flash procedure (Acconeer
// Exploration Tool / `python -m acconeer.exptool.flash`, BOOT+RST bootloader
// entry on the XM125's OWN USB-C — not the QT Py's).
//
// The Distance Detector reports up to 9 simultaneous reflection PEAKS (each a
// distance + signal strength), unlike Presence's single detected/not-detected
// scalar. This is multiple RANGES, not multiple PEOPLE — two people at the same
// range still look like one peak, and a strong single reflector can produce more
// than one peak (multipath). Treat peak count as an upper bound on entities, not
// a count.
//
//   arduino-cli compile --upload \
//     -b esp32:esp32:adafruit_qtpy_esp32s3_nopsram:CDCOnBoot=cdc,PartitionScheme=tinyuf2,FlashSize=8M \
//     -p /dev/cu.usbmodem1101 firmware/xm125-bringup-distance
//
// SNAG — Wire pins. SDA=41 / SCL=40 on the QT Py ESP32-S3 Qwiic bus, same as
// every other bringup sketch in this repo. Wire.begin(41, 40) FIRST.

#include <Wire.h>
#include <SparkFun_Qwiic_XM125_Arduino_Library.h>

// Matches the current shintai-os.ino diagnostic config exactly, for an
// apples-to-apples isolation test (see specs/zokyo/hitokage.md HK-4).
#define XM125_RANGE_START_MM 500
#define XM125_RANGE_END_MM   5000

// Default threshold method is CFAR, default Threshold Sensitivity = 500
// (0..1000). MEASURED, not assumed: both 900 and 50 made the WALL's own
// detection flicker and its strength collapse (500's clean ~-1000..-2000
// degraded to ~-12000..-15000 at 900, ~-3000..-4700 at 50) — this register's
// polarity isn't the obvious "higher = more sensitive" and two blind guesses
// in either direction both made things worse. Left at the default; testing
// reflector material instead of guessing this knob further.
#define XM125_THRESHOLD_SENSITIVITY 500

SparkFunXM125Distance radar;

void setup() {
  Serial.begin(115200);
  unsigned long w = millis();
  while (!Serial && millis() - w < 1500) delay(10);

  Serial.println("\n=== XM125 Pulsed Coherent Radar — bench bring-up (distance, multi-peak) ===");
  Wire.begin(41, 40);

  // FATAL loops REPEAT the message (not a single print-then-halt): native-USB
  // re-enum takes >1.5s, so a terminal that attaches after the first print would
  // otherwise see nothing forever (bit us live during bring-up).
  if (!radar.begin(SFE_XM125_I2C_ADDRESS, Wire)) {
    while (1) {
      Serial.println("[FATAL] XM125 did not answer @ 0x52 — check Qwiic seating");
      Serial.println("        (run firmware/i2c-scan to confirm it's on the bus)");
      delay(1000);
    }
  }
  Serial.println("[OK] XM125 ready");

  // Manual setup (not the one-shot distanceSetup()) so we can inject the
  // threshold-sensitivity write before APPLY_CONFIGURATION — mirrors
  // distanceSetup()'s own sequence (SparkFun_Qwiic_XM125_Arduino_Library
  // sfDevXM125Distance.cpp) plus Acconeer's Example07_DistanceThresholdSettings.
  radar.setCommand(SFE_XM125_DISTANCE_RESET_MODULE);
  delay(100);
  radar.busyWait();

  bool ok = (radar.setStart(XM125_RANGE_START_MM) == 0);
  delay(100);
  ok = ok && (radar.setEnd(XM125_RANGE_END_MM) == 0);
  delay(100);
  ok = ok && (radar.setThresholdSensitivity(XM125_THRESHOLD_SENSITIVITY) == 0);
  delay(100);
  ok = ok && (radar.setCommand(SFE_XM125_DISTANCE_APPLY_CONFIGURATION) == 0);
  ok = ok && (radar.busyWait() == 0);

  if (!ok) {
    while (1) {
      Serial.println("[FATAL] distance config apply failed — is the DISTANCE firmware flashed?");
      delay(1000);
    }
  }
  Serial.printf("[cfg] distance range %d..%dmm  sensitivity=%d\n",
                XM125_RANGE_START_MM, XM125_RANGE_END_MM, XM125_THRESHOLD_SENSITIVITY);
  Serial.println("Listening…\n");
}

void loop() {
  int32_t rdErr = radar.detectorReadingSetup();

  uint32_t numDistances = 0;
  radar.getNumberDistances(numDistances);

  if (rdErr != 0) {
    uint32_t errStatus = 0, calNeeded = 0, measErr = 0;
    radar.getDetectorErrorStatus(errStatus);
    radar.getCalibrationNeeded(calNeeded);
    radar.getMeasureDistanceError(measErr);
    Serial.printf("[peaks] rdErr=%ld errStat=0x%lx cal=%lu measErr=%lu\n",
                  (long)rdErr, (unsigned long)errStatus, (unsigned long)calNeeded, (unsigned long)measErr);
  } else if (numDistances == 0) {
    Serial.println("[peaks] none");
  } else {
    Serial.printf("[peaks] %lu detected:\n", (unsigned long)numDistances);
    for (uint32_t i = 0; i < numDistances; i++) {
      uint32_t distanceMm = 0;
      int32_t strength = 0;
      if (radar.getPeakDistance(i, distanceMm) != ksfTkErrOk) continue;
      radar.getPeakStrength(i, strength);
      Serial.printf("  peak %lu: %lumm  strength=%ld\n",
                     (unsigned long)i, (unsigned long)distanceMm, (long)strength);
    }
  }

  delay(500);
}
