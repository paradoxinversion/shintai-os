# Kiroku (記録) — spec

*The record: turn every untethered session into a reviewable black box — where you braked, cornered, and crashed, pinned on the map.*

**Status:** spec (unbuilt) · **Zōkyō:** Kiroku (candidate — realized through the [ground-station](../REGISTRY.md#parts-catalog), base-side) · **Seam:** [CONTRACT.md](../CONTRACT.md) (no change, v1)

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Kiroku — "the record" — reads a session's flash log and reconstructs it as a **trip black box**:
it detects **g-force events** (hard braking, hard cornering, jolts, impacts/crashes) from the
already-logged motion + GPS, and **pins them on the route map and the timeline**, each annotated
with the speed and the environmental context (air temp / humidity / CO₂ / heat) at that instant.
After a ride, hike, or drive you get a map of *where the interesting moments were* — not just a
breadcrumb trail.

It is the **payoff of untethered logging**: the rig already writes GPS + speed + accel + climate +
thermal to flash while on battery, and — thanks to [the durability fix](./fix-flash-log-durability.md)
— those logs now survive a power cut and pull cleanly. Kiroku is what you *do* with them.

Two things make it the odd one out in the series, deliberately:
- **Its surface is the [ground-station](../REGISTRY.md#parts-catalog), not the body.** Where Metsuke
  outputs to the glasses (Shikai) and Kehai/Kanki/Nesshi to the NeoPixel (Aizu), Kiroku outputs to
  **`analyze.py` / the route map** — base-side. It completes the output taxonomy: NeoPixel · glasses
  · **base-side analysis**.
- **First real use of the [IMU](../REGISTRY.md#sensors) as more than raw accel.** Kehai used ToF,
  Kanki the SCD-40, Nesshi/Metsuke the thermal cam — the LSM6DSOX has only ever been logged. Kiroku
  interprets it: acceleration magnitude → events.

## Why (the thrifty case)

- **Pure software on data you already have.** v1 is Python over the existing `accel_*`, `gps_*`,
  `speed_kmh`, and climate columns — **no firmware change, no contract change, no new BOM.** The
  most thrifty module in the series.
- **`analyze.py` already does the hard part.** It stitches every session onto an absolute timeline
  (`file_start + timestamp_ms`), and already draws `route_map.html`, `route.png`, `timeseries.png`;
  `hud.py` already computes `accel_mag`. Kiroku adds an *events* pass and pins to what exists.
- **Makes the untethered black box worth having.** The durability fix guaranteed the data survives;
  Kiroku makes the data *mean something*.

## Goals

1. Detect g-force / kinematic **events** from a stitched session: hard-brake, hard-accel, hard-turn,
   and jolt/impact.
2. **Pin them** on `route_map.html` (coloured by type) and mark them on the timeline, with a printed
   **event report** (type, time, lat/lon, speed, context).
3. Run entirely **base-side** on existing logged columns — contract-clean, no firmware.
4. Fuse the two motion sources sensibly: **GPS for smooth kinematics** (brake/accel/turn), **IMU for
   sharp impacts** (jolt/crash).

## Non-goals

- **No contract change (v1).** Detection is post-hoc on existing columns. A firmware-side `event`
  column + a live crash-alert is [Forward path](#forward-path), not v1.
- **No orientation fusion (v1).** We do **not** resolve the IMU into a world frame to classify
  brake-vs-corner — that needs mag+gyro fusion and is fragile. Direction comes from **GPS**; the IMU
  supplies impact *magnitude* only ([KrD-4](#decisions)).
- **Not real-time.** Kiroku is review-after, not a live warning (that's the [forward hook](#forward-path)
  into Aizu).
- **Not a certified accelerometer log.** It flags *notable* events by threshold, not a calibrated
  crash reconstruction.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [LSM6DSOX](../REGISTRY.md#sensors) | acceleration magnitude → jolt/impact events | `accel_*` columns |
| [PA1010D GPS](../REGISTRY.md#sensors) | speed & heading → brake / accel / turn events; position for pins | `gps_*`, `speed_kmh` |
| [SCD-40](../REGISTRY.md#sensors) / [MLX90640](../REGISTRY.md#sensors) | per-event context annotation | `air_temp_c` / `co2_ppm` / `hotspot_delta` … |
| **Ground-station** (`analyze.py`) | detects, pins, reports | base-side, [shared](../REGISTRY.md#parts-catalog) |

No worn feedback Tsukiwaza — the host produces the log; the base-side tooling makes the record.

## Behaviour — events, pins, report

**Two detectors, fused.**

- **GPS kinematics (smooth events).** From the absolute-time series:
  - **Hard brake / hard accel:** large `d(speed)/dt` (deceleration / acceleration beyond a threshold).
  - **Hard turn:** large `d(heading)/dt` while `speed` is above a floor (a sharp turn at speed, not a
    slow pivot). Heading from `heading_deg`; speed-gated to avoid noise at rest.
- **IMU impacts (sharp events).** From `accel_mag = √(ax²+ay²+az²)` (already in `hud.py`): a **spike**
  well above the ~9.8 m/s² resting baseline (a pothole, a drop, a crash), and optionally a **freefall
  dip → spike** signature for a fall. IMU catches what GPS's 1 Hz smoothing misses.

**Thresholds** (tunable consts — [KrD-1](#decisions)):
- impact: `accel_mag` excursion `> ~2.5 g` (≈ 25 m/s²) from baseline;
- hard brake/accel: `|Δspeed| > ~0.4 g`-equivalent over 1 s;
- hard turn: `> ~30 °/s` while `speed > ~10 km/h`.
Apply a **refractory window** (e.g. merge events within ~2 s) so one bump isn't ten events.

**Outputs** (extend the existing pipeline):
- **`route_map.html`** — a marker at each event's lat/lon, coloured by type (brake / accel / turn /
  impact), popup = time, speed, and context (temp/humidity/CO₂/heat) at that row.
- **`timeseries.png`** — event times marked on the axis (vlines), so a g-spike lines up visually with
  the speed/accel traces.
- **Printed report** — a table: `type · absolute time · lat,lon · speed · context`, plus headline
  counts ("3 hard brakes, 1 impact") — mirroring `analyze.py`'s existing gap/stats summary.

## The ground-station seam (the architectural point)

Kiroku is the first module whose **output surface is the ground-station**, and that surfaces a real
taxonomy question the series hadn't hit: **the ground-station is defined as "shared base-side tooling,
not a worn Tsukiwaza of any one Zōkyō"** — so is a base-side-only augmentation a **Zōkyō**, or just a
**feature of the ground-station**?

Kiroku's answer (proposed): it's a **Zōkyō whose expression Tsukiwaza is base-side** — its *sensing*
is worn (the same IMU/GPS Rokkan's Tanchi uses), but its *expression* is a new **analysis lens** on
the shared ground-station rather than a worn feedback module. So the ground-station stays shared
infrastructure; Kiroku is a lens that runs on it. This parallels Metsuke (whose expression rides
Shikai) and keeps the "shared tooling" invariant intact. Flagged in [Cross-spec impact](#cross-spec-impact)
as a registry framing note.

## Analysis integration

Target: `groundstation/analyze.py` (+ possibly a small `groundstation/kiroku.py` it calls).

1. **Reuse the stitch.** Consume `analyze.py`'s existing absolute-time combined frame (it already
   builds `combined.csv`); don't re-implement session stitching.
2. **Events pass.** Compute the GPS-kinematic and IMU-impact events with a refractory merge; return a
   small events table.
3. **Pin + annotate.** Add markers to `route_map.html`, vlines to `timeseries.png`, and print the
   report — reusing the existing folium/matplotlib outputs rather than new artifacts.
4. **Degrade gracefully.** No GPS fix in a session → only IMU impacts (no map pins, still a timeline +
   report). No motion of note → "no events" (like `hud.py`'s graceful-empty behavior).

## Contract impact

**None (v1).** Kiroku reads only columns already in the CSV schema. The CSV/BLE contract is untouched.

*(Forward, for symmetry:* if firmware-side live detection is added, an `event` CSV column would be
the **CSV-schema counterpart to Metsuke's BLE-schema change** — the two would be the only
contract-touching modules, one per half of the contract. See [Forward path](#forward-path).)*

## Acceptance criteria

1. **Events found:** a session with hard braking and a jolt produces the corresponding events at the
   right times (verified against the `timeseries.png` traces).
2. **Pinned + annotated:** each event appears on `route_map.html` at its lat/lon, coloured by type,
   with speed + context in the popup.
3. **Fusion:** GPS supplies brake/accel/turn; the IMU supplies impacts GPS smoothing misses; one
   physical bump yields one event (refractory merge), not many.
4. **Report:** a printed table + headline counts prints alongside `analyze.py`'s existing summary.
5. **Contract-clean:** no firmware change, no CSV/BLE schema change; existing `analyze.py` outputs
   still build.
6. **Degrades:** a no-GPS session still yields IMU events + timeline + report (no crash, no map pins).

## Decisions

All five opening questions are resolved; recorded here as the build contract.

- **KrD-1 — Thresholds (tunable).** Impact `> ~2.5 g` excursion, brake/accel `> ~0.4 g`-equivalent
  over 1 s, hard turn `> ~30 °/s` above `~10 km/h`, ~2 s refractory merge. Ship as consts; calibrate
  on a real session.
- **KrD-2 — A `kiroku.py` module.** The events logic lives in a small `groundstation/kiroku.py` that
  `analyze.py` calls — keeps `analyze.py` readable and the detection testable in isolation.
- **KrD-3 — Pins on the existing map.** Events pin onto `route_map.html`; a standalone report only if
  it gets busy.
- **KrD-4 — IMU magnitude only.** GPS supplies direction (brake/accel/turn); the IMU supplies impact
  magnitude. No orientation fusion in v1.
- **KrD-5 — Base-side only (v1).** No firmware-side detection, Aizu SOS, or `event` column in v1; the
  live hook is a follow-on once thresholds are validated on real data.

## Cross-spec impact

- **Registry (build-time)** — Kiroku earns a Zōkyō row, noted as **base-side-expressed** (its lens
  runs on the shared ground-station, which stays shared, not a worn Tsukiwaza). This is the first
  Zōkyō whose expression isn't worn.
- **Metsuke** — the two are the contract's mirror image: Metsuke touches the **BLE** half (a live
  characteristic), Kiroku's *forward* `event` column would touch the **CSV** half. Neither is needed
  for v1.
- **`analyze.py` / `hud.py`** — Kiroku reuses their stitch + `accel_mag` + map/timeline outputs; a
  small events module is the only new code.

## Forward path

- **Live crash-alert:** firmware-side impact detection → an Aizu SOS cue (red pulse) at the moment,
  and an `event` marker written to the flash row (the CSV-schema contract change). Turns the black
  box into a live safety reflex too.
- **Richer classification:** with a fused-orientation IMU (BNO085 on the upgrade menu) or on-board
  sensor fusion, classify brake/corner/impact directly in the device frame.
- **Session scoring:** a "smoothness" score per trip (event density), and a `shintaikiroku` launcher
  alongside the existing ground-station aliases.
