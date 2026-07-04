# Kiatsu (気圧) — spec

*Barometric sense: which floor you walked, and the storm the sky hasn't shown yet.*

**Status:** spec (2026-07-03) · not yet built · **Zōkyō:** Kiatsu (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) — **no change** · **Shares:** [Aizu](../platform/aizu.md) (weather cue), [Hokan](./hokan.md) (the Z-axis), the [ground-station](../../REGISTRY.md#parts-catalog) · **Date:** 2026-07-03

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Kiatsu — "atmospheric pressure" — turns the [BME688](../../REGISTRY.md#sensors)'s barometer into two
senses, drawn from the one `pressure_hpa` field that's ridden the CSV since Tanchi and done nothing:

- **A vertical dead-reckoner — the Z-axis [Hokan](./hokan.md) lacks.** Hokan reconstructs *where* you
  walked (x, y) with no GPS fix; barometric pressure tells you *which floor* — a storey is a clean
  ~0.4 hPa step. Kiatsu tags Hokan's flat path with altitude, turning a 2-D GPS-denied track into a
  **3-D one**: which floors, in what order, indoors where GPS is blind.
- **A weather precog you wear.** A **falling barometer** precedes a storm by hours — the oldest
  forecast there is. Kiatsu keeps a slow trend on-device and, when pressure drops fast, posts a calm
  cyan [Aizu](../platform/aizu.md) cue: *the weather is turning, hours out*, before the sky shows it.

Like its siblings it's **one part, expressed as sense** — a candidate **new Zōkyō** beside
[Hokan](./hokan.md), from the shared [parts catalog](../../REGISTRY.md#parts-catalog). And like each,
its value in the series is the finding it forces. Hokan's signal (gait) was *faster* than the 1.5 s
log, so it needed on-device DSP **and** a new CSV column. **Kiatsu is Hokan inverted:** its signals
are *slower* than the log, so it needs **neither** — no contract change, no real-time DSP. It is the
proof that **the log cadence decides where the work lives.** See [The cadence question](#the-cadence-question--hokan-inverted).

## Why (the thrifty case)

- **A third sense from a field you already log.** `pressure_hpa` is the deadest number in the CSV.
  Kiatsu gets a floor-tracker *and* a storm-warner out of it — no new BOM, no new column.
- **The Z-axis Hokan was missing.** Hokan gives x, y; Kiatsu gives z. Together, real **3-D indoor
  dead reckoning** — which floor of which building you crossed, GPS-denied.
- **Weather from nothing.** A tiny rolling buffer of a field you already sample yields a genuine
  storm heads-up — the calmest possible on-device computation, a subtraction over hours.
- **Runs on almost nothing.** Host + BME688 — the same minimal rig as [Kanki](./kanki.md) and
  [Kyūkaku](./kyukaku.md), a different field of the same part.

## Goals

1. **Floor detection (base-side):** from the logged `pressure_hpa`, detect storey-scale altitude
   **steps** (~0.4 hPa) and tag [Hokan](./hokan.md)'s dead-reckoned path with a floor level.
2. **Weather tendency (on-device):** keep a slow 3-hour pressure trend in a small ring buffer; when
   it falls past a threshold, post a calm **Aizu** cue (cyan) — a felt storm heads-up.
3. Do both **calibration-free and reference-free** in v1 — *relative* floor changes and *relative*
   trend, using the BME688's own `air_temp_c` to correct the altitude conversion.
4. Change the contract **not at all** — derive everything from the already-logged `pressure_hpa`.

## Non-goals

- **No contract change.** `pressure_hpa` (and `air_temp_c`) are already in the CSV and the Environment
  characteristic. Kiatsu is derivation + one output cue. ([Contract impact](#contract-impact).)
- **No absolute altitude / named floor (v1).** Barometric altitude drifts with weather, so without a
  known reference Kiatsu reports *relative* moves ("up two, down one"), not "you are on floor 3".
  Anchoring to a known ground floor or a QNH reference is [Forward path](#forward-path) ([KiD-3](#decisions)).
- **Not a barometric altimeter for fast motion.** Storey detection assumes walking/stairs/lift over
  seconds; it is not a variometer for skydiving or drone telemetry.
- **No on-device floor math.** The firmware only keeps the weather trend; **floor/altitude
  reconstruction is base-side** (`kiatsu.py`), composed onto Hokan's track — the same body/base split
  Hokan uses ([KiD-4](#decisions)).
- **No new animation engine.** The weather cue reuses [Aizu](../platform/aizu.md)'s motion vocabulary.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [BME688](../../REGISTRY.md#sensors) pressure | `pressure_hpa` — the floor step + weather trend | already logged each row |
| [BME688](../../REGISTRY.md#sensors) air temp | `air_temp_c` — temperature term in the pressure→altitude conversion ([KiD-2](#decisions)) | same reading |
| **[Aizu](../platform/aizu.md)** | renders the weather-turn cue on the NeoPixel | built (2026-07-02) |
| **[Hokan](./hokan.md)** PDR track | the x, y path Kiatsu tags with z (floor) | base-side |
| **Ground-station** (`kiatsu.py` + `analyze.py`) | floor detection + the 3-D / floor-vs-time draw | base-side |

No new libraries: `Adafruit_BME680` already services the sensor; the ground-station is Python it owns.

## Behaviour — two timescales, one field

Pressure carries two signals at **opposite timescales**, and separating them is the whole design:

**Fast (seconds–minutes) → floor steps, base-side.** Near sea level `dP/dh ≈ 0.12 hPa/m`, so
`1 hPa ≈ 8.3 m` and **one storey (~3–4 m) ≈ ~0.4 hPa**. Climbing a flight takes ~10–20 s — several
1.5 s log rows capture the ramp — so a floor change is a clean, sustained pressure **step** fully
visible *in the log*. The ground-station walks the rows, detects steps ≥ `FLOOR_HYST_HPA` (~0.3 hPa)
against a short-window reference, and increments/decrements a floor counter. Weather moves far slower
than a stair climb, so it never masquerades as a floor.

**Slow (hours) → weather tendency, on-device.** A small ring buffer samples `pressure_hpa` every
~30–60 s over `WX_WINDOW_H` (3 h) — a few hundred floats, trivial RAM. The tendency is a subtraction:
`Δ = p_now − p_3h_ago`. Floor changes wash out over three hours, so the trend sees only weather.

| Weather state | 3-h tendency | Aizu cue |
|---------------|--------------|----------|
| **Steady / rising** | `Δ ≥ −WX_FALL_HPA` (−1.0) | *(none — falls through to Aizu Idle)* |
| **Falling** | `−WX_STORM_HPA < Δ < −WX_FALL_HPA` (−1 to −3) | cyan, slow breathe — *weather turning, hours out* |
| **Falling fast** | `Δ ≤ −WX_STORM_HPA` (−3.0) | deeper cyan, slower stronger breathe — *front / storm building* |

- **The calmest cue in the system.** Weather is hours away, so Kiatsu's cue is the **lowest-priority,
  slowest** rung on Aizu — the opposite pole from a Kehai Reflex. It sits just above Idle ([AZ-13](../platform/aizu.md#decisions)).
- **Cyan is the identity.** Kanki owns green→red (stale air), Kyūkaku violet (chemical), Kiatsu **cyan**
  (weather/sky) — one more source-owned hue on the shared pixel, per the convention
  [Kyūkaku forced](../platform/aizu.md#decisions) ([KiD-5](#decisions)).
- **Temperature correction (free).** The pressure→altitude constant depends on air temperature; the
  BME688 hands us `air_temp_c` in the same reading, so the floor conversion uses the real T rather than
  a fixed 15 °C — the same "same-reading confound handled for free" move as [Kyūkaku's humidity veto](./kyukaku.md#decisions) ([KiD-2](#decisions)).
- **Relative, not absolute.** Both signals are *differences* — floor **changes** and pressure
  **tendency** — so neither needs a calibrated reference. This is why Kiatsu is cheap, and the reason
  absolute floor/altitude is out of scope for v1 ([KiD-3](#decisions)).

## The cadence question — Hokan inverted

**This is Kiatsu's finding.** [Hokan](./hokan.md) established that a signal *faster* than the 1.5 s
log cannot be recovered from it — so gait needed **on-device DSP** and a **new `steps` column**.
Kiatsu is the clean inverse and completes the rule:

- **The floor signal is slower than the log**, so it survives in the CSV untouched → **no new column**,
  and the reconstruction is **base-side** (like Hokan's path, but for z instead of x, y).
- **The weather signal is slower still**, so its on-device cost is a **subtraction over a ring
  buffer** — the *opposite* of Hokan's real-time DSP. No fast tick, no float library.

So the same body+base, live+analysis architecture Hokan introduced is reused with **zero contract
change and zero DSP**, purely because Kiatsu's signals are slow. The generalised rule: *sample rate
vs log cadence decides whether a module changes the contract and does live DSP (Hokan) or does
neither (Kiatsu).* A useful template for sizing every future sensor before writing a line of firmware.

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. Minimal — the heavy lifting is base-side.

1. **Consume the cached pressure.** `bmePressure` / `bmeHasData` / `bmePresent` are already maintained
   in `loop()`. Kiatsu reads them — no new sensor servicing, no new column.
2. **Weather ring buffer.** Push `bmePressure` into a small circular buffer every ~30–60 s (a coarse
   sub-sample of the loop, gated on `bmeHasData`); once it spans `WX_WINDOW_H`, compute the tendency
   and resolve the weather state (with hysteresis).
3. **Post, don't paint.** `postCue(KIATSU, …)` a cyan AMBIENT cue for Falling / Falling-fast;
   `clearCue(KIATSU)` when Steady/rising. Kiatsu never touches the NeoPixel — Aizu is the sole writer.
4. **Battery / idle** is Aizu's, inherited: a steady barometer posts nothing, so the pixel rests on
   Aizu's green-breathe / dark-heartbeat Idle.
5. **Standalone-safe.** BME688 absent (`bmePresent == false`) → Kiatsu posts nothing; Aizu falls
   through. The ring buffer simply never fills. Firmware still boots; other sensors unaffected.
6. **No telemetry disturbance.** No new column, no `Serial` writes on the telemetry stream, no change
   to the 1500 ms row cadence, BLE notify, or the untethered `!Serial` flash-logging gate.

## Ground-station integration

Target: `groundstation/` (`kiatsu.py` + `analyze.py`, mirroring [Hokan](./hokan.md)'s `hokan.py` split).

1. **Read `pressure_hpa`** (and `air_temp_c`) from the stitched frame.
2. **Detect floor steps:** track a short-window reference pressure; a sustained deviation ≥
   `FLOOR_HYST_HPA` is a floor transition — increment/decrement a relative floor index. Convert to
   metres with the T-corrected constant for an altitude profile.
3. **Compose with Hokan:** tag each row of Hokan's dead-reckoned (x, y) track with its floor/altitude,
   yielding a 3-D path; render as **floor bands / a Z-strip** on the timeline and, where the map
   supports it, a per-floor colouring of `route_map.html`. Degrade cleanly: no `pressure_hpa` (old
   logs / no BME688) → skip; no Hokan track → still draw the standalone altitude/floor-vs-time profile.
4. **Report line:** "crossed 4 floors (−1, +3), 118 m climbed; barometer fell 4.2 hPa over the walk
   (weather turning)."

## Contract impact

**None.** `pressure_hpa` and `air_temp_c` are already published in the CSV schema and the Environment
GATT characteristic (`abcdc0de-…`, [CONTRACT.md](../../CONTRACT.md)). Kiatsu is derivation plus one
output cue — the deliberate contrast with [Hokan](./hokan.md), which *had* to add `steps` because gait
outran the log. Kiatsu's signals fit inside it.

## Acceptance criteria

1. **Floor detection:** walking up N storeys (stairs or lift) reconstructs as +N floor transitions in
   `kiatsu.py` (within tolerance); walking a level corridor adds none.
2. **Altitude profile:** the reconstructed altitude-vs-time tracks the real climb/descent shape;
   temperature correction is applied (varying `air_temp_c` shifts the metres-per-hPa used).
3. **3-D composition:** on a GPS-denied multi-floor walk, Hokan's (x, y) path is tagged with floor
   level and drawn as a Z-strip / per-floor colouring; with no Hokan track present, a standalone
   floor-vs-time profile still renders.
4. **Weather cue:** a sustained pressure fall past `WX_FALL_HPA` over the window drives the cyan Aizu
   cue; a steady/rising barometer posts nothing (Idle); short pressure wobbles (a floor change, a
   door slam) do **not** trip the weather cue (it's a 3-h trend).
5. **Colour identity:** the weather cue is cyan and distinguishable from Kanki (green→red) and Kyūkaku
   (violet) on the shared pixel; it is the lowest-priority, calmest cue ([AZ-13](../platform/aizu.md#decisions)).
6. **Standalone:** criteria 1–2 and 4 hold on the minimal rig (host + BME688 only), no BLE central.
7. **No regression / no contract change:** CSV header/order/cadence, BLE notify, and flash-logging
   are unchanged; no new column appears; Aizu remains the sole NeoPixel writer.

## Decisions

All five opening questions are resolved; recorded here as the build contract.

- **KiD-1 — Two timescales, split by surface.** Floor steps are reconstructed **base-side** from the
  logged pressure; the weather trend runs **on-device** from a slow ring buffer feeding one Aizu cue.
  Same body/base split as Hokan, but with no contract change and no DSP ([The cadence question](#the-cadence-question--hokan-inverted)).
- **KiD-2 — Temperature-corrected conversion.** The pressure→altitude constant uses the BME688's own
  `air_temp_c` (same reading), not a fixed 15 °C — a free confound handled like Kyūkaku's humidity veto.
- **KiD-3 — Relative, not absolute (v1).** Report floor **changes** and pressure **tendency**, not an
  absolute floor number or QNH-referenced altitude. Anchoring is forward.
- **KiD-4 — Reconstruction base-side.** Floor/altitude math lives in `kiatsu.py`; the firmware only
  keeps the weather ring buffer. Mirrors Hokan's `hokan.py` split.
- **KiD-5 — Cyan colour identity + lowest Aizu rung.** Kiatsu owns cyan (weather/sky) and its cue is
  the calmest, lowest-priority ambient cue, just above Idle — the opposite pole from a Reflex. Lands as
  [AZ-13](../platform/aizu.md#decisions).
- **KiD-6 — Thresholds (tune on-site):** `FLOOR_HYST_HPA ≈ 0.3` (a storey ≈ 0.4 hPa), `WX_WINDOW_H = 3`,
  `WX_FALL_HPA = 1.0`, `WX_STORM_HPA = 3.0`; ring-buffer sub-sample ~30–60 s. Metres-per-hPa ≈ 8.3 at
  sea level, T-corrected.

## Cross-spec impact

- **CONTRACT.md** — **none.** Kiatsu is the first multi-surface module that adds *no* column — the
  deliberate counterpoint to Hokan's `steps`, showing the body/base template also works contract-free.
- **Aizu** — adds a **Kiatsu weather-turn** AMBIENT cue as the **lowest** ambient rung (cyan, calm),
  committed as [AZ-13](../platform/aizu.md#decisions); it also extends the source-hue palette (green→red / violet /
  amber / **cyan**).
- **Hokan** — Kiatsu is the first module to **build on another Zōkyō's base-side product**: it tags
  Hokan's (x, y) dead-reckoned path with z (floor), turning 2-D PDR into 3-D. No change to Hokan; the
  composition lives in `kiatsu.py`.
- **Kyūkaku** — the third BME688 sense; with Kanki (CO₂), the four fields of the environment pair are
  now each expressed (VOC → smell, pressure → altitude/weather, T/RH → the air senses).
- **Registry (build-time)** — Kiatsu earns a Zōkyō row beside Hokan; note it's the first contract-free
  multi-surface module and the Z-axis for Hokan's PDR.

## Forward path

- **Absolute floor / altitude:** anchor to a known ground floor (or a QNH reference pulled at capture
  time) to turn relative moves into named floors and true altitude.
- **PDR/GPS/baro fusion:** feed Kiatsu's z into the Hokan⇄GPS fusion so the fused indoor/outdoor track
  is fully 3-D and drift-bounded on all axes.
- **Live weather on the HUD:** mirror the tendency into the [Shikai](../../REGISTRY.md#shikai-視界--field-of-view)
  glasses via a future Aizu cue-mirror characteristic — a barometric trend arrow in the corner of the
  view.
- **Lift vs stairs:** classify a floor transition by its pressure-ramp *rate* (a lift is faster and
  smoother than stairs) — a small base-side refinement once floor detection is solid.
