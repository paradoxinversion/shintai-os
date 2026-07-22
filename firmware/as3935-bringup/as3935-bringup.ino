// Shintai-OS — SparkFun AS3935 "Franklin" lightning detector bench bring-up
//
// Standalone — no other sensors, no BLE, no flash. Gets a live lightning storm
// onto the serial console ASAP, then we integrate it as a Zōkyō afterward.
//
// The AS3935 signals every event (noise / disturber / lightning) on its INT pin.
// Here the INT pin is NOT wired, so we POLL the interrupt-source register
// (REG0x03) on a fast loop instead — a non-zero read means an event just latched.
// Reading the register clears it, so poll faster than events arrive (100 Hz here).
//
//   arduino-cli lib install "SparkFun AS3935 Lightning Detector Arduino Library"
//   arduino-cli compile --upload \
//     -b esp32:esp32:adafruit_qtpy_esp32s3_nopsram:CDCOnBoot=cdc,PartitionScheme=tinyuf2,FlashSize=8M \
//     -p /dev/cu.usbmodem101 firmware/as3935-bringup
//
// SNAG — Wire pins. On the QT Py ESP32-S3 the STEMMA QT / Qwiic bus is
//   SDA=41 / SCL=40 (same as the main firmware). The library's begin() does NOT
//   set pins, so call Wire.begin(41, 40) FIRST or the sensor never answers.
//
// Live tuning over serial (no reflash — useful mid-storm):
//   i / o : INDOOR (sensitive, default) / OUTDOOR (less gain, fewer false trips)
//   m     : toggle disturber masking (masked = quieter, but can hide weak strikes)
//   n / N : noise floor down / up   (1..7; raise if you see constant "Noise")
//   d / D : watchdog threshold down / up (1..10; raise to reject "Disturber")
//   c     : clear the accumulated strike statistics
//   ?     : print current settings + running counts

#include <Wire.h>
#include <SparkFun_AS3935.h>

#define AS3935_ADDR   0x03   // SparkFun default (ADD0+ADD1 high); jumpers -> 0x02 / 0x01
#define INDOOR        0x12
#define OUTDOOR       0x0E
#define LIGHTNING_INT 0x08
#define DISTURBER_INT 0x04
#define NOISE_INT     0x01

SparkFun_AS3935 lightning(AS3935_ADDR);

// Tunables (mirror the chip's power-on defaults so '?' reads true from boot).
int  noiseFloor = 2;    // 1..7
int  watchdog   = 2;    // 1..10
bool outdoor    = false;
bool maskDist   = false;

// Running tallies + heartbeat.
unsigned long strikes = 0, disturbers = 0, noises = 0;
unsigned long lastBeat = 0;

void printSettings() {
  Serial.printf("[cfg] mode=%s  noiseFloor=%d  watchdog=%d  maskDisturber=%s\n",
                outdoor ? "OUTDOOR" : "INDOOR", noiseFloor, watchdog,
                maskDist ? "on" : "off");
  Serial.printf("[cnt] strikes=%lu  disturbers=%lu  noise=%lu\n",
                strikes, disturbers, noises);
}

void setup() {
  Serial.begin(115200);
  unsigned long w = millis();
  while (!Serial && millis() - w < 1500) delay(10);   // boot even untethered

  Serial.println("\n=== AS3935 Franklin lightning detector — bench bring-up ===");
  Wire.begin(41, 40);                                  // QT Py Qwiic bus FIRST

  if (!lightning.begin(Wire)) {
    Serial.println("[FATAL] AS3935 did not answer @ 0x03 — check Qwiic seating / address jumpers");
    Serial.println("        (run firmware/i2c-scan to confirm it's on the bus)");
    while (1) delay(1000);
  }
  Serial.println("[OK] AS3935 ready");

  // Sensible bring-up config: INDOOR gain (most sensitive), show disturbers so we
  // can see the sensor reacting to the storm even before a clean strike lands.
  lightning.setIndoorOutdoor(outdoor ? OUTDOOR : INDOOR);
  lightning.setNoiseLevel(noiseFloor);
  lightning.watchdogThreshold(watchdog);
  lightning.maskDisturber(maskDist);
  printSettings();
  Serial.println("Listening… (polling; press '?' for cfg)\n");
}

void loop() {
  // ── Live tuning over serial ────────────────────────────────────────────────
  while (Serial.available()) {
    char c = Serial.read();
    switch (c) {
      case 'i': outdoor = false; lightning.setIndoorOutdoor(INDOOR);  Serial.println("-> INDOOR");  break;
      case 'o': outdoor = true;  lightning.setIndoorOutdoor(OUTDOOR); Serial.println("-> OUTDOOR"); break;
      case 'm': maskDist = !maskDist; lightning.maskDisturber(maskDist);
                Serial.printf("-> maskDisturber %s\n", maskDist ? "on" : "off"); break;
      case 'n': if (noiseFloor > 1) noiseFloor--; lightning.setNoiseLevel(noiseFloor);
                Serial.printf("-> noiseFloor %d\n", noiseFloor); break;
      case 'N': if (noiseFloor < 7) noiseFloor++; lightning.setNoiseLevel(noiseFloor);
                Serial.printf("-> noiseFloor %d\n", noiseFloor); break;
      case 'd': if (watchdog > 1)  watchdog--;  lightning.watchdogThreshold(watchdog);
                Serial.printf("-> watchdog %d\n", watchdog); break;
      case 'D': if (watchdog < 10) watchdog++;  lightning.watchdogThreshold(watchdog);
                Serial.printf("-> watchdog %d\n", watchdog); break;
      case 'c': lightning.clearStatistics(true); Serial.println("-> statistics cleared"); break;
      case '?': printSettings(); break;
      default: break;
    }
  }

  // ── Poll the interrupt-source register (REG0x03) ───────────────────────────
  uint8_t intVal = lightning.readInterruptReg();
  if (intVal == LIGHTNING_INT) {
    strikes++;
    uint8_t  km  = lightning.distanceToStorm();   // 1 = overhead, 63 = out of range
    uint32_t eng = lightning.lightningEnergy();    // raw energy (not physical units)
    if (km == 1)       Serial.printf("[%lu ms] ⚡ STRIKE #%lu — OVERHEAD (energy=%lu)\n", millis(), strikes, eng);
    else if (km == 63) Serial.printf("[%lu ms] ⚡ STRIKE #%lu — out of range / dist unknown (energy=%lu)\n", millis(), strikes, eng);
    else               Serial.printf("[%lu ms] ⚡ STRIKE #%lu — ~%u km away (energy=%lu)\n", millis(), strikes, km, eng);
  }
  else if (intVal == DISTURBER_INT) {
    disturbers++;
    Serial.printf("[%lu ms] · disturber (man-made noise) #%lu\n", millis(), disturbers);
  }
  else if (intVal == NOISE_INT) {
    noises++;
    Serial.printf("[%lu ms] · noise floor too low #%lu (press 'N' to raise)\n", millis(), noises);
  }

  // Heartbeat every 5 s so silence still reads as "alive, listening".
  if (millis() - lastBeat > 5000) {
    lastBeat = millis();
    Serial.printf("[%lu ms] …listening  (strikes=%lu disturbers=%lu noise=%lu)\n",
                  millis(), strikes, disturbers, noises);
  }

  delay(10);   // ~100 Hz poll — far faster than lightning event cadence
}
