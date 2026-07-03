# Aizu — follow-up work

Deferred items surfaced while implementing [`aizu.md`](aizu.md) (2026-07-02). Aizu
itself is built and compiles; these are downstream/verification items it depends
on or enables.

## 2026-07-02 — initial build

- **Cue sources — first one now built.** Aizu is *shared infrastructure*; the
  sources that post cues were all separate, unbuilt specs. As of 2026-07-02,
  [Kehai-Hikari](../zokyo/kehai-hikari.md) is **built** and posts Approach/Reflex
  cues via `serviceReflex()` — the first live exercise of the cue bus, confirming
  the `postCue`/`clearCue` seam. **[Kanki](../zokyo/kanki.md) is now built too**
  (2026-07-02) — it posts Ambient CO2 cues (Stuffy/Poor/Bad + warm-up) from the
  telemetry loop, the **second** source on the bus and the first *ambient*-class
  one. Two sources now share the pixel, so the arbiter's preempt/release path
  (Kehai Reflex over Kanki air colour) is live, not just by-inspection. Still
  unbuilt: [Nesshi](../zokyo/nesshi.md), [Hokan](../zokyo/hokan.md).

- **Nesshi HOLD subscriber not registered.** The gesture layer exposes
  `Aizu.onHold(handler)` for HOLD_START/HOLD_END (CLICK→mute is internal). Nesshi
  is the intended first subscriber (hold-to-measure, AZ-9/AZ-10) but is unbuilt, so
  no handler is registered — HOLD is a no-op until then.

- **Runtime acceptance criteria pending live sources.** AC-1–4 (single-source
  Kehai/Kanki behaviour; preempt+release) require a posting source to exercise
  on-wrist; they're satisfied *by inspection of the seam* here, not at runtime.
  AC-6 (Idle), AC-7 (mute), AC-9 (no telemetry regression) and the by-inspection
  AC-10/11 are met by this build. Re-run all ACs on hardware once a source lands.

- **No on-target firmware test harness.** Added `tools/aizu-arbiter-test.cpp` — a
  host (`c++`) test of the pure logic in `AizuCore.h` (winner pick, staleness,
  anti-flicker switch rule, motion envelope, gamma/cap). The stateful renderer,
  NeoPixel sink, and button are only exercised by the on-device build. Consider
  wiring the host test into the pre-commit hook / CI beside `check-contract.py`.

- **Tunables are first-draft defaults.** `AIZU_MAX_BRIGHT` (40), `AIZU_IDLE_BRIGHT`
  (12), render tick (20 ms), debounce (250 ms), hold threshold (400 ms), idle
  breathe (3.5 s) / heartbeat (30 s) — all per the spec's suggested starts. Tune
  on-wrist for eye comfort, battery, and button feel.

- **Branch convention.** Built on the working tree; the spec notes features build
  on a later `research-development-*` branch. Move/commit there if following that
  flow.

## 2026-07-02 — Metsuke (thermal grid) coexistence

Metsuke ([`../zokyo/metsuke.md`](../zokyo/metsuke.md)) is built. It doesn't post to
Aizu (its surface is the glasses, not the pixel), but its firmware integration has
one thing to validate on hardware *because* of the Kehai reflex:

- **2 Hz `getFrame()` vs the reflex tick.** Metsuke moved the MLX90640 read into a
  `serviceThermal()` tick at `THERMAL_MS` (500 ms, ~2 Hz), the sole thermal read
  site (telemetry now consumes the cached frame). `mlx.getFrame()` **blocks** while
  it reads; at 2 Hz that stall now recurs ~3× more often than the old 1500 ms
  telemetry read. Kehai's reflex (`serviceReflex`, 120 ms) holds its last range
  across the stall, so correctness is fine, but **verify on-wrist that the red
  reflex still feels ≤150 ms** (Kehai AC-3) with the grid streaming. If it hitches,
  bump `THERMAL_MS` (fewer, or gate thermal reads on a non-blocking data-ready poll).

- **Runtime ACs need the glasses + MLX90640.** AC-1–4 (2 Hz stream, auto-range,
  whole 68-byte frames over a negotiated MTU, gated on subscription) are met *by
  construction* here — host-tested pack (`tools/thermal-grid-test.cpp`), compiled
  firmware + Android. Re-run them live: subscribe from Glass, wave a warm hand, and
  confirm the hot cells track and the panel auto-ranges.
