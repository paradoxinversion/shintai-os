#ifndef KANKI_BAND_H
#define KANKI_BAND_H

// KankiBand — the pure, hardware-free CO2->cue mapping for Kanki (換気)
// (specs/zokyo/kanki.md). No sensor, no NeoPixel: it turns one SCD-40 reading
// (co2_ppm) into the Aizu cue Kanki should post. Keeping it Arduino-free makes the
// band logic and the hysteresis host-testable (tools/kanki-band-test.cpp); the
// sketch supplies the live co2 and the warm-up gate (scdHasData).
//
// The deliberate contrast with Kehai (kanki.md "Calm vocabulary"): CO2 moves over
// MINUTES, so Kanki's voice is colour-dominant and SLOW — even "Bad" is a strong
// but slow pulse, never a strobe. Kehai's proximity reflex is frantic; the two
// distinct voices on the one pixel are what let Aizu mix them intelligibly.
//
// Kanki posts its ACTIVE bands (Stuffy/Poor/Bad) plus a warm-up cue. "Fresh" is
// quiescent: it posts NOTHING and falls through to Aizu's Idle green wallpaper —
// which is already, verbatim, the Fresh spec (dim green tethered, dark + ~30 s
// heartbeat on battery: Kehai-Hikari D-1 + Kanki KD-2). So the Idle policy IS the
// Fresh state; re-posting it would only duplicate the arbiter's idle timers.

#include <stdint.h>
#include "AizuCore.h"

// Band thresholds (ppm) — KD-4. Outdoor air ~420; cognitive effects from
// ~1000-1400; 2000+ is unmistakably stuffy. Each is a band's upper edge.
static const uint16_t KANKI_FRESH_MAX  = 800;    // < this            -> Fresh (CO2_FRESH)
static const uint16_t KANKI_STUFFY_MAX = 1200;   // 800 .. 1200       -> Stuffy
static const uint16_t KANKI_POOR_MAX   = 2000;   // 1200 .. 2000      -> Poor  (CO2_POOR = Bad onset)
                                                 // >= 2000           -> Bad

// Hysteresis (kanki.md "Hysteresis"): CO2 hovers on band edges. The SCD refreshes
// only ~every 5 s, so Aizu's 250 ms temporal debounce can't smooth a boundary
// flip-flop (each 5 s sample "sticks") — value-domain hysteresis has to live HERE.
// A band only changes once co2 clears the boundary by this margin (AC-2).
static const int KANKI_HYST_PPM = 50;

// Motion periods (ms). All slow — the calm vocabulary. Escalation rides on colour
// (amber -> orange -> red) and on the breathe tightening toward the Bad pulse;
// peak brightness itself is the arbiter's cap (AIZU_MAX_BRIGHT), not a per-cue
// field, so "Poor brighter than Stuffy" is expressed as a more insistent motion.
static const uint16_t KANKI_STUFFY_BREATHE_MS = 4000;   // slow breathe (~3-4 s)
static const uint16_t KANKI_POOR_BREATHE_MS   = 3000;   // slow breathe, more insistent
static const uint16_t KANKI_BAD_PULSE_MS      = 1500;   // slow STRONG pulse (never a strobe)
static const uint16_t KANKI_WARMUP_BREATHE_MS = 3500;   // gentle "sensor warming"

// Cue hues. Green (Fresh) never appears here — Fresh is the Idle wallpaper.
static const AizuColour KANKI_AMBER  = {255, 110, 0};   // Stuffy — ventilation slipping
static const AizuColour KANKI_ORANGE = {255,  60, 0};   // Poor   — open a window
static const AizuColour KANKI_RED    = {255,   0, 0};   // Bad    — ventilate now
static const AizuColour KANKI_WARM   = { 90, 130, 220}; // Warm-up — dim white/blue

// Band indices double as an ascending scale (0..3) for the hysteresis walk. Fresh
// is the lowest (quiescent) band; Warm-up is the pre-data state (no index).
enum KankiBand {
  KANKI_BAND_FRESH  = 0,
  KANKI_BAND_STUFFY = 1,
  KANKI_BAND_POOR   = 2,
  KANKI_BAND_BAD    = 3
};

// Instantaneous band from co2 with NO hysteresis — used to seed the walk on the
// first reading (leaving warm-up) so it snaps to the true band.
static inline int kankiRawBand(uint16_t co2) {
  if (co2 >= KANKI_POOR_MAX)   return KANKI_BAND_BAD;
  if (co2 >= KANKI_STUFFY_MAX) return KANKI_BAND_POOR;
  if (co2 >= KANKI_FRESH_MAX)  return KANKI_BAND_STUFFY;
  return KANKI_BAND_FRESH;
}

// Advance the hysteretic band. `prev` is the last band index; anything out of
// [FRESH, BAD] (e.g. the sketch's -1 "unseeded"/warm-up sentinel) snaps via
// kankiRawBand. Otherwise the band only rises past a boundary+HYST, or falls below
// boundary-HYST — the dead zone that stops edge flip-flop. A large jump (spike or
// a long dropout) walks multiple bands in one step. Pure in (co2, prev).
static inline int kankiStep(uint16_t co2, int prev) {
  if (prev < KANKI_BAND_FRESH || prev > KANKI_BAND_BAD)
    return kankiRawBand(co2);
  const int bnd[3] = { KANKI_FRESH_MAX, KANKI_STUFFY_MAX, KANKI_POOR_MAX };  // upper edges
  int b = prev;
  while (b < KANKI_BAND_BAD   && (int)co2 >= bnd[b]     + KANKI_HYST_PPM) b++;  // rise
  while (b > KANKI_BAND_FRESH && (int)co2 <  bnd[b - 1] - KANKI_HYST_PPM) b--;  // fall
  return b;
}

// The cue Kanki should post for a resolved band. post=false means clearCue(KANKI)
// (Fresh -> quiescent -> Aizu Idle). Bad is a red slow PULSE (ALERT-class in the
// ladder, so it preempts without debounce); Poor/Stuffy are colour breathes.
struct KankiCue {
  bool       post;
  int        priority;
  AizuColour colour;
  AizuMotion motion;
};

static inline KankiCue kankiCueFor(int band) {
  KankiCue c = {false, AIZU_PRIO_IDLE, {0, 0, 0}, {AIZU_STEADY, 0}};
  switch (band) {
    case KANKI_BAND_BAD:
      c.post = true;
      c.priority = AIZU_PRIO_KANKI_BAD;
      c.colour = KANKI_RED;
      c.motion = {AIZU_PULSE, KANKI_BAD_PULSE_MS};
      break;
    case KANKI_BAND_POOR:
      c.post = true;
      c.priority = AIZU_PRIO_KANKI_POOR;
      c.colour = KANKI_ORANGE;
      c.motion = {AIZU_BREATHE, KANKI_POOR_BREATHE_MS};
      break;
    case KANKI_BAND_STUFFY:
      c.post = true;
      c.priority = AIZU_PRIO_KANKI_STUFFY;
      c.colour = KANKI_AMBER;
      c.motion = {AIZU_BREATHE, KANKI_STUFFY_BREATHE_MS};
      break;
    case KANKI_BAND_FRESH:
    default:
      c.post = false;   // quiescent — Aizu renders Idle green (== the Fresh state)
      break;
  }
  return c;
}

// The warm-up cue: for the first ~5 s after boot (scdHasData == false) show a
// distinct dim white/blue breathe — "SCD-40 warming," clearly not the green all-
// clear. Lowest Kanki priority so a live Kehai reflex/approach still wins.
static inline KankiCue kankiWarmupCue() {
  KankiCue c = {true, AIZU_PRIO_KANKI_WARMUP, KANKI_WARM, {AIZU_BREATHE, KANKI_WARMUP_BREATHE_MS}};
  return c;
}

#endif  // KANKI_BAND_H
