// Host unit tests for Hokan's pure gait DSP (HokanDsp.h).
//
// Same posture as the *-band-test.cpp / aizu-arbiter-test.cpp: the firmware has no
// on-target harness, so the decidable-from-values logic — the step counter and the
// freefall->impact->stillness fall state machine — is replayed on the host from
// synthetic accel-magnitude (m/s²) traces, no board. This is exactly the "steps
// can't be recovered from the CSV" DSP (specs/zokyo/hokan.md), checked deterministically.
//
//   c++ -std=c++17 -Wall -o /tmp/hokan-test tools/hokan-dsp-test.cpp && /tmp/hokan-test

#include "../firmware/shintai-os/HokanDsp.h"

#include <cassert>
#include <cstdio>
#include <cmath>
#include <vector>

static const uint32_t DT = 20;   // 20 ms sample step (~50 Hz), matching HOKAN_SAMPLE_MS

// Count steps over a synthetic |g| oscillation: mean = G, amplitude A, `period_ms`
// per step, `n_steps` cycles. Amplitude 3 m/s² keeps troughs (~6.8) above the
// freefall threshold (so a normal walk never looks like a fall) and peaks (~12.8)
// above the step threshold (baseline + 2.2).
static int walk_steps(int n_steps, uint32_t period_ms, float A, HokanFallDetector* fall = nullptr) {
  HokanStepDetector step;
  int count = 0;
  uint32_t t = 0;
  uint32_t total = period_ms * n_steps + period_ms / 2;   // a little extra so the last down-cross lands
  for (uint32_t ms = 0; ms <= total; ms += DT, t += DT) {
    float phase = (float)(ms % period_ms) / (float)period_ms;
    float mag = HOKAN_G + A * std::sin(2.0f * (float)M_PI * phase);
    if (step.update(mag, t)) count++;
    if (fall) fall->update(mag, t);
  }
  return count;
}

static void test_step_count() {
  // A clean 10-step walk at ~600 ms/step counts ~10 (±1 for start/stop edges).
  int s = walk_steps(10, 600, 3.0f);
  assert(std::abs(s - 10) <= 1);

  // Faster cadence (~450 ms/step) over 20 steps still tracks.
  int s2 = walk_steps(20, 450, 3.0f);
  assert(std::abs(s2 - 20) <= 2);
}

static void test_still_no_steps() {
  // Standing dead still at 1 g adds no steps (AC-1) and never trips a fall.
  HokanStepDetector step;
  HokanFallDetector fall;
  int count = 0;
  uint32_t t = 0;
  for (int i = 0; i < 500; i++, t += DT) {          // 10 s at rest
    if (step.update(HOKAN_G, t)) count++;
    assert(fall.update(HOKAN_G, t) == HOKAN_FALL_NONE);
  }
  assert(count == 0);
}

static void test_walk_is_not_a_fall() {
  // A normal walk must not false-trigger the fall detector (AC-2).
  HokanFallDetector fall;
  walk_steps(30, 550, 3.0f, &fall);
  assert(!fall.down());
}

// Feed a constant `mag` for `dur_ms`, starting at `t`, into the fall detector;
// record any event seen. Returns the advanced timestamp.
static uint32_t feed(HokanFallDetector& fall, float mag, uint32_t dur_ms, uint32_t t,
                     std::vector<HokanFallEvent>& events) {
  for (uint32_t e = 0; e < dur_ms; e += DT, t += DT) {
    HokanFallEvent ev = fall.update(mag, t);
    if (ev != HOKAN_FALL_NONE) events.push_back(ev);
  }
  return t;
}

static void test_fall_confirm_and_resolve() {
  HokanFallDetector fall;
  std::vector<HokanFallEvent> events;
  uint32_t t = 0;

  t = feed(fall, HOKAN_G, 400, t, events);        // walking/standing normally
  t = feed(fall, 2.0f,    200, t, events);        // freefall — |g| dips toward 0
  t = feed(fall, 30.0f,   40,  t, events);        // impact spike
  t = feed(fall, HOKAN_G, 1000, t, events);       // post-impact stillness (> 800 ms) -> CONFIRM
  // One confirm, and the detector is now latched DOWN.
  assert(events.size() == 1 && events[0] == HOKAN_FALL_CONFIRMED);
  assert(fall.down());

  // The wearer gets up: sustained motion away from 1 g -> RESOLVE, unlatch.
  t = feed(fall, 15.0f, 1600, t, events);
  assert(events.size() == 2 && events[1] == HOKAN_FALL_RESOLVED);
  assert(!fall.down());
}

static void test_freefall_without_impact_is_ignored() {
  // A low-g dip that never impacts (e.g. a lift/drop of the arm) must not confirm.
  HokanFallDetector fall;
  std::vector<HokanFallEvent> events;
  uint32_t t = 0;
  t = feed(fall, HOKAN_G, 200, t, events);
  t = feed(fall, 3.0f, 1000, t, events);          // long low-g, no impact spike
  t = feed(fall, HOKAN_G, 1000, t, events);
  assert(events.empty());
  assert(!fall.down());
}

static void test_cadence() {
  // Walking at 600 ms/step is ~100 steps/min; the smoothed cadence should land near
  // it, then decay to 0 once the walk stops (no step within the idle window).
  HokanStepDetector step;
  uint32_t t = 0, period = 600;
  for (uint32_t ms = 0; ms <= period * 15; ms += DT, t += DT) {
    float phase = (float)(ms % period) / (float)period;
    step.update(HOKAN_G + 3.0f * std::sin(2.0f * (float)M_PI * phase), t);
  }
  uint16_t cad = step.cadenceSpm(t);
  assert(cad >= 85 && cad <= 115);
  assert(step.cadenceSpm(t + 5000) == 0);   // stopped -> cadence reads 0
}

static void test_fall_cue_is_top_alert() {
  HokanCue c = hokanFallCue();
  assert(c.priority == AIZU_PRIO_HOKAN_FALL_SOS);
  assert(aizuIsAlert(c.priority));                       // preempts with no debounce
  assert(c.priority > AIZU_PRIO_NESSHI_HELD);            // outranks an interactive read
  assert(c.priority > AIZU_PRIO_KANKI_BAD);              // and the ambient alerts
  assert(c.priority < AIZU_PRIO_KEHAI_REFLEX);           // a collision reflex still tops it
  assert(c.colour.r == 255 && c.colour.g == 0 && c.colour.b == 0);  // red
  assert(c.motion.kind == AIZU_PULSE);                   // urgent pulse
}

int main() {
  test_step_count();
  test_still_no_steps();
  test_walk_is_not_a_fall();
  test_fall_confirm_and_resolve();
  test_freefall_without_impact_is_ignored();
  test_cadence();
  test_fall_cue_is_top_alert();
  std::printf("hokan-dsp-test: all assertions passed\n");
  return 0;
}
