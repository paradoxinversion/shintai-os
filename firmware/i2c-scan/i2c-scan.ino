// Shintai-OS — STEMMA QT / I2C bus scanner (bench bring-up tool)
//
// Step 1 of the Phase-1 bring-up (specs/phase1-bringup.md): confirm every
// plug-and-play part *answers on the bus* before a line of driver code lands.
// Standalone sketch — no sensors driven, no BLE, no flash. Flash it, open the
// Serial Monitor at 115200, and read the pass/fail map every 3 s.
//
// Compile target is this DIRECTORY (Arduino needs sketch == folder name):
//   arduino-cli compile --upload \
//     -b esp32:esp32:adafruit_qtpy_esp32s3_nopsram:CDCOnBoot=cdc,PartitionScheme=tinyuf2,FlashSize=8M \
//     -p /dev/cu.usbmodem101 firmware/i2c-scan
//
// Same STEMMA QT bus as the main sketch: Wire.begin(41, 40) (SDA=41, SCL=40).

#include <Wire.h>

// Known-address map — every hit is labeled against what we expect to see, so a
// scan reads as pass/fail instead of a wall of hex. Sources: the on-spine
// sensors already in firmware/shintai-os/shintai-os.ino and the Phase-1
// newcomers in specs/phase1-bringup.md. Some parts have a strap-selectable
// alt address; both are listed where relevant.
struct KnownDev { uint8_t addr; const char* label; };
const KnownDev KNOWN[] = {
  {0x10, "PA1010D GPS        [spine]"},
  {0x1C, "LIS3MDL mag        [spine] (alt 0x1E)"},
  {0x1E, "LIS3MDL mag        [spine] (alt of 0x1C)"},
  {0x24, "PN532 NFC          [phase1, solder-gated]"},
  {0x29, "VL53L4CX ToF       [spine + phase1 x2 via mux]"},
  {0x33, "MLX90640 thermal   [spine]"},
  {0x3C, "OLED (SSD1306/SH1106) [phase1, output] (128x32 default; alt 0x3D)"},
  {0x3D, "OLED (SSD1306/SH1106) [phase1, output] (128x64 default; alt 0x3C)"},
  {0x40, "INA260 power       [phase1]"},
  {0x5A, "DRV2605L haptic    [known-good sanity check]"},
  {0x62, "SCD-40 climate     [spine]"},
  {0x6A, "LSM6DSOX IMU       [spine] (alt 0x6B)"},
  {0x6B, "LSM6DSOX IMU       [spine] (alt of 0x6A)"},
  {0x70, "PCA9546 mux        [phase1] (hides its channels until enabled)"},
  {0x76, "BME688 env         [phase1] (alt 0x77)"},
  {0x77, "BME688 env         [phase1] (alt of 0x76)"},
};

const char* lookup(uint8_t addr) {
  for (const KnownDev& d : KNOWN) if (d.addr == addr) return d.label;
  return "unknown / unexpected";
}

// PCA9546A: 4-channel I2C mux at 0x70. Its control register is a single byte —
// bit N enables channel N. Write (1 << ch) to expose exactly one channel's bus;
// write 0x00 to isolate all of them. The mux itself keeps ACKing at 0x70 no
// matter what's selected, so behind-the-mux scans skip 0x70 to avoid noise.
const uint8_t MUX_ADDR   = 0x70;
const uint8_t MUX_CHANS  = 4;

bool muxPresent() {
  Wire.beginTransmission(MUX_ADDR);
  return Wire.endTransmission() == 0;
}

void muxSelect(int ch) {            // 0..3 to enable one channel, -1 to isolate all
  Wire.beginTransmission(MUX_ADDR);
  Wire.write(ch < 0 ? 0x00 : (uint8_t)(1 << ch));
  Wire.endTransmission();
}

// Scan the currently-visible bus, printing labeled hits at `indent`. Optionally
// skip one address (used to hide the mux from its own behind-the-channel scans).
int scanBus(const char* indent, int skip) {
  int found = 0;
  for (uint8_t addr = 1; addr < 127; addr++) {
    if (addr == skip) continue;
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {   // 0 = device ACKed
      Serial.printf("%s0x%02X  %s\n", indent, addr, lookup(addr));
      found++;
    }
  }
  return found;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);   // native USB: wait for the host to attach
  Wire.begin(41, 40);          // STEMMA QT bus — matches the main sketch
  Serial.println("Shintai-OS I2C scanner — SDA=41 SCL=40 @ 115200");
  Serial.println("Note: parts behind the PCA9546 mux (0x70) are hidden until a");
  Serial.println("channel is enabled; PN532 is invisible until soldered + DIP=I2C.");
}

void loop() {
  Serial.println("-- scanning 0x01..0x7E (main bus) --");
  int found = scanBus("  ", -1);
  Serial.printf("-- %d device(s) on main bus --\n", found);

  // If the PCA9546 is on the bus, walk its 4 channels so parts behind it (the
  // two colliding VL53L4CX at 0x29) show up on their own channel. A ToF plugged
  // into channel N appears as 0x29 under "ch N" and NOWHERE on the main bus —
  // that's the mux doing its job, not a fault.
  if (muxPresent()) {
    for (int ch = 0; ch < MUX_CHANS; ch++) {
      muxSelect(ch);
      delay(5);
      Serial.printf("  mux ch %d:\n", ch);
      int n = scanBus("    ", MUX_ADDR);      // skip 0x70 — it always ACKs
      if (n == 0) Serial.println("    (empty)");
    }
    muxSelect(-1);   // isolate all channels again, leaving the bus clean
  }
  Serial.println();
  delay(3000);
}
