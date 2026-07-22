#ifndef ZANSHIN_GRID_H
#define ZANSHIN_GRID_H

// ZanshinGrid — the pure, hardware-free derivation + pack for Zanshin (残心)
// (specs/zokyo/zanshin.md): the rear VL53L5CX depth field that supersedes Kōei's two
// point arcs. No sensor, no BLE: it turns the 8x8 (64-zone) per-zone distances into
//   (a) the LEFT/RIGHT nearest ranges that feed the existing distance_l/r_mm + alert
//       (so Kōei's whole downstream — CSV, the Distance char, the Kehai reflex — is
//       unchanged; only the source of the two ranges moved from two sensors to one field);
//   (b) the packed 128-byte depth grid the apps render as a rear depth panel.
// Host-testable (tools/zanshin-grid-test.cpp); the sketch supplies the live frame + BLE.
//
// Zone layout: row-major 8x8, zone z = row*8 + col. Columns split the field into a LEFT
// half (col 0..3) and a RIGHT half (col 4..7) — which physical side is which depends on
// how the sensor sits on the pack and is validated on-wrist (swap the halves to flip).
//
// Wire format (Rear Depth Grid characteristic, CONTRACT.md): 128 bytes = 64 x uint16 LE
// millimetres, row-major; a zone with no valid target packs 0 (a real ToF range is never 0).

#include <stdint.h>

static const int ZANSHIN_W = 8, ZANSHIN_H = 8;
static const int ZANSHIN_ZONES = ZANSHIN_W * ZANSHIN_H;   // 64
static const int ZANSHIN_GRID_BYTES = ZANSHIN_ZONES * 2;  // 128 (uint16 per zone)

// A zone's range is usable when the VL53L5CX target_status is 5 (100% valid) or 9 (valid,
// large pulse) — ST's two "range valid" codes. Anything else is treated as no target.
static inline bool zanshinZoneValid(uint8_t status) {
  return status == 5 || status == 9;
}

// Pack the 64-zone field as 128 bytes (uint16 LE mm, row-major). Invalid/no-target -> 0.
static inline void zanshinPackGrid(const int16_t* distMm, const uint8_t* status, uint8_t* out) {
  for (int z = 0; z < ZANSHIN_ZONES; z++) {
    uint16_t mm = (zanshinZoneValid(status[z]) && distMm[z] > 0) ? (uint16_t)distMm[z] : 0;
    out[z * 2]     = (uint8_t)(mm & 0xFF);
    out[z * 2 + 1] = (uint8_t)((mm >> 8) & 0xFF);
  }
}

// Nearest valid target in the LEFT half (col 0..3) and RIGHT half (col 4..7), in mm; -1 if
// that half sees nothing. Feeds the existing distance_l_mm / distance_r_mm + alert path.
static inline void zanshinDeriveLR(const int16_t* distMm, const uint8_t* status,
                                   int16_t* lMm, int16_t* rMm) {
  int16_t l = -1, r = -1;
  for (int row = 0; row < ZANSHIN_H; row++)
    for (int col = 0; col < ZANSHIN_W; col++) {
      int z = row * ZANSHIN_W + col;
      if (!zanshinZoneValid(status[z]) || distMm[z] <= 0) continue;
      int16_t* side = (col < ZANSHIN_W / 2) ? &l : &r;
      if (*side < 0 || distMm[z] < *side) *side = distMm[z];
    }
  *lMm = l; *rMm = r;
}

#endif  // ZANSHIN_GRID_H
