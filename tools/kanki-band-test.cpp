// Host unit tests for Kanki's pure CO2->cue mapping (KankiBand.h).
//
// Same posture as kehai-band-test.cpp / aizu-arbiter-test.cpp: the firmware has no
// on-target harness, so the decidable-from-values logic — the raw bands, the ±50
// ppm hysteresis walk, and the cue each band posts — is checked on the host with
// no board.
//
//   c++ -std=c++17 -Wall -o /tmp/kanki-test tools/kanki-band-test.cpp && /tmp/kanki-test
//
// Thresholds mirror KD-4: Fresh <800, Stuffy 800-1200, Poor 1200-2000, Bad >=2000.

#include "../firmware/shintai-os/KankiBand.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

static void test_raw_bands() {
  assert(kankiRawBand(420)  == KANKI_BAND_FRESH);   // outdoor air
  assert(kankiRawBand(799)  == KANKI_BAND_FRESH);
  assert(kankiRawBand(800)  == KANKI_BAND_STUFFY);  // == edge climbs
  assert(kankiRawBand(1199) == KANKI_BAND_STUFFY);
  assert(kankiRawBand(1200) == KANKI_BAND_POOR);
  assert(kankiRawBand(1999) == KANKI_BAND_POOR);
  assert(kankiRawBand(2000) == KANKI_BAND_BAD);     // CO2_POOR = Bad onset
  assert(kankiRawBand(5000) == KANKI_BAND_BAD);
}

static void test_seed_from_warmup() {
  // The -1 "unseeded"/warm-up sentinel snaps straight to the true band (no
  // hysteresis on the first reading).
  assert(kankiStep(650,  -1) == KANKI_BAND_FRESH);
  assert(kankiStep(1500, -1) == KANKI_BAND_POOR);
  assert(kankiStep(2500, -1) == KANKI_BAND_BAD);
}

static void test_hysteresis() {
  // Sitting in Fresh, brushing the 800 edge does NOT climb until +HYST past it.
  assert(kankiStep(820, KANKI_BAND_FRESH)  == KANKI_BAND_FRESH);   // < 800+50
  assert(kankiStep(850, KANKI_BAND_FRESH)  == KANKI_BAND_STUFFY);  // >= 850 climbs
  // Sitting in Stuffy, dropping toward 800 holds until -HYST below it.
  assert(kankiStep(780, KANKI_BAND_STUFFY) == KANKI_BAND_STUFFY);  // > 800-50
  assert(kankiStep(749, KANKI_BAND_STUFFY) == KANKI_BAND_FRESH);   // < 750 falls
  // The dead zone at every boundary: a value inside [B-HYST, B+HYST) keeps prev.
  assert(kankiStep(1220, KANKI_BAND_STUFFY) == KANKI_BAND_STUFFY); // < 1200+50, stays low
  assert(kankiStep(1220, KANKI_BAND_POOR)   == KANKI_BAND_POOR);   // > 1200-50, stays high
  assert(kankiStep(1960, KANKI_BAND_POOR)   == KANKI_BAND_POOR);   // < 2000+50
  assert(kankiStep(1960, KANKI_BAND_BAD)    == KANKI_BAND_BAD);    // > 2000-50

  // No flip-flop across a boundary: oscillating co2 in the dead zone never moves
  // the band (AC-2). Hovering at 800 ± a few ppm from Fresh stays Fresh.
  int b = KANKI_BAND_FRESH;
  for (uint16_t co2 : {795u, 805u, 798u, 802u, 790u, 810u}) b = kankiStep(co2, b);
  assert(b == KANKI_BAND_FRESH);

  // A big spike walks multiple bands in one step; a big drop walks back.
  assert(kankiStep(3000, KANKI_BAND_FRESH) == KANKI_BAND_BAD);
  assert(kankiStep(400,  KANKI_BAND_BAD)   == KANKI_BAND_FRESH);
}

static void test_cue_for_band() {
  // Bad: red slow PULSE at ALERT-class priority (aizuIsAlert -> preempts).
  KankiCue bad = kankiCueFor(KANKI_BAND_BAD);
  assert(bad.post && bad.priority == AIZU_PRIO_KANKI_BAD);
  assert(bad.colour.r == 255 && bad.colour.g == 0 && bad.colour.b == 0);
  assert(bad.motion.kind == AIZU_PULSE && bad.motion.periodMs == KANKI_BAD_PULSE_MS);
  assert(aizuIsAlert(bad.priority));

  // Poor: orange breathe, more insistent (shorter period) than Stuffy.
  KankiCue poor = kankiCueFor(KANKI_BAND_POOR);
  assert(poor.post && poor.priority == AIZU_PRIO_KANKI_POOR);
  assert(poor.colour.r == 255 && poor.colour.g == 60 && poor.colour.b == 0);
  assert(poor.motion.kind == AIZU_BREATHE);

  // Stuffy: amber breathe, the calmest active band.
  KankiCue stuffy = kankiCueFor(KANKI_BAND_STUFFY);
  assert(stuffy.post && stuffy.priority == AIZU_PRIO_KANKI_STUFFY);
  assert(stuffy.colour.r == 255 && stuffy.colour.g == 110 && stuffy.colour.b == 0);
  assert(stuffy.motion.kind == AIZU_BREATHE);

  // Calm vocabulary: no active Kanki band strobes; every one is slow, and the
  // escalation tightens the period (Stuffy > Poor > Bad) without ever going fast.
  assert(stuffy.motion.periodMs > poor.motion.periodMs);
  assert(poor.motion.periodMs   > bad.motion.periodMs);
  assert(bad.motion.periodMs >= 1000);   // even "Bad" stays slow, not a strobe

  // Priority ladder is strictly ordered so the arbiter escalates monotonically.
  assert(bad.priority > poor.priority && poor.priority > stuffy.priority);

  // Fresh is quiescent: posts nothing -> Aizu Idle green (== the Fresh state).
  assert(!kankiCueFor(KANKI_BAND_FRESH).post);
}

static void test_warmup_cue() {
  // Distinct dim white/blue breathe, lowest Kanki priority so a Kehai reflex or
  // approach still wins during the ~5 s warm-up.
  KankiCue w = kankiWarmupCue();
  assert(w.post && w.priority == AIZU_PRIO_KANKI_WARMUP);
  assert(w.motion.kind == AIZU_BREATHE);
  assert(w.colour.b > w.colour.r);            // blue-dominant, not green all-clear
  assert(w.priority < AIZU_PRIO_KANKI_STUFFY);
  assert(w.priority < AIZU_PRIO_KEHAI_APPROACH);
  assert(!aizuIsAlert(w.priority));
}

int main() {
  test_raw_bands();
  test_seed_from_warmup();
  test_hysteresis();
  test_cue_for_band();
  test_warmup_cue();
  std::printf("kanki-band-test: all assertions passed\n");
  return 0;
}
