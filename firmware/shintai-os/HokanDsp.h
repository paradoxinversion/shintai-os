#ifndef HOKAN_DSP_H
#define HOKAN_DSP_H

// HokanDsp — the pure, hardware-free gait DSP for Hokan (歩勘)
// (specs/zokyo/hokan.md). No sensor, no NeoPixel, no millis(): just two detectors
// that take accel-magnitude samples (m/s², with a timestamp) and decide "was that a
// step?" / "was that a fall?". Keeping the DSP Arduino-free is what makes it
// host-testable (tools/hokan-dsp-test.cpp) — you replay a synthetic |g| trace and
// assert the step count / fall event, no board (Hokan's "first on-device real-time
// DSP" finding, exercised on the host).
//
// The sketch supplies the live samples (serviceHokan reads the LSM6DSOX fast and
// passes accel_mag + millis()); this header holds the decision logic and the Aizu
// SOS cue. Position integration (dead reckoning) lives BASE-SIDE in groundstation/
// hokan.py (HkD-4) — the firmware only counts steps and fires the fall SOS.

#include <stdint.h>
#include "AizuCore.h"

// Standard gravity (m/s²). accel_mag ≈ HOKAN_G at rest; walking oscillates around it,
// a fall dips toward 0 (freefall) then spikes (impact).
static const float HOKAN_G = 9.80665f;

// ── Step detector tunables ──────────────────────────────────────────────────
// A step is a rhythmic |g| oscillation ~1.5–2.5 Hz. We track a slow baseline
// (gravity, ~1 g) and count one step per peak-then-return crossing, debounced by a
// minimum inter-step interval so jitter and double-bounces don't over-count.
static const float    HOKAN_STEP_DELTA    = 2.2f;   // m/s² a peak must clear the baseline by (~0.22 g)
static const float    HOKAN_BASE_ALPHA    = 0.02f;  // EMA weight for the gravity baseline (slow)
static const uint32_t HOKAN_STEP_MIN_MS   = 250;    // min interval between steps (=> <= 4 steps/s)

// ── Fall detector tunables ──────────────────────────────────────────────────
// The classic signature: freefall (|g| dips low) -> impact (|g| spikes) -> post-
// impact stillness (|g| back near 1 g and steady). Confirm only on all three, so
// normal walking/sitting doesn't false-trigger (AC-2).
static const float    HOKAN_FREEFALL_TH   = 0.60f * HOKAN_G;  // below => in freefall (~5.9 m/s²)
static const float    HOKAN_IMPACT_TH     = 2.30f * HOKAN_G;  // above => impact spike (~22.6 m/s²)
static const float    HOKAN_STILL_BAND    = 2.50f;            // |mag - G| < this => "still" (lying at rest)
static const uint32_t HOKAN_FALL_WIN_MS   = 800;    // impact must follow freefall within this window
static const uint32_t HOKAN_STILL_HOLD_MS = 800;    // post-impact stillness to CONFIRM (=> ~1 s total)
static const uint32_t HOKAN_IMPACT_TO_MS  = 3000;   // give up if no stillness within this of impact
static const float    HOKAN_RESUME_DELTA  = 0.40f * HOKAN_G;  // motion this far from 1 g => moving again
static const uint32_t HOKAN_RESUME_HOLD_MS = 1500;  // sustained motion to RESOLVE a latched fall

// ── Aizu fall-SOS cue ─────────────────────────────────────────────────────────
// Red urgent pulse at the top-tier ALERT rung (AZ-11 — co-critical with Kehai
// Reflex; aizuIsAlert => preempts with no debounce). The sketch latches it (keeps
// re-posting) from CONFIRMED until RESOLVED.
static const uint16_t   HOKAN_SOS_PULSE_MS = 400;   // urgent — the frantic voice, unlike Kanki's slow one

// ── Step detector ─────────────────────────────────────────────────────────────
// Feed it accel_mag (m/s²) + a timestamp (ms) each sample; update() returns true on
// the sample where a step is counted. Pure state — no globals, no hardware — so a
// test can drive it deterministically.
static const uint32_t HOKAN_CADENCE_IDLE_MS = 2000;  // no step within this => cadence reads 0 (stopped)

struct HokanStepDetector {
  float    baseline = 0.0f;   // EMA of |g| (gravity ~1 g)
  bool     seeded   = false;
  bool     above    = false;  // currently above the peak threshold this oscillation
  uint32_t lastStepMs = 0;
  bool     haveStep = false;  // a step has been counted (so lastStepMs is meaningful)
  float    emaStepMs = 0.0f;  // EMA of the inter-step interval (for cadence)

  bool update(float mag, uint32_t tMs) {
    if (!seeded) { baseline = mag; seeded = true; }
    baseline += HOKAN_BASE_ALPHA * (mag - baseline);   // slow-track gravity

    bool stepped = false;
    if (!above && mag > baseline + HOKAN_STEP_DELTA) {
      above = true;                                    // rising into a peak
    } else if (above && mag < baseline) {
      above = false;                                   // fell back through the baseline — one oscillation
      if (!haveStep || (uint32_t)(tMs - lastStepMs) >= HOKAN_STEP_MIN_MS) {
        if (haveStep) {                                // track cadence from the interval to the prior step
          float interval = (float)(uint32_t)(tMs - lastStepMs);
          emaStepMs = (emaStepMs <= 0.0f) ? interval : emaStepMs * 0.7f + interval * 0.3f;
        }
        lastStepMs = tMs;
        haveStep   = true;
        stepped    = true;
      }
    }
    return stepped;
  }

  // Cadence in steps/min from the smoothed inter-step interval; 0 when stopped
  // (no step within HOKAN_CADENCE_IDLE_MS) or not enough steps to measure yet.
  uint16_t cadenceSpm(uint32_t now) const {
    if (!haveStep || emaStepMs <= 0.0f) return 0;
    if ((uint32_t)(now - lastStepMs) > HOKAN_CADENCE_IDLE_MS) return 0;
    return (uint16_t)(60000.0f / emaStepMs + 0.5f);
  }
};

// ── Fall detector ─────────────────────────────────────────────────────────────
enum HokanFallEvent {
  HOKAN_FALL_NONE = 0,   // nothing this sample
  HOKAN_FALL_CONFIRMED,  // freefall -> impact -> stillness all matched: post the SOS
  HOKAN_FALL_RESOLVED    // wearer moving again after a latched fall: clear the SOS
};

enum HokanFallState {
  HOKAN_NORMAL = 0,   // walking/standing/sitting — watching for a freefall dip
  HOKAN_FREEFALL,     // |g| dipped low — watching for an impact spike
  HOKAN_IMPACT,       // impact seen — accumulating post-impact stillness
  HOKAN_DOWN          // confirmed + latched — watching for the wearer to get up
};

struct HokanFallDetector {
  HokanFallState state = HOKAN_NORMAL;
  uint32_t tFreefall = 0;   // when freefall began
  uint32_t tImpact   = 0;   // when the impact spike hit
  uint32_t tStill    = 0;   // when the current stillness run began (0 = not still)
  uint32_t tMoving   = 0;   // when the current post-fall motion run began (0 = not moving)

  HokanFallEvent update(float mag, uint32_t tMs) {
    switch (state) {
      case HOKAN_NORMAL:
        if (mag < HOKAN_FREEFALL_TH) { state = HOKAN_FREEFALL; tFreefall = tMs; }
        break;

      case HOKAN_FREEFALL:
        if (mag > HOKAN_IMPACT_TH) {          // freefall -> impact
          state = HOKAN_IMPACT; tImpact = tMs; tStill = 0;
        } else if ((uint32_t)(tMs - tFreefall) > HOKAN_FALL_WIN_MS) {
          state = HOKAN_NORMAL;               // a dip with no impact — not a fall
        }
        break;

      case HOKAN_IMPACT: {
        bool still = (mag > HOKAN_G - HOKAN_STILL_BAND) && (mag < HOKAN_G + HOKAN_STILL_BAND);
        if (still) {
          if (tStill == 0) tStill = tMs;
          if ((uint32_t)(tMs - tStill) >= HOKAN_STILL_HOLD_MS) {
            state = HOKAN_DOWN; tMoving = 0;
            return HOKAN_FALL_CONFIRMED;        // all three matched
          }
        } else {
          tStill = 0;                           // motion broke the stillness run
          if ((uint32_t)(tMs - tImpact) > HOKAN_IMPACT_TO_MS)
            state = HOKAN_NORMAL;               // never settled — give up (not a clean fall)
        }
        break;
      }

      case HOKAN_DOWN: {
        // Latched. The wearer getting up (sustained motion away from 1 g) resolves it.
        bool moving = (mag < HOKAN_G - HOKAN_RESUME_DELTA) || (mag > HOKAN_G + HOKAN_RESUME_DELTA);
        if (moving) {
          if (tMoving == 0) tMoving = tMs;
          if ((uint32_t)(tMs - tMoving) >= HOKAN_RESUME_HOLD_MS) {
            state = HOKAN_NORMAL;
            return HOKAN_FALL_RESOLVED;
          }
        } else {
          tMoving = 0;
        }
        break;
      }
    }
    return HOKAN_FALL_NONE;
  }

  bool down() const { return state == HOKAN_DOWN; }
};

// The Aizu cue Hokan re-posts while a fall is latched: red urgent PULSE at the
// top-tier SOS rung (AZ-11). Kept as a helper so the sketch never hardcodes the hue.
struct HokanCue {
  int        priority;
  AizuColour colour;
  AizuMotion motion;
};

static inline HokanCue hokanFallCue() {
  HokanCue c;
  c.priority = AIZU_PRIO_HOKAN_FALL_SOS;
  c.colour   = (AizuColour){255, 0, 0};             // red — emergency
  c.motion   = (AizuMotion){AIZU_PULSE, HOKAN_SOS_PULSE_MS};
  return c;
}

#endif  // HOKAN_DSP_H
