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

## 2026-07-03 — Hokan (steps, falls, PDR breadcrumb) hardware validation

Hokan ([`../zokyo/hokan.md`](../zokyo/hokan.md)) is built (firmware DSP + CSV `steps`
+ base-side `groundstation/hokan.py` PDR + a live BLE breadcrumb characteristic
`abcdf007` rendered as a mini-map in both apps). Host tests pass
(`tools/hokan-dsp-test.cpp`, `tools/hokan-pdr-test.py`), firmware compiles, Android
`detekt`+`lint` clean. What still needs the body:

- **Step counting — spot-verified, not calibrated.** A live tethered walk counted
  **~34 for a called 30** (~13% over) and held flat at rest (no drift, no false
  falls). Good enough to trust the detector, but `STEP_LEN` (0.7 m), `HOKAN_STEP_DELTA`
  (2.2 m/s²), and the debounce are first-draft defaults — calibrate over a longer
  measured walk, and against a known distance.

- **Fall detection — UNVERIFIED on hardware.** The freefall→impact→stillness state
  machine + latching Aizu SOS (rank-2 ALERT, AZ-11) and the RESOLVED-on-getting-up
  clear are host-tested only. Never triggered on the board. Do a **cushioned drop
  test**: confirm the red urgent pulse fires within ~1 s, latches, and clears when
  the board is picked up — and that normal sitting/dropping-into-a-chair doesn't
  false-trigger. Tune `HOKAN_FREEFALL_TH` / `HOKAN_IMPACT_TH` / `HOKAN_STILL_HOLD_MS`.

- **PDR breadcrumb heading — the real gap.** A house loop reconstructs as a **wobbly
  line, not a loop**. Diagnosed: step counting and the integration are correct and
  the apps draw the data faithfully (verified) — the **shape is entirely heading**,
  and indoor heading is unreliable for four stacked reasons: (1) indoor magnetic
  disturbance (steel/wiring — the HkD-3 non-goal), (2) heading is raw
  `atan2(mag.y, mag.x)` with **no tilt compensation**, (3) it's device orientation,
  not travel direction, (4) no hard/soft-iron calibration. The magnetometer reads
  *stable at rest* (parked cleanly at 171°), so it's turn-tracking that fails.
  - **To confirm:** walk a loop **on a power bank** (phone USB-C power opened the
    CDC port and flipped the board to *tethered* → the walk did NOT log to flash;
    only a power bank guarantees untethered logging of `heading_deg`+`steps`), then
    check whether the logged heading sweeps ~360°. Try it **outdoors, away from
    steel**, board held flat with a fixed forward axis, to test the algorithm clean.
  - **Real fix (forward path):** a tilt-compensated **fused heading (BNO085)** — the
    Hokan spec's own "Better heading" item — cuts PDR drift at the source.

- **BLE breadcrumb mini-map — renders, heading aside.** The `Hokan` characteristic
  and the Glass/Operator mini-maps were seen drawing live on the phone (so parse +
  render work); only the heading accuracy above is open. Glass mini-map not yet
  seen on the RayNeo (only `:operator` was installed this session).

- **Phone-power gotcha (confirmed).** On the Pixel, powering the board over USB-C
  opens the CDC serial port → the board reads as tethered → **flash logging is
  suppressed**. Field capture needs a plain power bank, not the phone.
