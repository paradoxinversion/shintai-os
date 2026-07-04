// Host unit tests for Kyūkaku's pure gas->cue mapping (KyukakuBand.h).
//
// Same posture as kanki-band-test.cpp: the firmware has no on-target harness, so the
// decidable-from-values logic — the ratio bands + hysteresis, the baseline EMA, the
// spike detector with its humidity veto, the rolling stepState, and the cue each
// band posts — is checked on the host with no board.
//
//   c++ -std=c++17 -Wall -o /tmp/kyukaku-test tools/kyukaku-band-test.cpp && /tmp/kyukaku-test
//
// Thresholds mirror KY-6: Clean r>=0.60, Taint 0.35-0.60, Foul <0.35; Spike = a drop
// of >=0.25 below the reference not explained by a >=3 %RH humidity jump.

#include "../firmware/shintai-os/KyukakuBand.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

// Settle a fresh state: seed, then run enough constant clean readings to arm it.
// Leaves baseline≈gas, rRef≈1, band=Clean, lastHum=hum.
static void settle(KyukakuState& s, float gas, float hum) {
  s = {0.0f, 1.0f, 0.0f, 1.0f, KYUKAKU_BAND_CLEAN, 0};
  for (long i = 0; i < KYUKAKU_SETTLE_COUNT + 2; i++) kyukakuStepState(s, gas, hum);
}

static void test_raw_bands() {
  assert(kyukakuRawBand(1.00f) == KYUKAKU_BAND_CLEAN);   // baseline air
  assert(kyukakuRawBand(0.60f) == KYUKAKU_BAND_CLEAN);   // == onset stays clean
  assert(kyukakuRawBand(0.59f) == KYUKAKU_BAND_TAINT);
  assert(kyukakuRawBand(0.35f) == KYUKAKU_BAND_TAINT);   // == foul onset still taint
  assert(kyukakuRawBand(0.34f) == KYUKAKU_BAND_FOUL);
  assert(kyukakuRawBand(0.05f) == KYUKAKU_BAND_FOUL);
}

static void test_seed_from_unseeded() {
  // The -1 sentinel snaps straight to the true band (no hysteresis on first read).
  assert(kyukakuStep(0.90f, -1) == KYUKAKU_BAND_CLEAN);
  assert(kyukakuStep(0.50f, -1) == KYUKAKU_BAND_TAINT);
  assert(kyukakuStep(0.20f, -1) == KYUKAKU_BAND_FOUL);
}

static void test_hysteresis() {
  // From Clean, r must drop an extra HYST below 0.60 (=> <0.55) to enter Taint.
  assert(kyukakuStep(0.57f, KYUKAKU_BAND_CLEAN) == KYUKAKU_BAND_CLEAN);
  assert(kyukakuStep(0.54f, KYUKAKU_BAND_CLEAN) == KYUKAKU_BAND_TAINT);
  // From Taint, r must recover an extra HYST above 0.60 (=> >0.65) to return Clean.
  assert(kyukakuStep(0.63f, KYUKAKU_BAND_TAINT) == KYUKAKU_BAND_TAINT);
  assert(kyukakuStep(0.66f, KYUKAKU_BAND_TAINT) == KYUKAKU_BAND_CLEAN);
  // Foul boundary (0.35): enter when <0.30, leave when >0.40 — the dead zone.
  assert(kyukakuStep(0.32f, KYUKAKU_BAND_TAINT) == KYUKAKU_BAND_TAINT);
  assert(kyukakuStep(0.29f, KYUKAKU_BAND_TAINT) == KYUKAKU_BAND_FOUL);
  assert(kyukakuStep(0.38f, KYUKAKU_BAND_FOUL)  == KYUKAKU_BAND_FOUL);
  assert(kyukakuStep(0.42f, KYUKAKU_BAND_FOUL)  == KYUKAKU_BAND_TAINT);

  // No flip-flop: r oscillating in the dead zone around 0.60 never moves the band.
  int b = KYUKAKU_BAND_CLEAN;
  for (float r : {0.62f, 0.58f, 0.61f, 0.59f, 0.63f, 0.57f}) b = kyukakuStep(r, b);
  assert(b == KYUKAKU_BAND_CLEAN);

  // A hard drop walks multiple bands in one step; a full recovery walks back.
  assert(kyukakuStep(0.10f, KYUKAKU_BAND_CLEAN) == KYUKAKU_BAND_FOUL);
  assert(kyukakuStep(1.00f, KYUKAKU_BAND_FOUL)  == KYUKAKU_BAND_CLEAN);
}

static void test_spike_detector() {
  assert( kyukakuIsSpike(0.95f, 0.65f, 0.0f));   // drop 0.30 >= 0.25, no humidity
  assert(!kyukakuIsSpike(0.95f, 0.75f, 0.0f));   // drop 0.20 < 0.25 — too small
  assert(!kyukakuIsSpike(0.95f, 0.60f, 5.0f));   // drop 0.35 but +5 %RH — vetoed (KY-4)
  assert( kyukakuIsSpike(0.95f, 0.60f, 2.0f));   // +2 %RH < veto — still a real spike
  assert(!kyukakuIsSpike(0.80f, 0.90f, 0.0f));   // r rose (air clearing) — never a spike
}

static void test_baseline_ema() {
  // Seeded: rise toward cleaner air is moderate; decay downward is far slower, so a
  // smell can't drag the baseline down after itself (KY-1).
  float up   = kyukakuBaselineStep(100000.f, 110000.f, true);
  float down = kyukakuBaselineStep(100000.f,  90000.f, true);
  assert(up > 100000.f && up < 101000.f);          // moved up a little
  assert(down < 100000.f && down > 99900.f);        // moved down only a hair
  assert((100000.f - down) < (up - 100000.f));      // decay slower than rise (asymmetric)
  // Unseeded (settling): converge fast to seed R0.
  float seed = kyukakuBaselineStep(100000.f, 110000.f, false);
  assert(seed > 100900.f);                          // ~+1000 with SEED_ALPHA
}

static void test_cue_for_band() {
  // Spike: fast violet->red PULSE at ALERT-class priority (preempts, no debounce).
  KyukakuCue sp = kyukakuSpikeCue();
  assert(sp.post && sp.priority == AIZU_PRIO_KYUKAKU_SPIKE);
  assert(aizuIsAlert(sp.priority));
  assert(sp.motion.kind == AIZU_PULSE);
  assert(sp.colour.r == 255 && sp.colour.b > 0);    // hot violet->red

  // Foul / Taint: violet breathes, told apart from Kanki (amber/orange/red) by hue.
  KyukakuCue foul  = kyukakuCueFor(KYUKAKU_BAND_FOUL);
  KyukakuCue taint = kyukakuCueFor(KYUKAKU_BAND_TAINT);
  assert(foul.post  && foul.priority  == AIZU_PRIO_KYUKAKU_FOUL);
  assert(taint.post && taint.priority == AIZU_PRIO_KYUKAKU_TAINT);
  assert(foul.motion.kind == AIZU_BREATHE && taint.motion.kind == AIZU_BREATHE);
  assert(foul.colour.b > foul.colour.r && foul.colour.g == 0);   // violet identity (KY-3)
  assert(!aizuIsAlert(foul.priority) && !aizuIsAlert(taint.priority));

  // Clean is quiescent: posts nothing -> Aizu Idle green (== "air is as it was").
  assert(!kyukakuCueFor(KYUKAKU_BAND_CLEAN).post);

  // Voice: Foul more insistent (shorter breathe) than Taint; the Spike is the one
  // fast motion, faster than either ambient breathe.
  assert(foul.motion.periodMs < taint.motion.periodMs);
  assert(sp.motion.periodMs   < foul.motion.periodMs);

  // AZ-12 ladder: Spike below Fall SOS but above Nesshi; Foul between Kanki Poor and
  // Stuffy; Taint just below Kanki Stuffy.
  assert(AIZU_PRIO_KYUKAKU_SPIKE < AIZU_PRIO_HOKAN_FALL_SOS);
  assert(AIZU_PRIO_KYUKAKU_SPIKE > AIZU_PRIO_NESSHI_HELD);
  assert(AIZU_PRIO_KANKI_POOR    > AIZU_PRIO_KYUKAKU_FOUL);
  assert(AIZU_PRIO_KYUKAKU_FOUL  > AIZU_PRIO_KANKI_STUFFY);
  assert(AIZU_PRIO_KANKI_STUFFY  > AIZU_PRIO_KYUKAKU_TAINT);
}

static void test_stepstate_settle_and_spike() {
  // First reading seeds R0 and does NOT arm.
  KyukakuState s = {0.0f, 1.0f, 0.0f, 1.0f, KYUKAKU_BAND_CLEAN, 0};
  KyukakuObs o = kyukakuStepState(s, 100000.f, 40.f);
  assert(!o.seeded && s.baseline == 100000.f && s.count == 1);

  // Even a hard drop during settle raises no spike (KY-5).
  KyukakuObs during = kyukakuStepState(s, 50000.f, 40.f);
  assert(!during.seeded && !during.spike);

  // Settled clean air: a sudden gas collapse fires a spike...
  settle(s, 100000.f, 40.f);
  assert(kyukakuStepState(s, 60000.f, 40.f).spike);     // r~0.6 vs rRef~1.0 => drop 0.4

  // ...but the same collapse riding a humidity jump is vetoed.
  KyukakuState s2;
  settle(s2, 100000.f, 40.f);
  assert(!kyukakuStepState(s2, 60000.f, 50.f).spike);   // +10 %RH — it's moisture, not a smell

  // Sustained loaded air resolves to a non-Clean ambient band (baseline holds, so r
  // stays low rather than the baseline chasing it down).
  KyukakuState s3;
  settle(s3, 100000.f, 40.f);
  KyukakuObs last = {false, false, KYUKAKU_BAND_CLEAN, 1.0f};
  for (int i = 0; i < 5; i++) last = kyukakuStepState(s3, 40000.f, 40.f);
  assert(last.seeded && last.band != KYUKAKU_BAND_CLEAN);
}

int main() {
  test_raw_bands();
  test_seed_from_unseeded();
  test_hysteresis();
  test_spike_detector();
  test_baseline_ema();
  test_cue_for_band();
  test_stepstate_settle_and_spike();
  std::printf("kyukaku-band-test: all assertions passed\n");
  return 0;
}
