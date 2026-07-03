// Host unit tests for Aizu's pure arbitration + rendering math (AizuCore.h).
//
// The firmware has no on-target test harness (it's an Arduino sketch), so this
// covers the decidable-from-values logic — winner selection, staleness, the
// anti-flicker switch rule, the motion envelope, and the gamma/cap level — on
// the host, with no board attached. It mirrors the check-contract.py posture:
// a stdlib-only, hardware-free check that catches drift before a flash.
//
//   c++ -std=c++17 -Wall -o /tmp/aizu-test tools/aizu-arbiter-test.cpp && /tmp/aizu-test
//
// Exits non-zero on the first failed assertion.

#include "../firmware/shintai-os/AizuCore.h"

#include <cassert>
#include <cmath>
#include <cstdio>

static AizuCue mkCue(AizuSource src, int prio, uint32_t maxAge, uint32_t postedAt) {
  AizuCue c{};
  c.active = true;
  c.source = src;
  c.priority = prio;
  c.colour = AizuColour{255, 0, 0};
  c.motion = AizuMotion{AIZU_SOLID, 0};
  c.maxAgeMs = maxAge;
  c.postedAtMs = postedAt;
  return c;
}

static void test_winner_empty() {
  AizuCue cues[AIZU_SOURCE_COUNT];
  for (int i = 0; i < AIZU_SOURCE_COUNT; i++) cues[i].active = false;
  assert(aizuPickWinner(cues, AIZU_SOURCE_COUNT, 1000) == -1);  // nothing live -> Idle
}

static void test_winner_highest_priority() {
  AizuCue cues[AIZU_SOURCE_COUNT];
  for (int i = 0; i < AIZU_SOURCE_COUNT; i++) cues[i].active = false;
  cues[AIZU_KANKI] = mkCue(AIZU_KANKI, AIZU_PRIO_KANKI_POOR, 0, 0);
  cues[AIZU_KEHAI] = mkCue(AIZU_KEHAI, AIZU_PRIO_KEHAI_REFLEX, 0, 0);
  // Reflex outranks a Kanki ambient (spec ladder, AC-4).
  assert(aizuPickWinner(cues, AIZU_SOURCE_COUNT, 0) == AIZU_KEHAI);

  // Approach outranks Kanki Poor; Kanki Bad outranks Approach (ladder spot-checks).
  cues[AIZU_KEHAI] = mkCue(AIZU_KEHAI, AIZU_PRIO_KEHAI_APPROACH, 0, 0);
  assert(aizuPickWinner(cues, AIZU_SOURCE_COUNT, 0) == AIZU_KEHAI);
  cues[AIZU_KANKI] = mkCue(AIZU_KANKI, AIZU_PRIO_KANKI_BAD, 0, 0);
  assert(aizuPickWinner(cues, AIZU_SOURCE_COUNT, 0) == AIZU_KANKI);
}

static void test_staleness_drops_cue() {
  AizuCue cues[AIZU_SOURCE_COUNT];
  for (int i = 0; i < AIZU_SOURCE_COUNT; i++) cues[i].active = false;
  cues[AIZU_KANKI] = mkCue(AIZU_KANKI, AIZU_PRIO_KANKI_POOR, 15000, 1000);  // posted @1s, 15s life
  assert(aizuCueLive(cues[AIZU_KANKI], 10000));                             // 9s later: live
  assert(aizuPickWinner(cues, AIZU_SOURCE_COUNT, 10000) == AIZU_KANKI);
  assert(!aizuCueLive(cues[AIZU_KANKI], 20000));                            // 19s later: stale
  assert(aizuPickWinner(cues, AIZU_SOURCE_COUNT, 20000) == -1);            // frozen colour dropped
  // maxAgeMs==0 never expires (used by the internal Idle cue).
  cues[AIZU_SYSTEM] = mkCue(AIZU_SYSTEM, AIZU_PRIO_IDLE, 0, 0);
  assert(aizuCueLive(cues[AIZU_SYSTEM], 4000000000u));
}

static void test_should_switch() {
  // Upward preempt is instant even with zero dwell (a Reflex never debounces).
  assert(aizuShouldSwitch(AIZU_PRIO_KANKI_POOR, AIZU_PRIO_KEHAI_REFLEX,
                          /*sameSource=*/false, /*msSinceSwitch=*/0, 250));
  // Release back to a lower cue: shownPrio passed as IDLE (old winner not live) -> instant.
  assert(aizuShouldSwitch(AIZU_PRIO_IDLE, AIZU_PRIO_KANKI_POOR, false, 0, 250));
  // Downward/near-equal with two LIVE cues waits out the debounce dwell.
  assert(!aizuShouldSwitch(AIZU_PRIO_KEHAI_APPROACH, AIZU_PRIO_KANKI_POOR, false, 100, 250));
  assert(aizuShouldSwitch(AIZU_PRIO_KEHAI_APPROACH, AIZU_PRIO_KANKI_POOR, false, 300, 250));
  // A source refreshing its own slot always applies immediately.
  assert(aizuShouldSwitch(AIZU_PRIO_KANKI_POOR, AIZU_PRIO_KANKI_BAD, true, 0, 250));
}

static void test_envelope() {
  assert(aizuEnvelope(AIZU_STEADY, 0, 0) == 1.0f);
  assert(aizuEnvelope(AIZU_SOLID, 0, 12345) == 1.0f);

  // Breathe: trough at phase 0, peak at half period, back to trough at full.
  assert(aizuEnvelope(AIZU_BREATHE, 1000, 0) < 0.01f);
  assert(aizuEnvelope(AIZU_BREATHE, 1000, 500) > 0.99f);
  assert(aizuEnvelope(AIZU_BREATHE, 1000, 1000) < 0.01f);   // wraps

  // Pulse: sharp on early in the cycle, dim floor later.
  assert(aizuEnvelope(AIZU_PULSE, 1000, 100) == 1.0f);
  assert(aizuEnvelope(AIZU_PULSE, 1000, 800) < 0.5f);

  // Heartbeat: brief blink at the top of the interval, dark otherwise.
  assert(aizuEnvelope(AIZU_HEARTBEAT, 30000, 50) == 1.0f);
  assert(aizuEnvelope(AIZU_HEARTBEAT, 30000, 5000) == 0.0f);
}

static void test_level_and_colour() {
  // Envelope 0 -> off; full envelope -> exactly the cap; nothing exceeds the cap.
  assert(aizuLevel(0.0f, 40) == 0);
  assert(aizuLevel(1.0f, 40) == 40);
  assert(aizuLevel(0.5f, 40) <= 40);
  // Gamma is monotonic: brighter envelope never dimmer output.
  assert(aizuLevel(0.25f, 40) <= aizuLevel(0.75f, 40));
  // Idle obeys a dimmer cap.
  assert(aizuLevel(1.0f, 12) == 12);

  // Colour scaling: green hue at cap 40 -> (0,40,0); at level 0 -> black.
  AizuColour green = {0, 255, 0};
  AizuColour lit = aizuScaleColour(green, 40);
  assert(lit.r == 0 && lit.g == 40 && lit.b == 0);
  AizuColour off = aizuScaleColour(green, 0);
  assert(off.r == 0 && off.g == 0 && off.b == 0);
}

int main() {
  test_winner_empty();
  test_winner_highest_priority();
  test_staleness_drops_cue();
  test_should_switch();
  test_envelope();
  test_level_and_colour();
  std::printf("aizu-arbiter-test: all assertions passed\n");
  return 0;
}
