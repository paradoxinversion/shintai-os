# Enmu (煙霧) — spec

*The air you can't see you're breathing — particulate load, told as a number.*

**Status:** built (2026-07-05) — plumbing complete across all seam sites; hardware bring-up pending · **Zōkyō:** Enmu (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) (**both halves** — 3 CSV columns + 1 BLE characteristic) · **Shares:** none in v1 (an [Aizu](../platform/aizu.md) cue is the [Forward path](#forward-path)) · **Date:** 2026-07-05

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Enmu — "smog / haze" — is a **particulate / air-quality sense**. It adds the
[PMSA003I](../../REGISTRY.md#sensors) (Adafruit #4632) — a laser particle counter — to the sensor
chain and publishes the three mass concentrations it measures, **PM1.0 / PM2.5 / PM10** in µg/m³,
as three new CSV columns and a new **Particulate** BLE characteristic. The consumers render it: the
phone console shows the full triad, the glasses surface the health-relevant **PM2.5** alone.

It is the sense the platform's "sixth-sense" ambition kept pointing at but never had a *number* for.
[Kyūkaku](./kyukaku.md) turned the BME688's gas plate into a *change* detector — "something entered
the air" — but a MOX resistance has no absolute scale and can't tell you *how bad the air is*, only
*that it shifted*. Enmu answers the quantitative question Kyūkaku can't: **how many micrograms of
particulate per cubic metre are you actually breathing** — wildfire smoke, roadside soot, a dusty
workshop, an unventilated print room. PM2.5 is the fraction that reaches the deep lung; it is the
number public AQI indices are built on.

## Why (the case for adding a chip)

Every Zōkyō since Tanchi's original set has been **thrifty** — a new *sense* wrung from a part already
on the chain (Kyūkaku and Kaori from the BME688 gas plate, Kiatsu from its barometer, Hokan from the
IMU, Kōei from a second copy of an existing ToF). Enmu is deliberately the **counter-case: the first
new-BOM sensor**. It earns the exception:

- **Particulate is unreachable from the existing BOM.** No sensor already on the chain measures
  suspended particle mass. The BME688 gas plate reacts to *gases* (reducing VOCs), not to *particles*
  — smoke's VOC component and its particulate load are different physical signals. There is no thrifty
  path to µg/m³; it requires a part that optically counts particles ([EN-1](#decisions)).
- **It completes the air-sense trilogy.** The platform now reads air three ways —
  [Kanki](./kanki.md)/SCD-40 for **CO₂** (is it stuffy?), [Kyūkaku](./kyukaku.md)/BME688 for **VOCs**
  (did something chemical enter?), and Enmu for **particulate** (what am I inhaling?). Three
  orthogonal hazards, three sensors; none substitutes for another.
- **It exercises the seam the leanest way a real sensor can.** Enmu is the cleanest possible
  *both-halves* contract change: three end-appended CSV columns and one new string characteristic,
  no existing field touched, no binary payload, no MTU concern the string chars don't already meet.
  It is the template for "add a sensor" once the thrifty senses are exhausted.
- **Plain bus, no mux.** The PMSA003I answers at `0x12`, a free address — unlike Kōei's twin ToFs it
  needs no PCA9546 channel. It drops onto the STEMMA QT chain and is read like the BME688.

## Goals

1. Read the PMSA003I's PM1.0 / PM2.5 / PM10 **atmospheric** mass concentrations each telemetry cycle
   and publish them as `pm1_0_ugm3` / `pm2_5_ugm3` / `pm10_ugm3` (CSV + flash log).
2. Notify a **Particulate** BLE characteristic (`PM1.0:.. PM2.5:.. PM10:.. ug/m3`) — the live twin of
   those columns — whenever the sensor has data.
3. Render it on **both** consumers: the Operator console shows the full triad; the Glass HUD surfaces
   **PM2.5** alone (the lean-surface rule — one health-relevant number, not three).
4. Degrade cleanly: absent sensor → blank columns, no notify, no crash; old logs without the columns
   still parse (append-at-end, [EN-2](#decisions)).
5. Add the sensor to the ground-station capture panel, HUD traces, stats, and the Bunshin authority
   table — the full base-side mirror.

## Non-goals

- **No on-body cue (v1).** Enmu does **not** post to [Aizu](../platform/aizu.md) — the NeoPixel stays
  with the senses that own it. A haze→alarm particulate cue (PM2.5 banded like Kanki's CO₂ ramp) is
  the natural next step but is deferred to the [Forward path](#forward-path) ([EN-5](#decisions)).
- **No AQI index computation.** Enmu publishes raw µg/m³, not a US-EPA/EU AQI number. The
  µg/m³→AQI breakpoint math is a consumer-side / derived concern, not a contract field
  ([EN-3](#decisions)).
- **Not a calibrated reference instrument.** A consumer laser counter drifts and saturates; Enmu is a
  *heads-up* on relative particulate load, not a certified monitor.
- **No particle-count bins.** The PMSA003I also reports per-size *count* histograms (>0.3 µm … >10 µm
  per 0.1 L). Those are more than the seam needs; Enmu publishes only the three **mass**
  concentrations ([EN-4](#decisions)).
- **No standard-vs-atmospheric toggle.** Enmu takes the sensor's **atmospheric** ("environmental")
  values — correct for ambient air — and never the "standard" reference-condition set.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [PMSA003I](../../REGISTRY.md#sensors) | particulate input — PM1.0/2.5/10 µg/m³ over I²C `0x12` | **new BOM** (Adafruit #4632) |
| [QT Py ESP32-S3 host](../../REGISTRY.md#host--infrastructure) | reads it on the plain STEMMA QT bus (no mux), publishes CSV + BLE | already the host |
| [Operator + Glass](../../REGISTRY.md#output--feedback) | render the readout (full triad / PM2.5) | already built |

New firmware library: `Adafruit_PM25AQI` (pinned in `firmware/shintai-os/sketch.yaml`). Transitive
`Adafruit BusIO` is already pinned. No new Android or Python dependency — the readout is a plain
string channel and three numeric columns.

## Behaviour — read, publish, render

The PMSA003I free-runs, buffering a fresh frame roughly once a second. The firmware polls it each
`loop()` (`Adafruit_PM25AQI::read()` returns false until a frame is ready) and caches the last good
reading, exactly as the SCD-40 and BME688 are cached between their slower updates. On a fresh frame:

- **CSV / flash:** the three atmospheric concentrations are appended to the row (blank when the sensor
  is absent or has not yet delivered a frame).
- **BLE:** the **Particulate** characteristic notifies `PM1.0:<n> PM2.5:<n> PM10:<n> ug/m3` (labeled
  ASCII tokens; the consumer splits on the labels). It notifies only while the sensor supplies data.
- **Serial (human):** an `AQI :` line joins the readout block for on-wrist tuning.

**Naming.** The columns encode the micron size explicitly — `pm1_0_ugm3`, `pm2_5_ugm3`, `pm10_ugm3`
— rather than the Adafruit driver's terse `pm10/pm25/pm100` (where `pm10` confusingly means PM1.0).
The contract is prose-first and read by humans; the size must be unambiguous on the page
([EN-6](#decisions)). The firmware maps the driver fields with a comment at the read site:
`pm10_env → pm1_0`, `pm25_env → pm2_5`, `pm100_env → pm10`.

**Consumer split.** Both apps subscribe to the Particulate channel (keeping the contract's "both take
all string channels" invariant true), but render it differently: the **Operator** shows the full
`PM1.0 / PM2.5 / PM10` triad in the Air panel beside Climate and Environment; the **Glass** HUD shows
only `AIR PM2.5` — the single fraction that matters for a glanceable heads-up — via the `:core`
`formatPm25` helper. This mirrors how the glasses already keep their surface lean (they take
Environment only for the derived smell spike, never its raw readout).

## Contract impact

**Both halves — the leanest a real new sensor can be** ([CONTRACT.md](../../CONTRACT.md)):

- **CSV half:** three columns `pm1_0_ugm3` / `pm2_5_ugm3` / `pm10_ugm3`, **appended at the end** after
  `board`. No existing column moves; consumers key on names; pre-Enmu logs still parse. (Same posture
  as `steps` and `board`.)
- **BLE half:** one new **Particulate** string characteristic, `abcdd057-…` (`d057` ≈ "dust"),
  `READ | NOTIFY` like every other string char — no binary payload, no new MTU requirement (its ~31-
  byte payload sits well inside the negotiated 247 the string chars already use).
- **Multi-producer:** the three columns join the **air-chem** authority row (**aft → fwd** — chemistry
  rides the pack), mirrored in all three merge sites: `CONTRACT.md`, `groundstation/analyze.py`
  `AUTHORITY`, and `:core` `Merge.kt` (`Channel.Particulate`).
- **Linter:** `tools/check-contract.py` gains `Particulate → PARTICULATE` in `CHAR_TO_KOTLIN` and a
  `ugm3` unit suffix, so the new column/characteristic are guarded against drift like the rest.

## Acceptance criteria

1. **Present + reading:** with the PMSA003I attached, `pm1_0_ugm3` / `pm2_5_ugm3` / `pm10_ugm3` carry
   plausible µg/m³ in the CSV and flash log, and the **Particulate** characteristic notifies the
   matching `PM1.0:.. PM2.5:.. PM10:.. ug/m3` string.
2. **Response:** a particulate source in front of the sensor (incense, a match blown out, aerosol
   dust) drives PM2.5 up within a few seconds and it decays as the air clears.
3. **Absent → clean degrade:** with the sensor unplugged the columns are blank, the characteristic is
   silent, the board boots and every other sensor is unaffected (non-fatal presence gate).
4. **Old logs parse:** a pre-Enmu log (no `pm*` columns) still loads in `hud.py` / `analyze.py`
   without error; the PM traces/stats simply don't appear.
5. **Operator:** the Air panel shows the full `PM1.0 / PM2.5 / PM10` readout when the channel streams.
6. **Glass:** the HUD shows `AIR PM2.5 <n> µg/m³`; `—` before the first frame.
7. **Bunshin:** on a two-pod rig the particulate channel resolves **aft → fwd** identically on the
   glasses, the Operator, and `analyze.py` (all three merge sites agree).
8. **Contract clean:** `python3 tools/check-contract.py` passes; `firmware/verify.sh` compiles;
   `android/build.sh detekt lint` is clean.

## Decisions

- **EN-1 — New BOM is justified, not a regression of the thrifty ethos.** Particulate mass is
  physically unreachable from any part on the chain; the PMSA003I is the minimum addition that reads
  it. Enmu is intentionally the platform's first "add a sensor" case study.
- **EN-2 — Append at the end.** The three columns go after `board`, so column *order* is undisturbed
  and old logs parse — the discipline `steps` and `board` established.
- **EN-3 — Raw µg/m³, not AQI.** The contract carries the physical measurement; turning it into a
  banded AQI index (or a colour) is a consumer/Forward-path concern, kept out of the seam.
- **EN-4 — Mass concentrations only.** PM1.0/2.5/10 µg/m³, not the per-size particle-count bins — the
  three numbers a wearer and the analysis actually use; the histograms are omitted.
- **EN-5 — No Aizu cue in v1.** Enmu ships as a *readout*, not an on-body reflex. The NeoPixel already
  carries Kanki (CO₂) and Kyūkaku (VOC); a third air cue needs its own colour-identity and ranking
  decision (an Aizu `AZ-` row), which the [Forward path](#forward-path) will make deliberately rather
  than bolt on here.
- **EN-6 — Explicit micron naming.** `pm2_5_ugm3`, not the driver's ambiguous `pm25`. Readability of
  the human-facing contract wins over matching the library's identifiers.
- **EN-7 — Atmospheric values.** Take the sensor's "environmental" concentrations (ambient-air
  corrected), never the "standard" reference-condition set.

## Cross-spec impact

- **Registry** — Enmu earns a [Zōkyō](../../REGISTRY.md#zōkyō) row and the PMSA003I earns a
  [parts-catalog](../../REGISTRY.md#sensors) sensor row (first new sensor added to the catalog since
  Tanchi); [Tanchi](../../REGISTRY.md#tanchi-探知--detection)'s parts list gains the PMSA003I.
- **Contract** — three CSV columns + one BLE characteristic + one authority-table row (above).
- **Kyūkaku / Kanki** — no code change; Enmu completes the air-sense trilogy alongside them (CO₂ · VOC
  · particulate). On a full rig the three coexist: two readouts (CO₂, particulate) plus one reflex
  (VOC spike).
- **Bunshin** — the particulate channel is added to the multi-producer merge as air-chem (aft → fwd)
  in all three mirror sites.

## Forward path

- **Aizu haze cue.** Band PM2.5 (good → moderate → unhealthy, the AQI breakpoints) and post a calm→
  alarm particulate colour to [Aizu](../platform/aizu.md) — a third air-sense on the shared pixel,
  needing its own hue identity distinct from Kanki's green→red CO₂ ramp and Kyūkaku's violet, and an
  `AZ-` ranking row. The [EN-5](#decisions) decision deliberately reserved this.
- **AQI badge on the HUD.** A consumer-side µg/m³→AQI conversion with a coloured badge, so the glasses
  show "AQI 74 — Moderate" instead of a bare number — echoing how the Operator colours CO₂.
- **Sensor upgrade path.** The [SEN5x](../../REGISTRY.md#sensors) folds particulate **and** VOC/NOx/RH/T
  into one part — a future consolidation that could let one chip back both Enmu and (some of) Kyūkaku.
- **Duty-cycling.** The PMSA003I's laser and fan have a limited lifetime; a future build can gate the
  sensor to periodic sampling rather than continuous run to extend it — a power/longevity refinement,
  no contract change.
