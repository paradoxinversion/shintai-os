# Hokan (歩勘) — spec

*Step-reckoning: count steps, catch falls, and reconstruct where you walked — even with no GPS fix.*

**Status:** spec (unbuilt) · **Zōkyō:** Hokan (candidate — sibling to [Rokkan](../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../CONTRACT.md) — **changes it** (CSV half)

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Hokan — "step-reckoning" — turns the [IMU](../REGISTRY.md#sensors) into a **pedometer, fall detector,
and pedestrian dead-reckoner**. Its headline is the one capability nothing else in the series has:
**positioning without GPS.** Count steps from the accelerometer, take heading from the magnetometer,
and integrate `step_length × heading` step by step — and you get a **reconstructed walked path in a
GPS-denied space** (indoors, in a canyon, underground), where `lat`/`lon` are blank. Falls fire a
live SOS through [Aizu](./aizu.md); the path is drawn base-side by the [ground-station](../REGISTRY.md#parts-catalog).

It's the odd one out on three axes, each a genuine first for the series — see
[Three firsts](#three-firsts):
1. **First on-device real-time DSP.** Steps live at ~2 Hz; the log cadence is 1.5 s, so steps
   **cannot** be derived from the CSV after the fact — the firmware must sample fast and detect
   steps live.
2. **First CSV-half contract change.** It adds a `steps` column — realizing the CSV counterpart that
   [Kiroku](./kiroku.md) only flagged (Metsuke changed the BLE half).
3. **First multi-surface module.** Output splits across the **live body** (Aizu fall SOS) *and*
   **base-side analysis** (the dead-reckoned path).

## Why (the thrifty case)

- **GPS-denied nav from a sensor you own.** The LSM6DSOX + LIS3MDL already on the rig give a walked
  track where GPS goes blind — no new BOM. This is the one *new capability* left in the parts bin.
- **Falls for free.** Once the IMU is sampled fast, the freefall→impact→stillness signature is cheap
  to detect — a real safety feature riding [Aizu](./aizu.md)'s existing SOS path.
- **The ground-station already draws tracks.** `analyze.py` maps GPS routes; Hokan feeds it a
  *dead-reckoned* track to draw the same way when there's no fix.

## Goals

1. Detect steps + cadence on-device from a fast-sampled IMU; maintain a cumulative step count.
2. Detect falls (freefall → impact → stillness) and fire an **Aizu SOS** cue live.
3. Log `steps` so the ground-station can **dead-reckon a path** (`Δsteps × step_length @ heading`)
   through GPS-denied stretches.
4. Change the contract **once, cleanly** (CSV half: `steps`), respecting the three-mirror discipline.

## Non-goals

- **Not survey-grade.** Dead reckoning **drifts** — magnetometer heading is disturbed indoors by
  steel/electronics, and step length is estimated. Hokan gives "roughly where I walked," not a
  surveyed position ([HkD-3](#decisions)).
- **No GPS fusion (v1).** Snapping the PDR track to GPS when a fix returns is [Forward path](#forward-path);
  v1 is raw dead reckoning (the GPS-denied case is the whole point).
- **No BLE for steps (v1).** `steps` goes in the **CSV** only; a live step/heading BLE characteristic
  (for a HUD mini-map) is forward, not v1 — so the contract change is CSV-only.
- **No on-device path integration.** The firmware counts steps; the **ground-station** does the
  position math (testable base-side, reuses the map). Firmware stays lean.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [LSM6DSOX](../REGISTRY.md#sensors) | fast accel → step + fall detection | sampled live in `loop()` |
| [LIS3MDL](../REGISTRY.md#sensors) | heading for each step segment | `heading_deg` |
| **Onboard NeoPixel** | fall SOS cue | via [Aizu](./aizu.md) |
| **Ground-station** (`analyze.py`) | dead-reckon + draw the path | base-side |

## Behaviour — steps, falls, the path

**Step detection (on-device, fast).** Sample `accel_mag = √(ax²+ay²+az²)` every loop iteration (the
loop already spins far faster than the 1.5 s telemetry gate — the same "fast tick" pattern
[Kehai-Hikari](./kehai-hikari.md) uses). A step is a rhythmic |g| oscillation ~1.5–2.5 Hz: detect
peaks above a dynamic threshold with a minimum inter-step interval (debounce) to reject jitter.
Maintain a cumulative `steps` counter and a running `cadence` (steps/min).

**Fall detection (on-device, live).** The classic signature: `accel_mag` **dips toward 0** (freefall)
→ **spikes** (impact) → **goes still** for a moment. On match, post an **Aizu ALERT cue** (SOS — red
pulse) and latch it until cleared. This is the safety payoff and reuses Aizu's cue bus.

**Dead reckoning (base-side).** The firmware logs cumulative `steps` per row; each row also has
`heading_deg`. The ground-station walks the rows: for each `Δsteps` between rows, advance the
position by `Δsteps × step_length` along that row's heading. Result: an (x, y) track anchored at the
last known GPS point (or the origin), drawn on the map — a path through the GPS-blank stretch.

**Outputs.**
- **Live:** fall → Aizu SOS.
- **Base-side:** the dead-reckoned track on `route_map.html` (a distinct style from the GPS trace),
  step/cadence on the timeline, and headline counts ("4,210 steps, 1 fall") in the report.

## Three firsts

Each spec has contributed one architectural finding; Hokan contributes three at once because it's the
first to need **fast sensing, a logged derivation, and two surfaces** together:

- **On-device DSP.** Every prior module either read a slow sensor (SCD-40), a thresholded value
  (ToF), or worked post-hoc on logs (Kiroku). Hokan's signal (gait) is **faster than the log
  cadence**, so detection *must* run live in firmware. This generalizes Kehai's fast reflex tick
  into "the firmware does real-time signal processing," not just sampling.
- **CSV-half contract change.** Steps can't be recovered from 1.5 s samples, so `steps` must be
  *logged*. That's the CSV-schema change [Kiroku](./kiroku.md) flagged as forward and
  [Metsuke](./metsuke.md) mirrored on the BLE side — Hokan is the one that actually lands it. Mirror
  sites: `CONTRACT.md` CSV table · firmware `CSV_HEADER` · ground-station column parsing (no Kotlin —
  CSV, not BLE).
- **Multi-surface.** Fall SOS is *live on the body* (Aizu); the path is *base-side* (ground-station).
  The first module whose expression spans both — a template for future safety-plus-analysis modules.

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`.

1. **Fast IMU sampling.** Read the LSM6DSOX every loop iteration (ungated, alongside `GPS.read()`),
   or via its FIFO at ~26–52 Hz. Feed `accel_mag` to the step + fall detectors each sample.
2. **Detectors.** Step: peak-pick with dynamic threshold + min inter-step interval. Fall:
   freefall-dip → impact-spike → stillness state machine. Keep both cheap (no float DSP libs needed).
3. **Log `steps`.** Add cumulative `steps` to the CSV row (and the flash log) — one new column at the
   end of `CSV_HEADER` (append-only, so existing parsers that key on column *names* stay safe).
4. **Fall → Aizu.** Post an ALERT SOS cue via Aizu on a detected fall; clear on stillness-resolved or
   a mute [gesture](./aizu.md#input--the-boot-button).
5. **Untethered-safe.** Steps log to flash on battery like every other field value — riding
   [the durability fix](./fix-flash-log-durability.md). No change to the telemetry cadence.

## Ground-station integration

Target: `groundstation/` (`analyze.py` + a small `hokan.py`, mirroring [Kiroku](./kiroku.md)'s split).

1. **Read `steps`** from the stitched frame; compute `Δsteps` per row.
2. **Integrate** `Δsteps × STEP_LEN @ heading_deg` into an (x, y) track, anchored at the last GPS fix
   (or origin) — active through GPS-blank stretches.
3. **Draw** the dead-reckoned track on `route_map.html` (distinct style), step/cadence on the
   timeline, counts in the report. Degrade: no `steps` column (old logs) → skip cleanly.

## Contract impact

**Changes the CSV half.** Adds one column, `steps` (cumulative count, integer), appended to the CSV
schema. Update the three mirror sites: [CONTRACT.md](../CONTRACT.md) CSV table, firmware `CSV_HEADER`,
and any hardcoded ground-station column handling. **Append at the end** so consumers that key on
column *names* (and the `line[0].isdigit()` framing) are unaffected. The BLE GATT and flash framing
are untouched. (A `cadence` column is optional — [HkD-5](#decisions).)

## Acceptance criteria

1. **Steps:** walking N steps increments `steps` by ≈N (within a small tolerance); standing still
   adds none.
2. **Falls:** a simulated fall (drop + impact) fires an Aizu SOS within ~1 s; normal walking/sitting
   does not false-trigger.
3. **Dead reckoning:** a walked loop with no GPS fix reconstructs as a closed-ish track whose *shape*
   matches the route (drift acknowledged), drawn on `route_map.html`.
4. **Anchoring:** where GPS *is* present, the track starts from the last fix; where it drops, PDR
   carries on.
5. **Contract discipline:** `steps` appears in CONTRACT.md, `CSV_HEADER`, and the ground-station
   parser; old logs without it still analyze.
6. **No regression:** telemetry cadence, BLE, flash framing, and existing analyze outputs unchanged.

## Decisions

All five opening questions are resolved; recorded here as the build contract.

- **HkD-1 — Per-iteration IMU polling.** Sample the LSM6DSOX every loop iteration to start (the loop
  spins well above gait rate); move to the FIFO/interrupt only if the sample rate proves unstable.
- **HkD-2 — Fall SOS in v1.** Include fall detection + an Aizu SOS cue — cheap once the IMU is
  fast-sampled, and it's the safety payoff.
- **HkD-3 — Constant step length.** `STEP_LEN` ≈ 0.7 m, calibratable; adaptive (Weinberg) is an upgrade.
- **HkD-4 — PDR math base-side.** Position integration lives in `hokan.py`; the firmware only counts.
- **HkD-5 — Log `steps` only.** No `cadence` column in v1 (it's derivable base-side from Δsteps/Δt).

## Cross-spec impact

- **CONTRACT.md** — the first **CSV-half** change (`steps`); pairs with Metsuke's BLE-half change so
  the series now exercises both halves of the contract with real modules.
- **Aizu** — adds a **Hokan fall SOS** ALERT cue source, committed as the top-tier rung
  [AZ-11](./aizu.md#decisions) (rank 2, co-critical with Kehai Reflex; latches until resolved).
- **Kiroku** — shares the IMU/GPS sensing and the base-side-analysis surface; Kiroku detects *events*
  post-hoc, Hokan detects *steps* live and logs them. Complementary, not overlapping.
- **Registry (build-time)** — Hokan earns a Zōkyō row; note it's the first contract-touching *and*
  multi-surface module.

## Forward path

- **PDR ⇄ GPS fusion:** snap/anchor the dead-reckoned track to GPS whenever a fix returns, bounding
  drift — a true indoor/outdoor continuous track.
- **Adaptive step length** (Weinberg/Kim) for better distance accuracy on varied gait.
- **Live HUD mini-map:** a step/heading BLE characteristic feeding a [Shikai](../REGISTRY.md#shikai-視界--field-of-view)
  breadcrumb — Hokan's path shown live in the glasses (the BLE-half change it avoided in v1).
- **Better heading:** a fused IMU (BNO085) for tilt-compensated heading, cutting PDR drift at the
  source.
