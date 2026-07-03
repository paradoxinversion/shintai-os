#ifndef KEHAI_BAND_H
#define KEHAI_BAND_H

// KehaiBand — the pure, hardware-free distance->cue mapping for Kehai-Hikari
// (specs/zokyo/kehai-hikari.md). No sensor, no NeoPixel: it turns one ToF range
// (mm) into the Aizu cue Kehai should post. Keeping it Arduino-free makes the
// band logic and the perceptual pulse curve host-testable (tools/kehai-band-test.cpp);
// the sketch supplies the live range and the NEAR_MM/FAR_MM thresholds.
//
// Kehai posts only its ACTIVE bands (Approach, Reflex). "Clear" and "no target"
// are quiescent — they post nothing and fall through to Aizu's Idle green
// wallpaper (Aizu: "quiescent != a cue"), so Kehai never fights the idle policy.

#include <stdint.h>
#include "AizuCore.h"

// Perceptual pulse endpoints (D-3). Approach period shortens sharply toward
// NEAR_MM; below PERIOD_MIN the pulse reads as effectively solid (the Reflex).
static const uint16_t KEHAI_PERIOD_MIN_MS = 120;
static const uint16_t KEHAI_PERIOD_MAX_MS = 1200;

// Cue hues. Amber for an approaching object, red for the too-close reflex.
static const AizuColour KEHAI_AMBER = {255, 110, 0};
static const AizuColour KEHAI_RED   = {255, 0, 0};

enum KehaiBand {
  KEHAI_BAND_IDLE,      // mm <= 0  — no target
  KEHAI_BAND_CLEAR,     // mm > FAR_MM
  KEHAI_BAND_APPROACH,  // NEAR_MM < mm <= FAR_MM
  KEHAI_BAND_REFLEX     // 0 < mm <= NEAR_MM  — mirrors alert == 1
};

// Classify a ToF reading (mm <= 0 means no target) against the sketch's bands.
static inline KehaiBand kehaiClassify(int16_t mm, int nearMm, int farMm) {
  if (mm <= 0)     return KEHAI_BAND_IDLE;
  if (mm > farMm)  return KEHAI_BAND_CLEAR;
  if (mm > nearMm) return KEHAI_BAND_APPROACH;
  return KEHAI_BAND_REFLEX;
}

// Approach pulse period (ms): period = PERIOD_MIN + (PERIOD_MAX-PERIOD_MIN)*f^2,
// f = clamp((mm-near)/(far-near), 0, 1). The f^2 puts the resolution in the last
// half-metre where it's actionable, not in far distances (D-3).
static inline uint16_t kehaiApproachPeriod(int16_t mm, int nearMm, int farMm) {
  float span = (float)(farMm - nearMm);
  float f = (span > 0.0f) ? (float)(mm - nearMm) / span : 0.0f;
  if (f < 0.0f) f = 0.0f;
  if (f > 1.0f) f = 1.0f;
  float p = (float)KEHAI_PERIOD_MIN_MS + (float)(KEHAI_PERIOD_MAX_MS - KEHAI_PERIOD_MIN_MS) * f * f;
  return (uint16_t)(p + 0.5f);
}

// The cue Kehai should post for a given range. post=false means clearCue(KEHAI)
// (quiescent -> Aizu Idle). Reflex is a red SOLID ALERT; Approach is an amber
// PULSE at the perceptual period, priority-ranked as AMBIENT+ in Aizu's ladder.
struct KehaiCue {
  bool       post;
  int        priority;
  AizuColour colour;
  AizuMotion motion;
};

static inline KehaiCue kehaiCueFor(int16_t mm, int nearMm, int farMm) {
  KehaiCue c = {false, AIZU_PRIO_IDLE, {0, 0, 0}, {AIZU_STEADY, 0}};
  switch (kehaiClassify(mm, nearMm, farMm)) {
    case KEHAI_BAND_REFLEX:
      c.post = true;
      c.priority = AIZU_PRIO_KEHAI_REFLEX;
      c.colour = KEHAI_RED;
      c.motion = {AIZU_SOLID, 0};
      break;
    case KEHAI_BAND_APPROACH:
      c.post = true;
      c.priority = AIZU_PRIO_KEHAI_APPROACH;
      c.colour = KEHAI_AMBER;
      c.motion = {AIZU_PULSE, kehaiApproachPeriod(mm, nearMm, farMm)};
      break;
    case KEHAI_BAND_IDLE:
    case KEHAI_BAND_CLEAR:
    default:
      c.post = false;   // quiescent — Aizu renders Idle
      break;
  }
  return c;
}

#endif  // KEHAI_BAND_H
