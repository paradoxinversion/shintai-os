// Shintai-OS — Modulino address changer (QT Py, matrix-only)
//
// One-shot bench tool: move the Modulino LED Matrix (ABX00152) off its default
// 7-bit address 0x39 — which COLLIDES with the APDS9960 gesture sensor on the
// Shintai-OS spine — to 0x3F, where the shipped firmware (AndonPanel.h) talks to
// it. The new address is stored in the module PERMANENTLY (survives power-off).
//
// This is the stock Arduino_Modulino examples/Utilities/AddressChanger, stripped to
// the QT Py + single-device case: it uses Wire on the STEMMA QT pins (SDA=41,
// SCL=40) instead of Wire1, and — since the matrix is the ONLY thing plugged in —
// it auto-finds the one device and retargets it rather than making you type
// "0x39 0x3F" into the Serial Monitor. There is still ONE guard: it waits for you
// to send a character before the persistent write, so you can confirm the device.
//
//   arduino-cli compile --upload \
//     -b esp32:esp32:adafruit_qtpy_esp32s3_nopsram:CDCOnBoot=cdc,PartitionScheme=tinyuf2,FlashSize=8M \
//     -p /dev/cu.usbmodem101 firmware/modulino-addr-changer
//
// Flash it, open Serial Monitor @ 115200, confirm the prompt, then re-flash the
// i2c-scan (should now show the matrix at 0x3F) and finally the main firmware.
//
// The address-change command (from the Modulino protocol): to the module at its
// CURRENT address, write { 'C', 'F', newAddr<<1, 0, 0, ... } (40 bytes). The <<1
// is the 7-bit -> 8-bit form the module firmware expects (so 0x3F is sent as 0x7E),
// the same doubling the discovery read uses. Then read 1 byte back at the new
// address to confirm it moved.

#include <Wire.h>

const uint8_t MATRIX_DEFAULT_ADDR = 0x39;   // where the matrix ships (informational)
const uint8_t TARGET_ADDR         = 0x3F;   // where Shintai-OS wants it (AndonPanel.h)

// Scan 0x08..0x77 and return the single device found, or -1 (none) / -2 (more than
// one — unexpected here, since only the matrix should be connected).
int findLoneDevice() {
  int found = -1;
  for (uint8_t a = 0x08; a <= 0x77; a++) {
    Wire.beginTransmission(a);
    if (Wire.endTransmission() == 0) {
      Serial.printf("  found device at 0x%02X\n", a);
      if (found == -1) found = a; else return -2;   // more than one on the bus
    }
  }
  return found;
}

// Send the persistent address-change command to `curAddr`, then verify at `newAddr`.
bool changeAddress(uint8_t curAddr, uint8_t newAddr) {
  uint8_t data[40] = { 'C', 'F', (uint8_t)(newAddr << 1) };   // rest already zero
  Serial.printf("Writing change command 0x%02X -> 0x%02X ...", curAddr, newAddr);
  Wire.beginTransmission(curAddr);
  Wire.write(data, sizeof(data));
  Wire.endTransmission();
  delay(500);                              // let the module commit + re-register

  Wire.requestFrom(newAddr, (uint8_t)1);   // confirm it answers at the new address
  if (Wire.available()) { Serial.println(" done."); return true; }
  Serial.println(" NO ACK at new address — change may have failed.");
  return false;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) delay(10);          // native USB: wait for the host to attach
  Wire.begin(41, 40);                 // STEMMA QT bus — matches the main sketch
  Serial.println("Modulino address changer (QT Py) — SDA=41 SCL=40 @ 115200\n");

  Serial.println("Scanning (expect exactly one device — the matrix)...");
  int dev = findLoneDevice();

  if (dev == -1) {
    Serial.println("\nNo device on the bus. Check: matrix plugged into STEMMA QT,");
    Serial.println("and that it's the only thing connected. Halting.");
    return;
  }
  if (dev == -2) {
    Serial.println("\nMore than one device on the bus. This tool expects ONLY the");
    Serial.println("matrix connected so it can't retarget the wrong one. Halting.");
    return;
  }
  if (dev == TARGET_ADDR) {
    Serial.printf("\nDevice is ALREADY at 0x%02X — nothing to do. You can re-flash\n", TARGET_ADDR);
    Serial.println("the i2c-scan / main firmware now. Halting.");
    return;
  }

  Serial.printf("\nWill change the device at 0x%02X to 0x%02X (permanent).\n", dev, TARGET_ADDR);
  if (dev != MATRIX_DEFAULT_ADDR)
    Serial.printf("(note: it's at 0x%02X, not the stock 0x%02X — retargeting it anyway.)\n",
                  dev, MATRIX_DEFAULT_ADDR);
  Serial.println(">>> Send any character in the Serial Monitor to proceed <<<");
  while (Serial.available() == 0) delay(10);
  while (Serial.available() > 0) Serial.read();     // drain the input

  if (changeAddress((uint8_t)dev, TARGET_ADDR)) {
    Serial.printf("\nSuccess: matrix is now at 0x%02X. Power-cycle to be safe, then\n", TARGET_ADDR);
    Serial.println("re-flash firmware/i2c-scan to confirm, then the main firmware.");
  } else {
    Serial.println("\nDid not confirm. Re-check wiring and reset the board to retry.");
  }
}

void loop() {
  // One-shot: all work happens in setup(). Nothing to do here.
}
