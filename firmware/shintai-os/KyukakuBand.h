#ifndef KYUKAKU_BAND_H
#define KYUKAKU_BAND_H

// KyukakuBand — the pure, hardware-free gas->cue mapping for Kyūkaku (嗅覚)
// (specs/zokyo/kyukaku.md). No sensor, no NeoPixel, no millis(): it turns the
// BME688 gas resistance (bmeGas, ohms) + humidity (bmeHum) into the Aizu cue
// Kyūkaku should post. Keeping it Arduino-free makes the baseline tracking, the
// spike detector, and the band hysteresis host-testable
// (tools/kyukaku-band-test.cpp); the sketch supplies the live reading, the
// settle/spike-hold timing (millis), and the postCue call.
//
// The thrifty core (KY-1): a MOX resistance is drift-garbage in ABSOLUTE terms,
// so Kyūkaku never reads raw ohms. It tracks an adaptive clean-air BASELINE (R0)
// and works in the ratio r = gas/R0 (~1 in clean air, falling toward 0 as VOCs
// rise). Two behaviours on two timescales, both dimensionless — no calibration:
//   * Spike (KY-2): a FAST drop of r below a medium reference (rRef) = "something
//     just entered the air" — a transient ALERT that preempts, then decays.
//   * Foul/Taint: r held low = loaded air — a calm violet AMBIENT, Kanki-style.
// Violet is the identity (KY-3): Kanki owns the green->red air ramp, Kyūkaku owns
// violet (-> red only at a Spike peak), so two air-senses stay legible on one
// pixel. Humidity is the free confound veto (KY-4): a gas drop that coincides
// with a humidity jump is the BME's own RH, not a smell — the same reading gives
// both. Settling (KY-5) is the sketch's job (arm only after ~120 s of baseline).

#include <stdint.h>
#include "AizuCore.h"

// Ratio-space band edges (KY-6, tune on-wrist). r >= TAINT_R is quiescent ("the
// air is as it was" — Aizu Idle); below TAINT_R a smell is present; below FOUL_R
// it's heavy. The spec's 0.85 "Clean" mark is simply well above the 0.60 onset —
// small dips are noise, only a real drop earns a cue (the Spike catches onsets).
static const float KYUKAKU_TAINT_R = 0.60f;   // r < this (>= FOUL_R) -> Taint (mild)
static const float KYUKAKU_FOUL_R  = 0.35f;   // r < this              -> Foul (loaded)
static const float KYUKAKU_HYST_R  = 0.05f;   // ratio hysteresis at each band edge

// Spike: r drops by >= SPIKE_DROP below the medium reference rRef, and the drop is
// NOT explained by a humidity rise of >= HUM_VETO %RH since the last reading (KY-4).
static const float KYUKAKU_SPIKE_DROP = 0.25f;
static const float KYUKAKU_HUM_VETO   = 3.0f;   // %RH rise between readings that vetoes

// Baseline (R0) tracking — asymmetric EMA. During settle it converges fast to seed
// R0; once seeded it rises moderately toward cleaner air but decays DOWNWARD very
// slowly, so a lingering smell can't drag the baseline down and hide itself.
static const float KYUKAKU_SEED_ALPHA = 0.10f;   // fast convergence while settling
static const float KYUKAKU_BASE_UP    = 0.02f;   // rise toward cleaner air
static const float KYUKAKU_BASE_DOWN  = 0.0006f; // very slow decay (smell can't move it)

// rRef — a medium EMA of the ratio (~average of the last ~4 readings ≈ a ~6 s
// window at the 1.5 s reading cadence). The Spike fires on (rRef - r); after an
// event persists, rRef catches down to r and the Spike decays into Foul/Taint.
static const float KYUKAKU_RREF_ALPHA = 0.25f;

// Settle / burn-in (KY-5): readings to seed the baseline before arming. At the
// ~1.5 s BME cadence, ~80 readings ≈ 120 s. Below this the sketch posts nothing.
static const long KYUKAKU_SETTLE_COUNT = 80;

// Motion periods (ms). The Spike is the one FAST motion in Kyūkaku's vocabulary —
// the nose startling; Foul/Taint are slow breathes (calm, Kanki-like), Foul the
// more insistent (shorter) of the two.
static const uint16_t KYUKAKU_SPIKE_PULSE_MS  = 500;    // fast pulse — the startle
static const uint16_t KYUKAKU_FOUL_BREATHE_MS = 2500;   // strong slow breathe
static const uint16_t KYUKAKU_TAINT_BREATHE_MS = 4000;  // gentler slow breathe

// Cue hues (KY-3). Violet is the source identity; the Spike leans hot violet->red
// at its peak. Neither is green (Idle/all-clear) or Kanki's amber/orange/red.
static const AizuColour KYUKAKU_VIOLET  = {170,   0, 255};  // Foul/Taint identity
static const AizuColour KYUKAKU_SPIKE_C = {255,   0,  80};  // Spike — hot violet->red

// Ambient bands, ascending severity as r FALLS. Clean is quiescent (posts nothing
// -> Aizu Idle green, == "the air is as it was").
enum KyukakuBand {
  KYUKAKU_BAND_CLEAN = 0,
  KYUKAKU_BAND_TAINT = 1,
  KYUKAKU_BAND_FOUL  = 2
};

// Live rolling state (owned by the sketch; mutated by kyukakuStepState). baseline
// == 0 and count == 0 mean "unseeded" (first reading seeds it).
struct KyukakuState {
  float baseline;   // R0 (ohms) — adaptive clean-air ceiling
  float rRef;       // medium EMA of the ratio (spike reference)
  float lastHum;    // %RH at the previous reading (humidity veto)
  float lastRatio;  // last ratio r (observability / human debug)
  int   band;       // hysteretic ambient band
  long  count;      // readings since boot (settle counter)
};

// Instantaneous band from r with NO hysteresis — seeds the walk from the unseeded
// sentinel. Lower r = worse air = higher band.
static inline int kyukakuRawBand(float r) {
  if (r < KYUKAKU_FOUL_R)  return KYUKAKU_BAND_FOUL;
  if (r < KYUKAKU_TAINT_R) return KYUKAKU_BAND_TAINT;
  return KYUKAKU_BAND_CLEAN;
}

// Advance the hysteretic band (mirror of kankiStep, inverted for a falling signal).
// Severity RISES only once r drops an extra HYST below an onset edge, and FALLS only
// once r recovers an extra HYST above it — the dead zone that stops edge flip-flop.
// `prev` outside [CLEAN, FOUL] (the -1 unseeded sentinel) snaps via kyukakuRawBand.
static inline int kyukakuStep(float r, int prev) {
  if (prev < KYUKAKU_BAND_CLEAN || prev > KYUKAKU_BAND_FOUL)
    return kyukakuRawBand(r);
  const float edge[2] = { KYUKAKU_TAINT_R, KYUKAKU_FOUL_R };  // CLEAN|TAINT, TAINT|FOUL onsets
  int b = prev;
  while (b < KYUKAKU_BAND_FOUL  && r <  edge[b]     - KYUKAKU_HYST_R) b++;  // worsen
  while (b > KYUKAKU_BAND_CLEAN && r >  edge[b - 1] + KYUKAKU_HYST_R) b--;  // recover
  return b;
}

// Spike test: a drop of >= SPIKE_DROP below the reference, not explained by a
// humidity rise of >= HUM_VETO. Pure in (rRef, r, humRise) for direct testing.
static inline bool kyukakuIsSpike(float rRef, float r, float humRise) {
  return ((rRef - r) >= KYUKAKU_SPIKE_DROP) && (humRise < KYUKAKU_HUM_VETO);
}

// One asymmetric-EMA baseline step (KY-1): fast while settling; once seeded, rise
// moderate / decay very slow so a smell can't pull R0 down after itself.
static inline float kyukakuBaselineStep(float baseline, float gas, bool seeded) {
  if (!seeded) return baseline + KYUKAKU_SEED_ALPHA * (gas - baseline);
  float a = (gas > baseline) ? KYUKAKU_BASE_UP : KYUKAKU_BASE_DOWN;
  return baseline + a * (gas - baseline);
}

// What one reading resolves to. `seeded` gates posting (settling -> post nothing);
// `spike` is a fresh onset THIS reading (the sketch latches a short hold on it);
// `band` is the resolved ambient band the cue decays to.
struct KyukakuObs {
  bool  seeded;
  bool  spike;
  int   band;
  float ratio;
};

// Fold one BME reading (gas ohms, hum %RH) into the rolling state and report what
// it means. Pure but stateful (mutates `s`) — no millis; the sketch owns the clock.
// First reading seeds R0 and returns seeded=false. Spike is computed against the
// PRE-update rRef (so a sudden drop this reading is caught), then rRef is advanced.
static inline KyukakuObs kyukakuStepState(KyukakuState& s, float gas, float hum) {
  KyukakuObs ob = {false, false, KYUKAKU_BAND_CLEAN, 1.0f};
  if (gas <= 0.0f) {                       // no/invalid reading — hold, don't disturb R0
    ob.band = (s.band < KYUKAKU_BAND_CLEAN) ? KYUKAKU_BAND_CLEAN : s.band;
    ob.seeded = (s.count >= KYUKAKU_SETTLE_COUNT);
    return ob;
  }
  if (s.count <= 0) {                       // first reading — seed the baseline
    s.baseline = gas; s.rRef = 1.0f; s.lastHum = hum; s.lastRatio = 1.0f;
    s.band = KYUKAKU_BAND_CLEAN; s.count = 1;
    return ob;                              // seeded=false: settling
  }
  bool seeded = (s.count >= KYUKAKU_SETTLE_COUNT);
  s.baseline = kyukakuBaselineStep(s.baseline, gas, seeded);
  float r = gas / s.baseline;
  bool spike = seeded && kyukakuIsSpike(s.rRef, r, hum - s.lastHum);
  s.rRef += KYUKAKU_RREF_ALPHA * (r - s.rRef);
  s.band = kyukakuStep(r, s.band);
  s.lastHum = hum; s.lastRatio = r;
  if (s.count < 0x3fffffffL) s.count++;
  ob.seeded = seeded; ob.spike = spike; ob.band = s.band; ob.ratio = r;
  return ob;
}

// The cue Kyūkaku posts. post=false means clearCue(KYUKAKU) (Clean -> quiescent ->
// Aizu Idle). Spike is a fast violet->red PULSE at ALERT-class priority (preempts
// without debounce); Foul/Taint are violet breathes told apart from Kanki by hue.
struct KyukakuCue {
  bool       post;
  int        priority;
  AizuColour colour;
  AizuMotion motion;
};

// The transient onset cue (KY-2): the sketch posts this for a short hold after a
// spike, then falls back to kyukakuCueFor(band).
static inline KyukakuCue kyukakuSpikeCue() {
  KyukakuCue c = {true, AIZU_PRIO_KYUKAKU_SPIKE, KYUKAKU_SPIKE_C,
                  {AIZU_PULSE, KYUKAKU_SPIKE_PULSE_MS}};
  return c;
}

static inline KyukakuCue kyukakuCueFor(int band) {
  KyukakuCue c = {false, AIZU_PRIO_IDLE, {0, 0, 0}, {AIZU_STEADY, 0}};
  switch (band) {
    case KYUKAKU_BAND_FOUL:
      c.post = true; c.priority = AIZU_PRIO_KYUKAKU_FOUL;
      c.colour = KYUKAKU_VIOLET; c.motion = {AIZU_BREATHE, KYUKAKU_FOUL_BREATHE_MS};
      break;
    case KYUKAKU_BAND_TAINT:
      c.post = true; c.priority = AIZU_PRIO_KYUKAKU_TAINT;
      c.colour = KYUKAKU_VIOLET; c.motion = {AIZU_BREATHE, KYUKAKU_TAINT_BREATHE_MS};
      break;
    case KYUKAKU_BAND_CLEAN:
    default:
      c.post = false;   // quiescent — Aizu renders Idle green (== "the air is as it was")
      break;
  }
  return c;
}

#endif  // KYUKAKU_BAND_H
