// Host unit tests for Kehai-Hikari's pure distance->cue mapping (KehaiBand.h).
//
// Same posture as aizu-arbiter-test.cpp: the firmware has no on-target harness,
// so the decidable-from-values logic — band classification, the perceptual pulse
// curve, and the cue each band posts — is checked on the host with no board.
//
//   c++ -std=c++17 -Wall -o /tmp/kehai-test tools/kehai-band-test.cpp && /tmp/kehai-test
//
// Thresholds mirror the sketch's NEAR_MM (200) / FAR_MM (2000).

#include "../firmware/shintai-os/KehaiBand.h"

#include <cassert>
#include <cstdio>

static const int NEAR = 200;
static const int FAR  = 2000;

static void test_classify() {
  assert(kehaiClassify(-1,   NEAR, FAR) == KEHAI_BAND_IDLE);      // no target
  assert(kehaiClassify(0,    NEAR, FAR) == KEHAI_BAND_IDLE);      // mm<=0 is no target
  assert(kehaiClassify(3000, NEAR, FAR) == KEHAI_BAND_CLEAR);     // beyond FAR
  assert(kehaiClassify(2000, NEAR, FAR) == KEHAI_BAND_APPROACH);  // == FAR is still in range
  assert(kehaiClassify(1000, NEAR, FAR) == KEHAI_BAND_APPROACH);
  assert(kehaiClassify(201,  NEAR, FAR) == KEHAI_BAND_APPROACH);  // just outside NEAR
  assert(kehaiClassify(200,  NEAR, FAR) == KEHAI_BAND_REFLEX);    // == NEAR is too close
  assert(kehaiClassify(50,   NEAR, FAR) == KEHAI_BAND_REFLEX);
}

static void test_approach_period_curve() {
  // Endpoints: near NEAR -> PERIOD_MIN; at FAR -> PERIOD_MAX.
  assert(kehaiApproachPeriod(NEAR + 1, NEAR, FAR) <= KEHAI_PERIOD_MIN_MS + 2);
  assert(kehaiApproachPeriod(FAR, NEAR, FAR) == KEHAI_PERIOD_MAX_MS);
  // Monotonic: closer is never a longer period.
  uint16_t far = kehaiApproachPeriod(1800, NEAR, FAR);
  uint16_t mid = kehaiApproachPeriod(1000, NEAR, FAR);
  uint16_t nearer = kehaiApproachPeriod(400, NEAR, FAR);
  assert(nearer < mid && mid < far);
  // f^2 curve: the change is steepest near NEAR. The drop over the closest 400 mm
  // exceeds the drop over an equal 400 mm span up high.
  uint16_t d_low  = kehaiApproachPeriod(600, NEAR, FAR)  - kehaiApproachPeriod(200, NEAR, FAR);
  uint16_t d_high = kehaiApproachPeriod(2000, NEAR, FAR) - kehaiApproachPeriod(1600, NEAR, FAR);
  assert(d_low < d_high);   // less period-change near NEAR == resolution concentrated there
  // Clamps: out-of-band inputs don't overflow the curve.
  assert(kehaiApproachPeriod(50, NEAR, FAR) == KEHAI_PERIOD_MIN_MS);
  assert(kehaiApproachPeriod(9999, NEAR, FAR) == KEHAI_PERIOD_MAX_MS);
}

static void test_cue_for_band() {
  // Reflex: red SOLID ALERT at the top priority.
  KehaiCue reflex = kehaiCueFor(100, NEAR, FAR);
  assert(reflex.post && reflex.priority == AIZU_PRIO_KEHAI_REFLEX);
  assert(reflex.colour.r == 255 && reflex.colour.g == 0 && reflex.colour.b == 0);
  assert(reflex.motion.kind == AIZU_SOLID);

  // Approach: amber PULSE at the perceptual period, AMBIENT+ priority.
  KehaiCue appr = kehaiCueFor(1000, NEAR, FAR);
  assert(appr.post && appr.priority == AIZU_PRIO_KEHAI_APPROACH);
  assert(appr.colour.r == 255 && appr.colour.g == 110 && appr.colour.b == 0);
  assert(appr.motion.kind == AIZU_PULSE);
  assert(appr.motion.periodMs >= KEHAI_PERIOD_MIN_MS && appr.motion.periodMs <= KEHAI_PERIOD_MAX_MS);

  // Clear and no-target are quiescent: post nothing -> Aizu Idle.
  assert(!kehaiCueFor(3000, NEAR, FAR).post);   // Clear
  assert(!kehaiCueFor(-1,   NEAR, FAR).post);   // no target
  assert(!kehaiCueFor(0,    NEAR, FAR).post);
}

int main() {
  test_classify();
  test_approach_period_curve();
  test_cue_for_band();
  std::printf("kehai-band-test: all assertions passed\n");
  return 0;
}
