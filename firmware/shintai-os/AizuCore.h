#ifndef AIZU_CORE_H
#define AIZU_CORE_H

// AizuCore — the pure, hardware-free heart of Aizu (specs/platform/aizu.md).
//
// No Arduino, no NeoPixel, no millis(): just the cue data model, the arbitration
// decision, and the motion/brightness math. Keeping "the math" (AZ-6) in one
// Arduino-free place is what makes the arbiter host-testable
// (tools/aizu-arbiter-test.cpp) and independent of any output sink (AZ-5).
//
// The stateful parts — cue slots, the render clock, anti-flicker dwell, the
// button — live in Aizu.cpp; everything decidable from values alone lives here.

#include <stdint.h>
#include <math.h>

static const float AIZU_TWO_PI = 6.28318530717958647692f;

// Sources that may post a cue. Adding a source is a one-row change (AZ-1, AC-10):
// an enum row here + a priority constant below + a postCue() call in the source.
enum AizuSource {
  AIZU_KEHAI = 0,   // proximity reflex / approach
  AIZU_KANKI,       // air-quality CO2 bands
  AIZU_NESSHI,      // hold-to-measure temperature read
  AIZU_HOKAN,       // fall-detection SOS
  AIZU_SYSTEM,      // reserved for host-level cues
  AIZU_SOURCE_COUNT
};

// v1 priority ladder (higher wins). Coarsely ALERT > INTERACTIVE > AMBIENT — the
// rungs of the source table in specs/platform/aizu.md#arbitration, so a source
// posts a self-documenting priority rather than a bare number.
enum {
  AIZU_PRIO_KEHAI_REFLEX   = 100,  // rank 1 — imminent collision            (ALERT)
  AIZU_PRIO_HOKAN_FALL_SOS =  90,  // rank 2 — a fall just happened, latched (ALERT)
  AIZU_PRIO_NESSHI_HELD    =  80,  // rank 3 — deliberate read while held    (INTERACTIVE)
  AIZU_PRIO_KANKI_BAD      =  70,  // rank 4 — dangerous air (>=2000 ppm)    (ALERT)
  AIZU_PRIO_KEHAI_APPROACH =  60,  // rank 5 — something approaching         (AMBIENT+)
  AIZU_PRIO_KANKI_POOR     =  50,  // rank 6 — open a window (1200-2000)     (AMBIENT)
  AIZU_PRIO_KANKI_STUFFY   =  40,  // rank 7 — ventilation slipping (800-1200) (AMBIENT)
  AIZU_PRIO_IDLE           =  -1   // nothing live -> Idle wallpaper
};

struct AizuColour {
  uint8_t r, g, b;
};

enum AizuMotionKind {
  AIZU_STEADY = 0,   // constant at brightness
  AIZU_BREATHE,      // smooth sine fade 0<->peak
  AIZU_PULSE,        // sharp on / dim, at rate
  AIZU_SOLID,        // peak, no motion
  AIZU_HEARTBEAT     // one brief blink per interval
};

struct AizuMotion {
  AizuMotionKind kind;
  uint16_t periodMs;   // BREATHE/PULSE period; HEARTBEAT interval; ignored for STEADY/SOLID
};

// One posted cue. active=false means the slot is empty (source has no cue).
// postedAtMs + maxAgeMs implement liveness: a cue not refreshed within maxAgeMs
// is dropped (staleness backstop, AC-8). maxAgeMs==0 means "never expires".
struct AizuCue {
  bool          active;
  AizuSource    source;
  int           priority;
  AizuColour    colour;
  AizuMotion    motion;
  uint32_t      maxAgeMs;
  uint32_t      postedAtMs;
};

// ALERT-class rungs preempt with no debounce (see aizuShouldSwitch). Everything
// else (INTERACTIVE, AMBIENT, Idle) debounces on the way down.
static inline bool aizuIsAlert(int priority) {
  return priority == AIZU_PRIO_KEHAI_REFLEX
      || priority == AIZU_PRIO_HOKAN_FALL_SOS
      || priority == AIZU_PRIO_KANKI_BAD;
}

// Is a cue still live at nowMs? Inactive slots are never live; maxAgeMs==0 never
// expires; otherwise it must have been refreshed within maxAgeMs. Unsigned
// subtraction is rollover-safe across the millis() wrap.
static inline bool aizuCueLive(const AizuCue& c, uint32_t nowMs) {
  if (!c.active) return false;
  if (c.maxAgeMs == 0) return true;
  return (uint32_t)(nowMs - c.postedAtMs) <= c.maxAgeMs;
}

// Pick the winning slot: highest-priority LIVE cue. Returns -1 if none live
// (caller renders Idle). Ties resolve to the lower index (stable).
static inline int aizuPickWinner(const AizuCue* cues, int count, uint32_t nowMs) {
  int best = -1;
  int bestPrio = AIZU_PRIO_IDLE;
  for (int i = 0; i < count; i++) {
    if (!aizuCueLive(cues[i], nowMs)) continue;
    if (best < 0 || cues[i].priority > bestPrio) {
      best = i;
      bestPrio = cues[i].priority;
    }
  }
  return best;
}

// Anti-flicker: should the shown winner switch to a new candidate? Upward
// preempt is instant (an ALERT never waits); a change to a different, not-higher
// winner must first outlast the debounce dwell so two near-equal cues flipping a
// boundary don't strobe. A source refreshing its own slot always applies.
// shownPrio should be AIZU_PRIO_IDLE when nothing live is currently shown, so a
// release back to a lower cue counts as upward (instant, AC-3).
static inline bool aizuShouldSwitch(int shownPrio, int candPrio, bool sameSource,
                                    uint32_t msSinceSwitch, uint32_t debounceMs) {
  if (sameSource) return true;            // same voice updating itself
  if (candPrio > shownPrio) return true;  // upward preempt — instant
  return msSinceSwitch >= debounceMs;     // downward / near-equal — dwell first
}

// Motion envelope: instantaneous brightness fraction [0,1] for a motion at
// phaseMs into its cycle. Pure in (kind, period, phase); gamma/cap applied
// separately by aizuLevel so this stays deterministic and unit-testable.
static inline float aizuEnvelope(AizuMotionKind kind, uint16_t periodMs, uint32_t phaseMs) {
  switch (kind) {
    case AIZU_STEADY:
    case AIZU_SOLID:
      return 1.0f;
    case AIZU_BREATHE: {
      if (periodMs == 0) return 1.0f;
      float ph = (float)(phaseMs % periodMs) / (float)periodMs;   // 0..1
      return 0.5f - 0.5f * cosf(AIZU_TWO_PI * ph);                // smooth 0<->1<->0
    }
    case AIZU_PULSE: {
      if (periodMs == 0) return 1.0f;
      float ph = (float)(phaseMs % periodMs) / (float)periodMs;
      return ph < 0.40f ? 1.0f : 0.12f;   // sharp on ~40% of the cycle, then a dim floor
    }
    case AIZU_HEARTBEAT: {
      if (periodMs == 0) return 0.0f;
      return (phaseMs % periodMs) < 120u ? 1.0f : 0.0f;   // one ~120 ms blink per interval
    }
  }
  return 0.0f;
}

// Resolve an envelope fraction to a 0..255 master brightness: gamma-correct so a
// breathe reads perceptually linear, then clamp to cap (AIZU_MAX_BRIGHT, or the
// dimmer idle cap). Idle brightness therefore also obeys the cap.
static inline uint8_t aizuLevel(float envelope, uint8_t cap) {
  if (envelope <= 0.0f) return 0;
  if (envelope > 1.0f) envelope = 1.0f;
  float v = powf(envelope, 2.2f) * (float)cap;   // perceptual gamma
  if (v > 255.0f) v = 255.0f;
  return (uint8_t)(v + 0.5f);
}

// Scale a full-range hue by a 0..255 master level -> the RGB actually written.
static inline AizuColour aizuScaleColour(AizuColour hue, uint8_t level) {
  AizuColour out;
  out.r = (uint8_t)(((uint16_t)hue.r * level) / 255);
  out.g = (uint8_t)(((uint16_t)hue.g * level) / 255);
  out.b = (uint8_t)(((uint16_t)hue.b * level) / 255);
  return out;
}

#endif  // AIZU_CORE_H
