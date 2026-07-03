# Aizu — follow-up work

Deferred items surfaced while implementing [`aizu.md`](aizu.md) (2026-07-02). Aizu
itself is built and compiles; these are downstream/verification items it depends
on or enables.

## 2026-07-02 — initial build

- **Cue sources are not implemented yet.** Aizu is *shared infrastructure*; the
  sources that post cues — [Kehai-Hikari](../zokyo/kehai-hikari.md),
  [Kanki](../zokyo/kanki.md), [Nesshi](../zokyo/nesshi.md), [Hokan](../zokyo/hokan.md) —
  are separate specs, still unbuilt. Until one calls `Aizu.postCue(...)`, Aizu
  renders **Idle only** (the correct degraded behaviour per the spec's "Degrades
  to nothing"). The seam is ready: each source is a `postCue`/`clearCue` call plus
  a priority-table row (both already defined in `AizuCore.h`).

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
