// Host unit tests for Kiatsu's pure pressure->cue mapping (KiatsuBand.h).
//
// Same posture as kyukaku-band-test.cpp: the firmware has no on-target harness, so the
// decidable-from-values logic — the weather-state bands + hysteresis, the ring buffer's
// fill/span gate, the edge-averaged tendency subtraction, and the cue each state posts —
// is checked on the host with no board. The ~45 s sub-sample clock and the postCue call
// stay in the sketch.
//
//   c++ -std=c++17 -Wall -o /tmp/kiatsu-test tools/kiatsu-band-test.cpp && /tmp/kiatsu-test
//
// Thresholds mirror KiD-6: Steady Δ > −1.0, Falling −3.0 < Δ ≤ −1.0, Falling-fast Δ ≤ −3.0
// hPa over the 3 h window; hysteresis 0.3 hPa at each edge.

#include "../firmware/shintai-os/KiatsuBand.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

// Fill a fresh ring with a constant pressure so it just spans the window, leaving
// state=Steady. Returns the last observation (spanning=true).
static KiatsuObs fill_steady(KiatsuState& s, float p) {
  s = {{0}, 0, 0, KIATSU_WX_STEADY};
  KiatsuObs ob = {false, 0.0f, KIATSU_WX_STEADY};
  for (int i = 0; i < KIATSU_RING_N; i++) ob = kiatsuPush(s, p);
  return ob;
}

static void test_raw_states() {
  assert(kiatsuRawState( 0.5f) == KIATSU_WX_STEADY);    // rising
  assert(kiatsuRawState(-0.9f) == KIATSU_WX_STEADY);    // small fall — not yet
  assert(kiatsuRawState(-1.0f) == KIATSU_WX_FALLING);   // == onset enters Falling
  assert(kiatsuRawState(-2.9f) == KIATSU_WX_FALLING);
  assert(kiatsuRawState(-3.0f) == KIATSU_WX_STORM);     // == storm onset
  assert(kiatsuRawState(-8.0f) == KIATSU_WX_STORM);
}

static void test_hysteresis() {
  // From Steady, Δ must fall an extra HYST past −1.0 (=> < −1.3) to enter Falling.
  assert(kiatsuStateStep(-1.2f, KIATSU_WX_STEADY)  == KIATSU_WX_STEADY);
  assert(kiatsuStateStep(-1.4f, KIATSU_WX_STEADY)  == KIATSU_WX_FALLING);
  // From Falling, Δ must recover an extra HYST above −1.0 (=> > −0.7) to return Steady.
  assert(kiatsuStateStep(-0.8f, KIATSU_WX_FALLING) == KIATSU_WX_FALLING);
  assert(kiatsuStateStep(-0.6f, KIATSU_WX_FALLING) == KIATSU_WX_STEADY);
  // Storm boundary (−3.0): enter when < −3.3, leave when > −2.7 — the dead zone.
  assert(kiatsuStateStep(-3.2f, KIATSU_WX_FALLING) == KIATSU_WX_FALLING);
  assert(kiatsuStateStep(-3.4f, KIATSU_WX_FALLING) == KIATSU_WX_STORM);
  assert(kiatsuStateStep(-2.8f, KIATSU_WX_STORM)   == KIATSU_WX_STORM);
  assert(kiatsuStateStep(-2.6f, KIATSU_WX_STORM)   == KIATSU_WX_FALLING);

  // No flip-flop: Δ oscillating in the dead zone around −1.0 never moves the state.
  int s = KIATSU_WX_STEADY;
  for (float d : {-0.9f, -1.1f, -0.8f, -1.2f, -0.7f, -1.0f}) s = kiatsuStateStep(d, s);
  assert(s == KIATSU_WX_STEADY);

  // A hard plunge walks both bands in one step; a full recovery walks back.
  assert(kiatsuStateStep(-6.0f, KIATSU_WX_STEADY) == KIATSU_WX_STORM);
  assert(kiatsuStateStep( 1.0f, KIATSU_WX_STORM)  == KIATSU_WX_STEADY);
}

static void test_ring_fill_and_gate() {
  // A ring still filling never spans -> Kiatsu stays quiet no matter the pressure.
  KiatsuState s = {{0}, 0, 0, KIATSU_WX_STEADY};
  KiatsuObs ob = {false, 0.0f, KIATSU_WX_STEADY};
  for (int i = 0; i < KIATSU_RING_N - 1; i++) {
    ob = kiatsuPush(s, 1013.0f - i);   // even a huge drop mid-fill
    assert(!ob.spanning);
  }
  // The Nth sample completes the span.
  ob = kiatsuPush(s, 1013.0f);
  assert(ob.spanning);
}

static void test_tendency_over_window() {
  // Steady air: a full ring at one pressure reads Δ≈0 -> Steady, posts nothing.
  KiatsuState s;
  KiatsuObs ob = fill_steady(s, 1013.0f);
  assert(ob.spanning && ob.state == KIATSU_WX_STEADY);

  // A sustained fall across the whole window: newest mean well below oldest mean.
  // Overwrite the ring with a ramp from 1015 down to 1010 (Δ ≈ −5 hPa) -> Storm.
  for (int i = 0; i < KIATSU_RING_N; i++) {
    float p = 1015.0f - 5.0f * (float)i / (float)(KIATSU_RING_N - 1);
    ob = kiatsuPush(s, p);
  }
  assert(ob.spanning && ob.delta < -3.0f && ob.state == KIATSU_WX_STORM);

  // A gentle fall (~−1.5 hPa across the window) lands in Falling, not Storm.
  KiatsuState s2;
  fill_steady(s2, 1013.0f);
  KiatsuObs g = {false, 0.0f, KIATSU_WX_STEADY};
  for (int i = 0; i < KIATSU_RING_N; i++) {
    float p = 1013.0f - 1.5f * (float)i / (float)(KIATSU_RING_N - 1);
    g = kiatsuPush(s2, p);
  }
  assert(g.spanning && g.delta < -1.0f && g.delta > -3.0f && g.state == KIATSU_WX_FALLING);
}

static void test_wobble_does_not_trip() {
  // AC-4: a lone anomalous sample (a door slam / lift pop) among an otherwise steady
  // window must NOT trip the cue — edge averaging over KIATSU_EDGE_AVG samples absorbs it.
  KiatsuState s;
  fill_steady(s, 1013.0f);
  // One newest sample plunges 4 hPa, then steady resumes for the rest of the edge window.
  KiatsuObs ob = kiatsuPush(s, 1009.0f);          // the wobble
  for (int i = 0; i < KIATSU_EDGE_AVG; i++)       // steady samples flush it out of the newest mean
    ob = kiatsuPush(s, 1013.0f);
  assert(ob.spanning && ob.state == KIATSU_WX_STEADY);
}

static void test_cue_for_state() {
  // Storm: deeper cyan, the slowest/deepest breathe, at the lowest ambient rung.
  KiatsuCue storm = kiatsuCueFor(KIATSU_WX_STORM);
  assert(storm.post && storm.priority == AIZU_PRIO_KIATSU_WX);
  assert(storm.motion.kind == AIZU_BREATHE);
  assert(!aizuIsAlert(storm.priority));                        // never an alert — it's the calm floor
  assert(storm.colour.b > storm.colour.g && storm.colour.r == 0);  // cyan identity, blue-leaning (KiD-5)

  // Falling: cyan breathe, same rung, faster (shallower timescale) than the storm.
  KiatsuCue fall = kiatsuCueFor(KIATSU_WX_FALLING);
  assert(fall.post && fall.priority == AIZU_PRIO_KIATSU_WX);
  assert(fall.motion.kind == AIZU_BREATHE);
  assert(fall.colour.g > 0 && fall.colour.b > 0 && fall.colour.r == 0);  // cyan (green+blue, no red)
  assert(fall.motion.periodMs < storm.motion.periodMs);       // storm breathes slower (KiD-5)

  // Steady is quiescent: posts nothing -> Aizu Idle green (== "the sky is as it was").
  assert(!kiatsuCueFor(KIATSU_WX_STEADY).post);

  // AZ-13 ladder: Kiatsu is the LOWEST ambient rung — below even Kanki's warmup, and
  // far below every other source; the opposite pole from a Kehai Reflex.
  assert(AIZU_PRIO_KIATSU_WX < AIZU_PRIO_KANKI_WARMUP);
  assert(AIZU_PRIO_KIATSU_WX < AIZU_PRIO_KYUKAKU_TAINT);
  assert(AIZU_PRIO_KIATSU_WX > AIZU_PRIO_IDLE);
  // And it is the slowest breathe in the whole system (calmest, longest-timescale):
  // Kyūkaku's gentlest (Taint) breathes at 4000 ms; Kiatsu is slower still.
  assert(KIATSU_STORM_BREATHE_MS > 4000);
}

int main() {
  test_raw_states();
  test_hysteresis();
  test_ring_fill_and_gate();
  test_tendency_over_window();
  test_wobble_does_not_trip();
  test_cue_for_state();
  std::printf("kiatsu-band-test: all assertions passed\n");
  return 0;
}
