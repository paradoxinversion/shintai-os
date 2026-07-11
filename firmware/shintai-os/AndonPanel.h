#ifndef ANDON_PANEL_H
#define ANDON_PANEL_H

// Andon (行灯) — on-body LED-matrix lantern (specs/zokyo/andon.md). An 8x12 Modulino
// LED Matrix (ABX00152, 96 LEDs) driven as an output *surface*, not a sensor: it
// draws ambient flair now (a falling-raindrop field) and is the panel that future
// sensor-driven modes will paint. Produces no telemetry — nothing in CONTRACT.md.
//
// Raw-Wire driver — deliberately NO Arduino_Modulino / ArduinoGraphics dependency.
// The module speaks a trivial I2C protocol, so the firmware talks to it the same way
// it talks to the PCA9546 mux rather than dragging Modulino.cpp's whole sensor-driver
// chain (VL53L4CD/ED, LSM6DSOX, LPS22HB, HS300x, LTR381RGB) into the build for one
// LED panel. It runs in the panel's 4-bit GRAYSCALE mode so each raindrop is a bright
// head fading to a dim tail:
//   - mode set (once):  write "GS4" padded to 12 bytes  ->  4-bit grayscale
//   - frame (per tick): write 48 bytes; row-major, 2 px/byte, high nibble = even
//     pixel (pixel index = row*12 + col, each nibble a 0..15 brightness)
// Verified against Arduino_Modulino 0.9.0 (Modulino_LED_Matrix.h): prepareFrame() is
// a no-op outside MonochromaticHorizontal, setMode() sends the "GS4" tag, sendFrame
// writes the 48 bytes unchanged, and the GRADIENT example fixes the row-major /
// high-nibble-first packing above. The library is kept only for the bench bring-up
// sketch (firmware/modulino-matrix-bringup).
//
// Bus: the module's default 7-bit address is 0x39, which COLLIDES with the APDS9960
// gesture sensor on the spine. It is readdressed once to 0x3F on the bench (see
// firmware/modulino-addr-changer); the firmware talks to it there. Wire.begin(41,40)
// in setup() has already set the STEMMA QT pins by the time andonBegin() runs.
//
// Orientation: the DATA layout above is exact; which physical edge reads as "up"
// (the direction the rain falls) depends on how the panel is mounted on-body and is
// validated on-wrist — negate the row in andonSetPixel (ANDON_ROWS-1 - row) to flip.

#include <Arduino.h>
#include <Wire.h>
#include "esp_random.h"

static const uint8_t  ANDON_ADDR        = 0x3F;   // readdressed off the 0x39 APDS clash
static const uint8_t  ANDON_COLS        = 12;     // panel is 12 wide
static const uint8_t  ANDON_ROWS        = 8;      // ... and 8 tall
static const uint8_t  ANDON_FRAME_BYTES = 48;     // grayscale: 96 px / 2 px-per-byte
static const uint32_t ANDON_TICK_MS     = 90;     // animation cadence (~11 fps)
static const uint8_t  ANDON_DROPS       = 5;      // simultaneous raindrops

// Per-drop brightness ramp (4-bit, 0..15): index 0 is the head (brightest), each
// further entry a dimmer trailing cell. Its length is the streak length.
static const uint8_t ANDON_LEVELS[] = { 15, 5, 1 };
static const uint8_t ANDON_TRAIL    = sizeof(ANDON_LEVELS);

// One falling drop: a fixed column and a head row that advances down each tick. The
// head starts ABOVE the panel (negative row), staggered so drops don't fall in
// lockstep; once the whole streak clears the bottom it respawns at a fresh column.
struct AndonDrop { int8_t col; int8_t headRow; };

static bool      andonPresent  = false;
static AndonDrop andonDrops[ANDON_DROPS];
static uint32_t  andonLastTick = 0;

// Bare address-ACK probe — the same non-fatal presence test the ToF mux and OLED use.
static inline bool andonProbe() {
  Wire.beginTransmission(ANDON_ADDR);
  return Wire.endTransmission() == 0;
}

// Select 4-bit grayscale mode: the "GS4" tag padded to a 12-byte write (matches the
// library's mono->grayscale switch; the module keys on the leading tag).
static inline void andonSendMode() {
  uint8_t buf[12] = { 'G', 'S', '4' };   // rest already zero
  Wire.beginTransmission(ANDON_ADDR);
  Wire.write(buf, sizeof(buf));
  Wire.endTransmission();
}

// Push one 48-byte grayscale frame.
static inline void andonSendFrame(const uint8_t* frame) {
  Wire.beginTransmission(ANDON_ADDR);
  Wire.write(frame, ANDON_FRAME_BYTES);
  Wire.endTransmission();
}

// Set one pixel's 4-bit brightness in a grayscale frame (row-major, 2 px/byte, high
// nibble = even pixel). Takes the MAX so overlapping drops keep the brighter value.
static inline void andonSetPixel(uint8_t* frame, uint8_t row, uint8_t col, uint8_t val4) {
  uint16_t p   = (uint16_t)row * ANDON_COLS + col;
  uint8_t  idx = (uint8_t)(p >> 1);
  if (p & 1) {                                             // odd pixel -> low nibble
    if ((frame[idx] & 0x0F) < val4) frame[idx] = (frame[idx] & 0xF0) | (val4 & 0x0F);
  } else {                                                 // even pixel -> high nibble
    if ((frame[idx] >> 4) < val4)   frame[idx] = (uint8_t)(val4 << 4) | (frame[idx] & 0x0F);
  }
}

static inline void andonRespawn(AndonDrop& d) {
  d.col     = (int8_t)random(ANDON_COLS);
  d.headRow = (int8_t)-(1 + random(ANDON_ROWS));   // start 1..8 rows above the top
}

// Presence-gated bring-up, non-fatal (matches every sensor): a missing or
// un-readdressed panel warns and Andon simply stays dark. Wire pins already set.
static inline void andonBegin() {
  andonPresent = andonProbe();
  if (andonPresent) {
    randomSeed(esp_random());            // vary the rain across boots
    andonSendMode();
    uint8_t blank[ANDON_FRAME_BYTES] = {0};
    andonSendFrame(blank);
    for (uint8_t i = 0; i < ANDON_DROPS; i++) andonRespawn(andonDrops[i]);
    Serial.println("[OK] Andon LED matrix @ 0x3F (grayscale raindrop flair)");
  } else {
    Serial.println("[WARN] Andon matrix not found @ 0x3F — panel dark "
                   "(readdressed off 0x39? see firmware/modulino-addr-changer)");
  }
}

// Advance + repaint the raindrop field. Non-blocking: self-rate-limited to
// ANDON_TICK_MS and a no-op when the panel is absent, so it's safe to call every
// loop() iteration alongside the reflex/telemetry work (like serviceReflex et al.).
static inline void andonService(uint32_t now) {
  if (!andonPresent) return;
  if ((uint32_t)(now - andonLastTick) < ANDON_TICK_MS) return;
  andonLastTick = now;

  uint8_t frame[ANDON_FRAME_BYTES] = {0};
  for (uint8_t i = 0; i < ANDON_DROPS; i++) {
    AndonDrop& d = andonDrops[i];
    d.headRow++;
    for (uint8_t t = 0; t < ANDON_TRAIL; t++) {          // 0 = bright head, then dimmer tail
      int r = d.headRow - t;
      if (r >= 0 && r < ANDON_ROWS) andonSetPixel(frame, (uint8_t)r, d.col, ANDON_LEVELS[t]);
    }
    if (d.headRow - (int)(ANDON_TRAIL - 1) >= ANDON_ROWS) andonRespawn(d);   // whole streak cleared
  }
  andonSendFrame(frame);
}

#endif  // ANDON_PANEL_H
