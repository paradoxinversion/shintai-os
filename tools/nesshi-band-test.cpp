// Host unit tests for Nesshi's pure temperature->cue mapping (NesshiBand.h).
//
// Same posture as kanki-band-test.cpp / kehai-band-test.cpp / aizu-arbiter-test.cpp:
// the firmware has no on-target harness, so the decidable-from-values logic — the
// raw safety bands, the ±2 °C edge hysteresis walk, and the cue each band posts —
// is checked on the host with no board.
//
//   c++ -std=c++17 -Wall -o /tmp/nesshi-test tools/nesshi-band-test.cpp && /tmp/nesshi-test
//
// Bands mirror ND-5: Cold <0, Cool 0-40 (safe), Warm 40-50, Hot 50-60, Danger >60.

#include "../firmware/shintai-os/NesshiBand.h"

#include <cassert>
#include <cstdio>
#include <initializer_list>

static void test_raw_bands() {
  assert(nesshiRawBand(-5.0f) == NESSHI_BAND_COLD);     // freezing
  assert(nesshiRawBand(-0.1f) == NESSHI_BAND_COLD);
  assert(nesshiRawBand(0.0f)  == NESSHI_BAND_COOL);     // 0 == edge climbs to safe
  assert(nesshiRawBand(22.0f) == NESSHI_BAND_COOL);     // room temp — safe to touch
  assert(nesshiRawBand(39.9f) == NESSHI_BAND_COOL);
  assert(nesshiRawBand(40.0f) == NESSHI_BAND_WARM);     // caution onset
  assert(nesshiRawBand(49.9f) == NESSHI_BAND_WARM);
  assert(nesshiRawBand(50.0f) == NESSHI_BAND_HOT);      // burn-risk onset
  assert(nesshiRawBand(59.9f) == NESSHI_BAND_HOT);
  assert(nesshiRawBand(60.0f) == NESSHI_BAND_DANGER);   // do not touch
  assert(nesshiRawBand(200.0f) == NESSHI_BAND_DANGER);  // a stove
}

static void test_seed_from_unseeded() {
  // The -1 "unseeded" sentinel (passed at the start of each hold) snaps straight to
  // the true band with no hysteresis, so the first frame shows the right colour.
  assert(nesshiStep(22.0f, -1) == NESSHI_BAND_COOL);
  assert(nesshiStep(45.0f, -1) == NESSHI_BAND_WARM);
  assert(nesshiStep(80.0f, -1) == NESSHI_BAND_DANGER);
  assert(nesshiStep(-10.0f, -1) == NESSHI_BAND_COLD);
}

static void test_hysteresis() {
  // Sitting in Cool, brushing the 40 °C edge does NOT climb until +HYST past it.
  assert(nesshiStep(41.0f, NESSHI_BAND_COOL) == NESSHI_BAND_COOL);   // < 40+2
  assert(nesshiStep(42.5f, NESSHI_BAND_COOL) == NESSHI_BAND_WARM);   // >= 42 climbs
  // Sitting in Warm, dropping toward 40 °C holds until -HYST below it.
  assert(nesshiStep(39.0f, NESSHI_BAND_WARM) == NESSHI_BAND_WARM);   // > 40-2
  assert(nesshiStep(37.0f, NESSHI_BAND_WARM) == NESSHI_BAND_COOL);   // < 38 falls
  // The dead zone at the burn edge (50 °C): a value inside [48, 52) keeps prev.
  assert(nesshiStep(51.0f, NESSHI_BAND_WARM) == NESSHI_BAND_WARM);   // < 50+2, stays low
  assert(nesshiStep(51.0f, NESSHI_BAND_HOT)  == NESSHI_BAND_HOT);    // > 50-2, stays high
  // And at the danger edge (60 °C).
  assert(nesshiStep(61.0f, NESSHI_BAND_HOT)    == NESSHI_BAND_HOT);    // < 60+2
  assert(nesshiStep(61.0f, NESSHI_BAND_DANGER) == NESSHI_BAND_DANGER); // > 60-2

  // No flip-flop across a boundary: a read oscillating in the dead zone never moves
  // the band (AC-2). Hovering at 50 ± a degree from Warm stays Warm.
  int b = NESSHI_BAND_WARM;
  for (float c : {49.5f, 50.5f, 49.0f, 51.0f, 48.5f, 51.5f}) b = nesshiStep(c, b);
  assert(b == NESSHI_BAND_WARM);

  // A big jump walks multiple bands in one step; a big drop walks back.
  assert(nesshiStep(90.0f, NESSHI_BAND_COOL)  == NESSHI_BAND_DANGER);
  assert(nesshiStep(20.0f, NESSHI_BAND_DANGER) == NESSHI_BAND_COOL);
}

static void test_cue_for_band() {
  // Every held read posts an INTERACTIVE, STEADY cue at the Nesshi rung — you are
  // reading a value, not being alarmed; the escalation is entirely in the colour.
  for (int band = NESSHI_BAND_COLD; band <= NESSHI_BAND_DANGER; band++) {
    NesshiCue c = nesshiCueFor(band);
    assert(c.post);
    assert(c.priority == AIZU_PRIO_NESSHI_HELD);
    assert(c.motion.kind == AIZU_STEADY);
    assert(!aizuIsAlert(c.priority));   // interactive, not an ALERT
  }

  // The safety colours, edge by edge.
  assert(nesshiCueFor(NESSHI_BAND_COLD).colour.b   > 200);  // blue-dominant
  NesshiCue cool = nesshiCueFor(NESSHI_BAND_COOL);
  assert(cool.colour.g == 255 && cool.colour.r == 0);        // green — safe to touch
  NesshiCue danger = nesshiCueFor(NESSHI_BAND_DANGER);
  assert(danger.colour.r == 255 && danger.colour.g == 0 && danger.colour.b == 0);  // red

  // Warm is amber, Hot is orange — the amber->orange->red climb up the burn line.
  NesshiCue warm = nesshiCueFor(NESSHI_BAND_WARM);
  NesshiCue hot  = nesshiCueFor(NESSHI_BAND_HOT);
  assert(warm.colour.g > hot.colour.g);   // amber is greener than orange
  assert(hot.colour.g  > danger.colour.g);

  // Nesshi's rung sits below the top safety ALERT (a collision) but above the
  // ambient air wallpaper (AZ-10) — a deliberate read dominates ambient, yields to
  // a reflex.
  assert(AIZU_PRIO_NESSHI_HELD < AIZU_PRIO_KEHAI_REFLEX);
  assert(AIZU_PRIO_NESSHI_HELD > AIZU_PRIO_KANKI_BAD);
  assert(AIZU_PRIO_NESSHI_HELD > AIZU_PRIO_KEHAI_APPROACH);
}

static void test_no_sensor_cue() {
  // No MLX90640: a distinct dim magenta PULSE so the hold still gives feedback but
  // is unmistakably "no reading," not a temperature colour (integration point 5).
  NesshiCue c = nesshiNoSensorCue();
  assert(c.post && c.priority == AIZU_PRIO_NESSHI_HELD);
  assert(c.motion.kind == AIZU_PULSE);                 // not STEADY -> reads as "no data"
  assert(c.colour.r > 0 && c.colour.b > 0 && c.colour.g == 0);  // magenta, not a band hue
}

int main() {
  test_raw_bands();
  test_seed_from_unseeded();
  test_hysteresis();
  test_cue_for_band();
  test_no_sensor_cue();
  std::printf("nesshi-band-test: all assertions passed\n");
  return 0;
}
