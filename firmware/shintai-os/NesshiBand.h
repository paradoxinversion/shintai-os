#ifndef NESSHI_BAND_H
#define NESSHI_BAND_H

// NesshiBand — the pure, hardware-free temperature->cue mapping for Nesshi (熱視)
// (specs/zokyo/nesshi.md). No sensor, no NeoPixel: it turns one surface temperature
// (°C, already reduced from the MLX90640 frame by the sketch) into the Aizu cue
// Nesshi should post while the BOOT button is held. Keeping it Arduino-free makes
// the safety bands and the edge hysteresis host-testable (tools/nesshi-band-test.cpp);
// the sketch supplies the live read (centre spot or scene max) and the button state.
//
// Unlike Kehai (frantic) and even Kanki (slow calm), Nesshi's voice is STEADY and
// colour-dominant: you are reading a value, not being alarmed (nesshi.md
// "Behaviour"). The whole point is the band edges — the amber->red transitions sit
// on the real burn-safety line (skin pain ~45 °C, burns rise sharply past 50 °C),
// so the colour flip IS the "is it safe to touch?" answer (ND-5).

#include <stdint.h>
#include "AizuCore.h"

// Band edges (°C) — ND-5. Each is a band's upper edge; DANGER has none.
//   < 0      Cold    (blue)   freezing
//   0 .. 40  Cool    (green)  safe to touch
//   40 .. 50 Warm    (amber)  uncomfortably hot — caution
//   50 .. 60 Hot     (orange) burn risk on contact
//   > 60     Danger  (red)    do not touch
static const float NESSHI_COLD_MAX = 0.0f;    // < this          -> Cold
static const float NESSHI_COOL_MAX = 40.0f;   // 0 .. 40         -> Cool  (safe)
static const float NESSHI_WARM_MAX = 50.0f;   // 40 .. 50        -> Warm
static const float NESSHI_HOT_MAX  = 60.0f;   // 50 .. 60        -> Hot; >= 60 -> Danger

// Edge hysteresis (°C). The MLX90640 pixel noise (and a reading hovering on an edge)
// would otherwise flip the colour; a band only changes once the read clears the
// boundary by this margin (AC-2). Small — the safety bands are 10 °C wide.
static const float NESSHI_HYST_C = 2.0f;

// Cue hues. Blue (Cold) and green (Cool/safe) join the amber/orange/red already
// shared with Kanki/Kehai. Green here is a STEADY full-cap read — distinct from the
// idle green BREATHE (which only shows when no cue is live, i.e. not while held).
static const AizuColour NESSHI_BLUE   = {  0,  80, 255};   // Cold   — freezing
static const AizuColour NESSHI_GREEN  = {  0, 255,   0};   // Cool   — safe to touch
static const AizuColour NESSHI_AMBER  = {255, 110,   0};   // Warm   — caution
static const AizuColour NESSHI_ORANGE = {255,  60,   0};   // Hot    — burn risk
static const AizuColour NESSHI_RED    = {255,   0,   0};   // Danger — do not touch

// The "no thermal sensor" cue: a distinct dim magenta slow PULSE so a hold still
// gives feedback (the button works) when the MLX90640 is absent — clearly NOT a
// temperature colour. Firmware integration point 5 / the degrade path.
static const AizuColour  NESSHI_NODATA          = {180, 0, 180};
static const uint16_t    NESSHI_NODATA_PULSE_MS = 1000;

// Band indices double as an ascending scale (0..4) for the hysteresis walk, coldest
// to hottest. -1 (or any out-of-range value) is the "unseeded" sentinel the sketch
// passes at the start of each hold so the first frame snaps to the true band.
enum NesshiBand {
  NESSHI_BAND_COLD   = 0,
  NESSHI_BAND_COOL   = 1,
  NESSHI_BAND_WARM   = 2,
  NESSHI_BAND_HOT    = 3,
  NESSHI_BAND_DANGER = 4
};

// Instantaneous band from a °C read with NO hysteresis — used to seed the walk on
// the first frame of a hold (from the -1 sentinel) so it snaps to the true band.
static inline int nesshiRawBand(float c) {
  if (c >= NESSHI_HOT_MAX)  return NESSHI_BAND_DANGER;   // >= 60
  if (c >= NESSHI_WARM_MAX) return NESSHI_BAND_HOT;      // 50 .. 60
  if (c >= NESSHI_COOL_MAX) return NESSHI_BAND_WARM;     // 40 .. 50
  if (c >= NESSHI_COLD_MAX) return NESSHI_BAND_COOL;     // 0 .. 40  (safe)
  return NESSHI_BAND_COLD;                               // < 0
}

// Advance the hysteretic band. `prev` out of [COLD, DANGER] (the sketch's -1
// re-seed sentinel) snaps via nesshiRawBand. Otherwise the band only rises past a
// boundary+HYST, or falls below boundary-HYST — the dead zone that stops edge
// flip-flop (AC-2). A large jump walks multiple bands in one step. Pure in
// (c, prev). Mirrors kankiStep with float °C edges.
static inline int nesshiStep(float c, int prev) {
  if (prev < NESSHI_BAND_COLD || prev > NESSHI_BAND_DANGER)
    return nesshiRawBand(c);
  const float bnd[4] = { NESSHI_COLD_MAX, NESSHI_COOL_MAX, NESSHI_WARM_MAX, NESSHI_HOT_MAX };  // upper edges
  int b = prev;
  while (b < NESSHI_BAND_DANGER && c >= bnd[b]     + NESSHI_HYST_C) b++;  // rise
  while (b > NESSHI_BAND_COLD   && c <  bnd[b - 1] - NESSHI_HYST_C) b--;  // fall
  return b;
}

// The cue Nesshi should post for a resolved band while the button is held. Always
// posts (a held read always shows something): an INTERACTIVE, STEADY colour at
// AIZU_PRIO_NESSHI_HELD — dominates the ambient wallpaper but yields to a safety
// ALERT (AZ-10). Motion is STEADY for every band: you are reading, not being
// alarmed. The escalation is entirely in the colour (blue->green->amber->orange->red).
struct NesshiCue {
  bool       post;
  int        priority;
  AizuColour colour;
  AizuMotion motion;
};

static inline NesshiCue nesshiCueFor(int band) {
  NesshiCue c = {true, AIZU_PRIO_NESSHI_HELD, NESSHI_GREEN, {AIZU_STEADY, 0}};
  switch (band) {
    case NESSHI_BAND_COLD:   c.colour = NESSHI_BLUE;   break;
    case NESSHI_BAND_WARM:   c.colour = NESSHI_AMBER;  break;
    case NESSHI_BAND_HOT:    c.colour = NESSHI_ORANGE; break;
    case NESSHI_BAND_DANGER: c.colour = NESSHI_RED;    break;
    case NESSHI_BAND_COOL:
    default:                 c.colour = NESSHI_GREEN;  break;   // safe to touch
  }
  return c;
}

// The cue for a hold with no thermal sensor present: distinct dim magenta PULSE at
// the same interactive priority, so the button gesture still gives feedback but is
// unmistakably "no reading," not a temperature (integration point 5).
static inline NesshiCue nesshiNoSensorCue() {
  NesshiCue c = {true, AIZU_PRIO_NESSHI_HELD, NESSHI_NODATA, {AIZU_PULSE, NESSHI_NODATA_PULSE_MS}};
  return c;
}

#endif  // NESSHI_BAND_H
