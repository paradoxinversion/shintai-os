# Kanki (換気) — spec

*A wearable ventilation sense: the air you're rebreathing, told as light.*

**Status:** spec (unbuilt) · **Zōkyō:** Kanki (candidate — sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) · **Shares:** the feedback layer from [Kehai-Hikari](./kehai-hikari.md) · **Date:** 2026-07-01

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Kanki — "ventilation" — is a **standalone air-quality guardian** you wear all day. It reads the
[SCD-40](../../REGISTRY.md#sensors) CO₂ level already published as `co2_ppm` and expresses it on the
**onboard NeoPixel**: calm green when the air is fresh, escalating to red as CO₂ climbs and the
room goes stale, drowsy, and dumb. No screen, no phone — a glanceable "open a window" cue on your
body.

Unlike [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense) (the full environmental-perception suite),
Kanki is **one sense, one cue**. It runs on the minimal rig — just the [QT Py host](../../REGISTRY.md#host--infrastructure)
+ SCD-40 — because the firmware already presence-gates every other sensor and warms-and-continues
when they're absent. That makes Kanki a candidate **new Zōkyō**, a small sibling to Rokkan drawn
from the same [parts catalog](../../REGISTRY.md#parts-catalog).

**Why write it second (the real reason):** Kanki is the first module to *reuse* the NeoPixel
feedback layer [Kehai-Hikari](./kehai-hikari.md) introduces. Two Zōkyō now want the **one** onboard
RGB pixel. That collision is the point — it tells us whether Kehai's `driveReflex()` seam is shaped
right. It isn't, quite: see [The shared feedback layer](#the-shared-feedback-layer).

## Why (the thrifty case)

- **Zero new BOM.** SCD-40 is already on the chain; CO₂ is already in the contract; the NeoPixel is
  on the board. Kanki is pure firmware over parts you own.
- **A real device from the least-used sensor.** The SCD-40 is the most product-worthy part in the
  bin and today it's buried mid-CSV. Kanki makes it a wearable people would actually pay for.
- **Runs on almost nothing.** Host + one I²C sensor. The lightest, cheapest Zōkyō to wear.
- **Exercises the seam.** Building it now hardens the shared feedback layer before three more
  remixes pile onto it.

## Goals

1. Map SCD-40 `co2_ppm` to a calm, glanceable NeoPixel state the wearer reads peripherally.
2. Run standalone on the minimal rig (host + SCD-40), every other sensor absent.
3. Behave as an all-day wearable: battery-frugal, non-annoying (calm motion, not a frantic pulse).
4. **Share the onboard LED with Kehai-Hikari** through one arbiter, without either module
   clobbering the other.
5. Add nothing to the contract — derive entirely from the existing `co2_ppm`.

## Non-goals

- **No contract change.** Output-only from existing `co2_ppm`. No new CSV column, no new GATT
  characteristic. ([Contract impact](#contract-impact).)
- **CO₂ only in v1.** The SCD-40 also gives air temp + humidity; expressing *comfort* (temp/RH) is a
  possible second channel but **out of scope** here ([KD-3](#decisions)).
- **Not a calibrated instrument.** Kanki signals *bands*, not a certified ppm readout — the number
  is already in the CSV/BLE stream for anyone who wants it.
- **No new animation engine.** Reuses the feedback layer's renderer; Kanki only posts *intent*.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [SCD-40](../../REGISTRY.md#sensors) | CO₂ input (`co2_ppm`; updates ~every 5 s) | already read in `loop()` |
| **Onboard NeoPixel** | the shared feedback surface | on the [host board](../../REGISTRY.md#host--infrastructure) |
| **Shared feedback arbiter** | renders the winning cue on the one LED | [established by Kehai-Hikari](./kehai-hikari.md), generalized here |

No new libraries beyond what Kehai-Hikari already pulls in (`Adafruit_NeoPixel`).

## Behaviour — the CO₂→light mapping

Four bands on `co2_ppm`, named as consts. Reference points: outdoor air ≈ 420 ppm; cognitive
effects and drowsiness set in from ~1000–1400 ppm; 2000+ is unmistakably stuffy.

| Band | Condition (ppm) | Colour | Motion | Meaning |
|------|-----------------|--------|--------|---------|
| **Fresh** | `< CO2_FRESH` (800) | green | dim steady (tethered) · dark + heartbeat (battery) — [Kehai-Hikari D-1](./kehai-hikari.md#decisions), [KD-2](#decisions) | well ventilated |
| **Stuffy** | `800 – 1200` | amber | slow breathe (~3–4 s) | ventilation slipping |
| **Poor** | `1200 – 2000` | orange | slow breathe, brighter | open a window |
| **Bad** | `≥ CO2_POOR` (2000) | red | slow *strong* pulse (~1.5 s) | ventilate now — drowsy/headache range |
| *Warm-up* | no reading yet (`scdHasData == false`) | dim white/blue | slow breathe | SCD-40 warming (~5 s after boot) |

**Calm vocabulary — the deliberate contrast with Kehai.** Kehai's proximity reflex is *frantic*
(fast pulse, closer = faster) because it drives a reaction in ~200 ms. Kanki is the opposite: CO₂
moves over minutes, so the cue is **colour-dominant and slow**. Even "Bad" is a strong-but-slow
pulse, never a strobe. Two distinct *voices* on the same pixel — which is exactly what lets the
arbiter mix them intelligibly.

- **Hysteresis.** CO₂ hovers on band edges; apply ±~50 ppm hysteresis so the LED doesn't flip-flop
  at a boundary. (Kehai wants the same on its distance bands — a shared concern for the layer.)
- **Update cadence.** The SCD-40 refreshes ~every 5 s; Kanki recomputes its band on each fresh
  reading and posts it to the feedback layer, which animates the breathe smoothly between updates.
  Kanki needs **no** fast reflex tick of its own — another way it differs from Kehai.

## The shared feedback layer

**This is the pressure-test finding.** [Kehai-Hikari](./kehai-hikari.md) framed feedback as
`driveReflex(mm, alertNow)` — as if **Kehai owns the LED**. Kanki breaks that assumption: on a
full rig (ToF *and* SCD-40 both present) both modules want the single onboard pixel at the same
time. The seam must generalize from *"Kehai drives the LED"* to *"sources post cues; an arbiter
renders the winner on a shared surface."*

**Committed model — a small feedback arbiter** ([KD-1](#decisions); name **Aizu 合図, "the cue"** —
the shared on-body output bus; light now, DRV2605 haptic later, same bus):

- Each source **posts a request**, it does not write pixels directly:
  `postCue(source, class, colour, motion)`.
- Two **classes**:
  - **Ambient** — continuous, low priority, calm. Kanki's normal bands live here (the LED's
    "resting wallpaper").
  - **Alert** — transient/insistent, high priority, preempts ambient. Kehai's Reflex band lives here.
- The arbiter renders the **highest-priority active** cue; when no Alert is active it falls back to
  the Ambient cue; when nothing is posted it shows Idle ([Kehai-Hikari D-1](./kehai-hikari.md#decisions)).
- **Priority order (v1):** `Kehai Reflex` > `Kanki Bad` > `Kanki Poor/Stuffy/Fresh` > `Idle`.
  Rationale: a proximity reflex ("you're about to hit something") is more urgent than stale air, so
  it preempts — then **releases back** to Kanki's air colour when it clears. Kanki's own "Bad" is
  insistent but still *ambient-class* (slow), so it never masks a Kehai reflex.
- **Hysteresis + anti-flicker** live in the arbiter, so every source benefits.

**Consequence for the Kehai-Hikari spec (applied):** its `driveReflex()` seam
([firmware note 3](./kehai-hikari.md#firmware-integration)) is **restated as posting an Alert cue to
this arbiter**, not owning the pixel — a small amendment (sensing logic unchanged; only the sink
generalizes). Done; see [Cross-spec impact](#cross-spec-impact).

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. Builds on Kehai-Hikari's integration.

1. **Consume the cached SCD-40 state.** `scdCo2` / `scdHasData` are already maintained in `loop()`.
   Kanki reads them — no new sensor servicing.
2. **Post, don't paint.** On each fresh SCD reading (or warm-up transition), compute the band (with
   hysteresis) and `postCue(...)` an Ambient cue. Kanki never touches the NeoPixel directly.
3. **The arbiter owns the pixel.** The feedback layer (Aizu) is the *only* writer of the NeoPixel and
   the sole timer for breathe/pulse animation; Kehai and Kanki are both just posters.
4. **Battery / idle** follows [Kehai-Hikari D-1](./kehai-hikari.md#decisions): tethered (`Serial`) →
   Fresh shows dim green; field/battery (`!Serial`) → Fresh is **dark** (management-by-exception —
   the light means "act"), plus a **dim all-clear heartbeat** (one soft green blink ~every 30 s) so
   the wearer knows it's alive ([KD-2](#decisions)).
5. **Standalone-safe.** With SCD-40 absent (`scdPresent == false`), Kanki posts nothing and the
   arbiter falls through to whatever else is active (or Idle). Firmware still boots; other sensors
   unaffected.
6. **No telemetry disturbance.** No `Serial` writes on the telemetry stream, no change to the 1500 ms
   row cadence, BLE notify, or the untethered `!Serial` flash-logging gate.

## Contract impact

**None.** `co2_ppm` is already published in both the CSV schema and the Climate GATT characteristic
([CONTRACT.md](../../CONTRACT.md)). Kanki is output-only and adds no field. Same posture as Kehai-Hikari.

## Acceptance criteria

1. **Fresh air:** at < 800 ppm, the LED shows the Fresh state (dim green tethered; dark on battery
   with the ~30 s all-clear heartbeat, KD-2).
2. **Escalation:** breathing on the sensor (or a stuffy room) drives CO₂ up and the LED walks
   green → amber → orange → red across the band thresholds, with **no flip-flop** at boundaries
   (hysteresis holds).
3. **Calm:** every Kanki state uses slow motion; none strobes. "Bad" is a strong *slow* pulse.
4. **Warm-up:** for the first ~5 s (no SCD data) the LED shows the distinct warm-up cue, then
   resolves to the real band.
5. **Standalone:** criteria 1–4 hold on the minimal rig (host + SCD-40 only, all other sensors
   absent), with no BLE central connected.
6. **Shared LED:** on a full rig, a Kehai proximity Reflex **preempts** Kanki's colour, then the LED
   **returns** to the Kanki air colour when the object clears — neither module is starved.
7. **No regression:** CSV header/order/cadence, BLE notify, and untethered flash-logging unchanged.

## Decisions

All four opening questions are resolved; recorded here as the build contract.

- **KD-1 — Arbiter model:** committed. The Ambient/Alert two-class scheme with priority
  `Kehai Reflex > Kanki Bad > Kanki normal > Idle`, rendered by a shared arbiter named **Aizu (合図)**
  — the sole NeoPixel writer, later the fan-out point for the DRV2605 haptic. Applied to
  Kehai-Hikari (see [Cross-spec impact](#cross-spec-impact)).
- **KD-2 — All-clear heartbeat:** yes. On battery, Fresh air emits one soft dim-green blink
  ~every 30 s (interval tunable) so the wearer knows it's alive despite the dark resting state.
- **KD-3 — Comfort channel:** CO₂-only for v1. Temp/RH (and a future BME688) become their own
  air-quality remix, not part of Kanki v1.
- **KD-4 — Thresholds:** `CO2_FRESH = 800`, Stuffy→Poor at 1200, `CO2_POOR = 2000` (ppm). Match
  common indoor-air guidance; tune on-wrist.

## Cross-spec impact

- **Kehai-Hikari (applied)** — its seam (goal 5, firmware note 3, AC-8, D-1) now posts an Alert cue
  to the shared arbiter (Aizu) instead of owning the NeoPixel; pixel-power/idle/brightness/animation
  responsibilities move to the arbiter. Sensing logic unchanged.
- **Registry** — on build, Kanki earns its own row in the [Zōkyō table](../../REGISTRY.md#zōkyō)
  beside Rokkan, and the shared feedback arbiter (Aizu) wants a registry entry as a **shared host
  capability** — like the ground-station, shared across Zōkyō rather than owned by one.

## Forward path

- **Aizu as the real output bus:** once two modules share it, adding the DRV2605 haptic means the
  arbiter fans a winning cue out to *both* light and vibration. Every future remix (a NeoPixel
  compass, Nesshi's hot/cold cue) just posts to the same bus.
- **Comfort / VOC:** temp+RH (and a future BME688 gas sensor from the catalog) extend Kanki from a
  CO₂ light into a fuller air-quality sense — its own spec when it lands.
