# Zōkyō specs — follow-up work

## Kehai-Hikari — 2026-07-02 (initial build)

Built from [`kehai-hikari.md`](kehai-hikari.md): the ToF proximity reflex now
renders on the onboard NeoPixel via Aizu. Deferred / verify-on-hardware items:

- **Haptic path (DRV2605L) — out of scope, forward path.** Kehai *proper* (the
  vibration reflex) is still `planned`, blocked on the part. It drops in as a
  second Aizu output sink behind the same cue — no sensing change. This is the
  spec's own Forward path, not a gap in this build.

- **On-wrist AC verification.** AC-2 (Approach pulse visibly speeds up, steepest
  in the last half-metre) and AC-3 (Reflex red within ≤150 ms, coincident with
  `alert == 1`) can only be confirmed on hardware. The pure band/curve logic is
  covered by `tools/kehai-band-test.cpp`; the latency target depends on the ToF
  timing budget below.

- **ToF timing budget is a first guess.** `REFLEX_TIMING_BUDGET_US = 50000` (50 ms)
  lowers the VL53L4CX budget to favour reflex latency (D-4). If the device rejects
  that value it warns and keeps the default (non-fatal). Tune on-wrist against the
  ≤150 ms latency target and range noise; consider setting the inter-measurement
  period too if fresh samples don't land close to `REFLEX_MS` (120 ms).

- **Reflex motion = SOLID red.** The spec allows "fast pulse or solid"; SOLID was
  chosen as the least-ambiguous alarm (and matches "below PERIOD_MIN the pulse
  reads as effectively solid"). Swap to a fast `AIZU_PULSE` in `kehaiCueFor()` if
  on-wrist testing prefers a live-looking blink over a steady glow.

- **D-2 (BOOT mute) — resolved.** The Kehai spec deferred the BOOT-button mute to
  the shared input layer; that layer shipped with Aizu (CLICK → mute). No Kehai
  work remains for it.
