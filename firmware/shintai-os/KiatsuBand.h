#ifndef KIATSU_BAND_H
#define KIATSU_BAND_H

// KiatsuBand — the pure, hardware-free weather-tendency mapping for Kiatsu (気圧)
// (specs/zokyo/kiatsu.md). No sensor, no NeoPixel, no millis(): it turns a slow
// stream of BME688 barometric pressure (bmePressure, hPa) into the calm cyan Aizu
// cue Kiatsu should post when the sky is turning. Keeping it Arduino-free makes the
// ring buffer, the tendency subtraction, and the state hysteresis host-testable
// (tools/kiatsu-band-test.cpp); the sketch owns the ~45 s sub-sample clock (millis)
// and the postCue call.
//
// The thrifty core (KiD-1): pressure carries two signals at OPPOSITE timescales,
// and Kiatsu splits them by surface. The FAST one — storey-scale floor steps — is
// slower than the 1.5 s log, so it survives untouched in the CSV and is
// reconstructed BASE-SIDE (groundstation/kiatsu.py); nothing about it lives here.
// The SLOW one — the 3-hour weather trend — is a subtraction over a ring buffer,
// the calmest possible on-device computation and the OPPOSITE of Hokan's real-time
// DSP. Δ = p_now − p_3h_ago; a falling barometer precedes a storm by hours, the
// oldest forecast there is. Cyan is the source identity (KiD-5): Kanki owns the
// green→red air ramp, Kyūkaku violet, Kiatsu cyan (weather / sky) — one more
// source-owned hue on the shared pixel. No calibration: the trend is a difference,
// so it needs no reference (KiD-3). Temperature correction (KiD-2) is only for the
// pressure→altitude floor math, which is base-side — the pure weather trend here is
// dimensionless pressure change and needs no temperature term.

#include <stdint.h>
#include "AizuCore.h"

// Ring geometry. WX_WINDOW_H = 3 h sampled every ~45 s => 240 slots (~960 B of
// float, trivial RAM). The tendency is only reported once the ring SPANS the full
// window (all slots valid) — before that Kiatsu is quiet, the same "settle before
// arming" posture as Kyūkaku's burn-in, but here the window is the weather's own
// 3-hour timescale. The sketch gates pushes on KIATSU_SUBSAMPLE_MS.
static const int      KIATSU_RING_N       = 240;    // 3 h / 45 s (KiD-6, WX_WINDOW_H)
static const uint32_t KIATSU_SUBSAMPLE_MS = 45000;  // ~45 s between ring samples (KiD-6)

// Edge averaging: p_now and p_3h_ago are each a mean of the K nearest samples
// (K ≈ 3 min at the 45 s cadence), not lone samples, so a single anomalous reading
// — a door slam, a lift's pressure pop, sensor noise — can't false-trip the trend
// (AC-4: "short pressure wobbles do not trip the weather cue"). K is negligible
// against the 3 h window, so the trend keeps its weather timescale.
static const int KIATSU_EDGE_AVG = 4;

// Tendency thresholds in hPa (KiD-6, tune on-site). Δ is p_now − p_3h_ago, so a
// FALLING barometer is negative. Steady/rising posts nothing (-> Aizu Idle);
// falling past FALL_HPA is a weather-turn; past STORM_HPA a front is building.
static const float KIATSU_WX_FALL_HPA  = 1.0f;   // Δ ≤ −1.0 hPa/3 h -> Falling
static const float KIATSU_WX_STORM_HPA = 3.0f;   // Δ ≤ −3.0 hPa/3 h -> Falling fast
static const float KIATSU_WX_HYST_HPA  = 0.3f;   // hysteresis at each edge (no flip-flop)

// Motion periods (ms). These are the SLOWEST breathes in the whole system — Kiatsu
// is the longest-timescale, calmest cue (AZ-13), the opposite pole from a Reflex.
// A building front breathes slower and deeper than a gentle fall.
static const uint16_t KIATSU_FALL_BREATHE_MS  = 5000;   // gentle slow cyan breathe
static const uint16_t KIATSU_STORM_BREATHE_MS = 7000;   // slower, deeper — front building

// Cue hues (KiD-5). Cyan is the source identity (weather / sky); the storm cue
// leans DEEPER cyan (bluer, less green). Neither is green (Idle), Kanki's amber/red,
// nor Kyūkaku's violet — the palette stays legible on the one shared pixel.
static const AizuColour KIATSU_CYAN      = {  0, 190, 255};  // Falling — weather turning
static const AizuColour KIATSU_CYAN_DEEP = {  0, 110, 255};  // Falling fast — front building

// Weather states, ascending severity as Δ FALLS. Steady is quiescent (posts nothing
// -> Aizu Idle green, == "the sky is as it was").
enum KiatsuWx {
  KIATSU_WX_STEADY  = 0,   // Δ ≥ −FALL_HPA — steady or rising
  KIATSU_WX_FALLING = 1,   // −STORM < Δ < −FALL — weather turning, hours out
  KIATSU_WX_STORM   = 2    // Δ ≤ −STORM — a front / storm building
};

// The slow pressure ring (owned by the sketch; mutated by kiatsuPush). count < N
// means "still filling" (Kiatsu quiet); state is the hysteretic weather state.
struct KiatsuState {
  float ring[KIATSU_RING_N];   // circular buffer of sub-sampled pressure (hPa)
  int   head;                  // next write index
  int   count;                 // valid samples so far (caps at KIATSU_RING_N)
  int   state;                 // hysteretic weather state (KiatsuWx)
};

// Instantaneous weather state from a tendency Δ with NO hysteresis — seeds the walk
// from the unseeded sentinel. More-negative Δ = worse = higher state.
static inline int kiatsuRawState(float delta) {
  if (delta <= -KIATSU_WX_STORM_HPA) return KIATSU_WX_STORM;
  if (delta <= -KIATSU_WX_FALL_HPA)  return KIATSU_WX_FALLING;
  return KIATSU_WX_STEADY;
}

// Advance the hysteretic weather state (mirror of kyukakuStep, for a falling Δ).
// Severity RISES only once Δ drops an extra HYST below an onset edge, and FALLS only
// once Δ recovers an extra HYST above it — the dead zone that stops boundary
// flip-flop. `prev` outside [STEADY, STORM] (the -1 sentinel) snaps via kiatsuRawState.
static inline int kiatsuStateStep(float delta, int prev) {
  if (prev < KIATSU_WX_STEADY || prev > KIATSU_WX_STORM)
    return kiatsuRawState(delta);
  const float edge[2] = { KIATSU_WX_FALL_HPA, KIATSU_WX_STORM_HPA };  // STEADY|FALLING, FALLING|STORM
  int s = prev;
  while (s < KIATSU_WX_STORM  && delta < -(edge[s]     + KIATSU_WX_HYST_HPA)) s++;  // worsen
  while (s > KIATSU_WX_STEADY && delta > -(edge[s - 1] - KIATSU_WX_HYST_HPA)) s--;  // recover
  return s;
}

// Mean of the K newest samples in the ring (p_now). Assumes count >= K (the caller
// only computes a tendency once the ring is full, so count == KIATSU_RING_N >> K).
static inline float kiatsuAvgNewest(const KiatsuState& s, int k) {
  float sum = 0.0f;
  for (int i = 0; i < k; i++)
    sum += s.ring[(s.head - 1 - i + KIATSU_RING_N) % KIATSU_RING_N];
  return sum / (float)k;
}

// Mean of the K oldest samples in the ring (p_3h_ago). When full, head points at the
// slot about to be overwritten — i.e. the oldest sample — so oldest runs forward from head.
static inline float kiatsuAvgOldest(const KiatsuState& s, int k) {
  float sum = 0.0f;
  for (int i = 0; i < k; i++)
    sum += s.ring[(s.head + i) % KIATSU_RING_N];
  return sum / (float)k;
}

// What one sub-sample resolves to. `spanning` gates posting (ring not yet 3 h full
// -> post nothing); `delta` is the tendency (hPa, valid iff spanning); `state` is
// the resolved hysteretic weather state.
struct KiatsuObs {
  bool  spanning;
  float delta;
  int   state;
};

// Push one pressure sample into the ring and report the weather tendency. Pure but
// stateful (mutates `s`) — no millis; the sketch owns the ~45 s sub-sample clock.
// While the ring is still filling toward 3 h, spanning=false (Kiatsu stays quiet).
// Once full, Δ = mean(newest K) − mean(oldest K) drives the hysteretic state.
static inline KiatsuObs kiatsuPush(KiatsuState& s, float pressure) {
  s.ring[s.head] = pressure;
  s.head = (s.head + 1) % KIATSU_RING_N;
  if (s.count < KIATSU_RING_N) s.count++;

  KiatsuObs ob = {false, 0.0f, s.state};
  if (s.count >= KIATSU_RING_N) {                 // ring spans the full 3 h window
    ob.delta = kiatsuAvgNewest(s, KIATSU_EDGE_AVG) - kiatsuAvgOldest(s, KIATSU_EDGE_AVG);
    s.state = kiatsuStateStep(ob.delta, s.state);
    ob.state = s.state;
    ob.spanning = true;
  }
  return ob;
}

// The cue Kiatsu posts. post=false means clearCue(KIATSU) (Steady -> quiescent ->
// Aizu Idle). Falling / Falling-fast are cyan breathes at the lowest ambient rung
// (AZ-13) — never an alert, always the first thing preempted.
struct KiatsuCue {
  bool       post;
  int        priority;
  AizuColour colour;
  AizuMotion motion;
};

static inline KiatsuCue kiatsuCueFor(int state) {
  KiatsuCue c = {false, AIZU_PRIO_IDLE, {0, 0, 0}, {AIZU_STEADY, 0}};
  switch (state) {
    case KIATSU_WX_STORM:
      c.post = true; c.priority = AIZU_PRIO_KIATSU_WX;
      c.colour = KIATSU_CYAN_DEEP; c.motion = {AIZU_BREATHE, KIATSU_STORM_BREATHE_MS};
      break;
    case KIATSU_WX_FALLING:
      c.post = true; c.priority = AIZU_PRIO_KIATSU_WX;
      c.colour = KIATSU_CYAN; c.motion = {AIZU_BREATHE, KIATSU_FALL_BREATHE_MS};
      break;
    case KIATSU_WX_STEADY:
    default:
      c.post = false;   // quiescent — Aizu renders Idle green (== "the sky is as it was")
      break;
  }
  return c;
}

#endif  // KIATSU_BAND_H
