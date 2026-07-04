# Kyūkaku (嗅覚) — spec

*A wearable sense of smell: the moment the air changes, told as light.*

**Status:** built (2026-07-04) · **Zōkyō:** Kyūkaku (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) (no change) · **Shares:** [Aizu](../platform/aizu.md) (output) · **Date:** 2026-07-03

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Kyūkaku — "olfaction" — is an **electronic sense of smell** you wear. It reads the
[BME688](../../REGISTRY.md#sensors) gas-sensor resistance already published as `gas_ohms` and
expresses it on the onboard NeoPixel via [Aizu](../platform/aizu.md): quiet when the air is as it
was, and a **sudden violet startle** the instant something *new* enters it — solvent, smoke, a gas
leak, off-gassing electronics, spoiling food, a chemical you can't consciously place. No screen, no
phone — a chemical early-warning on your body.

This is the sense [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense) is named for and never had.
Rokkan means *"sixth sense"*, yet the one classical sense the platform still can't *feel* is smell.
The BME688 is literally a nose — a heated metal-oxide plate whose resistance collapses when reducing
gases hit it. [Kanki](./kanki.md) already turned the SCD-40's CO₂ into an all-day light; the BME688's
gas field has ridden mid-CSV since Tanchi and done nothing on the body. Kyūkaku makes it a sense.

Like [Kanki](./kanki.md) and [Nesshi](./nesshi.md), Kyūkaku is **one sense, one cue** — a candidate
**new Zōkyō** beside Rokkan, drawn from the shared [parts catalog](../../REGISTRY.md#parts-catalog).
And like them, its real value in the spec series is what it *stresses*: **Kanki proved Aizu's output
must be shared; Nesshi proved its input must be too; Kyūkaku is the first module to put a *second
sense in the same category* on the one pixel** — the first cue Aizu must keep legible against a rival,
not just prioritise. See [Two air-senses on one pixel](#two-air-senses-on-one-pixel--the-pressure-test).

This spec is the one [Kanki's Forward path](./kanki.md#forward-path) reserved — *"a future BME688 gas
sensor … extend into a fuller air-quality sense — its own spec when it lands."* It's landed.

## Why (the thrifty case)

- **Zero new BOM.** The BME688 is already on the chain; `gas_ohms` is already in the contract; the
  NeoPixel is on the board and [Aizu](../platform/aizu.md) already owns it. Kyūkaku is pure firmware
  over parts you own.
- **A real device from a dead field.** `gas_ohms` is the least-expressed number in the CSV — a raw
  resistance nobody reads. Kyūkaku turns it into a canary you'd actually clip to a bag.
- **No calibration, ever.** The trick is to watch the *change*, not the value ([KY-1](#decisions)):
  a MOX resistance drifts with humidity, heat, and age and is meaningless in absolute terms, but a
  **fast drop relative to its own recent baseline** is unambiguous — *the air just changed.* That
  dodges burn-in calibration entirely and is the whole reason this is cheap.
- **Runs on almost nothing.** Host + one I²C sensor, the Kanki-minimal rig with a different part.
- **Exercises the seam.** It's the first *rival* ambient source on Aizu — building it now proves
  whether colour-identity, not just priority, keeps two air-senses apart on one pixel.

## Goals

1. Detect the **onset** of a new smell — a fast fall in `gas_ohms` relative to an adaptive clean-air
   baseline — and fire a transient, insistent **Spike** cue that startles the wearer.
2. Express **sustained** poor air (a resistance held low) as a calm ambient **Foul** cue, the way
   Kanki expresses stale air — so the sense reads both the event and the state.
3. Do it **calibration-free**: an adaptive baseline, ratio bands, no per-sensor tuning ([KY-1](#decisions)).
4. Run standalone on the minimal rig (host + BME688), every other sensor absent.
5. **Post to [Aizu](../platform/aizu.md)** for output with a **distinct colour identity** (violet),
   so it stays legible beside Kanki's green→red air ramp on the shared pixel; add nothing to the
   contract.

## Non-goals

- **No contract change.** Reads the existing `gas_ohms` (and `humidity_pct` for confound rejection);
  no new CSV column, no new GATT characteristic. ([Contract impact](#contract-impact).)
- **No smell *identification*.** Kyūkaku says *"the air changed / the air is foul"*, never *"that's
  toluene"* or *"that's coffee"*. Naming a scent is a trained classifier over the BME688 gas-scan —
  a beefier, contract-changing build ([Kaori](#forward-path)), explicitly out of scope here.
- **Not a safety-rated gas detector.** The BME688 has no absolute ppm and no CO/CO₂ selectivity; it
  registers *reducing VOCs broadly*, not the specific poisons a certified alarm targets. Kyūkaku is a
  *heads-up*, not a substitute for a CO or combustible-gas alarm ([KY-2](#decisions)).
- **Not a calibrated instrument.** It signals *change* and *bands*, not a certified reading — the raw
  resistance is already in the CSV/BLE stream for anyone who wants it.
- **No new animation engine.** Reuses [Aizu](../platform/aizu.md)'s motion vocabulary; Kyūkaku only
  posts intent.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [BME688](../../REGISTRY.md#sensors) | gas input (`gas_ohms`; `bmeGas`, refreshed each `loop()` as the heater fires) | already read in `loop()` |
| [BME688](../../REGISTRY.md#sensors) humidity | confound signal (`bmeHum`) — reject a drop that is really a humidity jump ([KY-4](#decisions)) | same reading |
| **[Aizu](../platform/aizu.md)** | the shared feedback bus — renders the winning cue on the one NeoPixel | built (2026-07-02) |

No new libraries: `Adafruit_BME680` is already `#include`d and servicing the sensor; `Adafruit_NeoPixel`
lives behind Aizu.

## Behaviour — reading the change, not the value

The BME688 gas plate reads **high resistance in clean air** and **drops as reducing VOCs rise**
(`gas_ohms`: lower = more VOC). Absolute ohms are drift-ridden and uncalibrated, so Kyūkaku never
uses them directly. Instead:

**The adaptive baseline `R₀`.** A slow estimate of *clean-air* resistance — the ceiling the signal
returns to when nothing's around. Track it with an **asymmetric** update: rise toward higher readings
reasonably quickly (clean air re-asserts the baseline) but decay downward very slowly, so a genuine
smell can't drag the baseline down and hide itself. `R₀` is seeded during settling (below).

**The ratio `r = gas_ohms / R₀`.** ≈ 1.0 in clean air; falls toward 0 as VOC load rises. Everything
Kyūkaku expresses is a function of `r` and its **rate of change** — both dimensionless, both
calibration-free.

Two behaviours, two Aizu classes:

| State | Condition | Aizu class | Colour · motion | Meaning |
|-------|-----------|-----------|-----------------|---------|
| **Spike** | `r` falls by ≥ `GAS_SPIKE_DROP` (0.25) within `GAS_SPIKE_WINDOW` (~6 s), *and* not humidity-vetoed | **ALERT** (transient) | violet→red, fast pulse ~3 s then decays | *something just entered the air* |
| **Foul** | `r < GAS_FOUL_R` (0.35), sustained | **AMBIENT** | violet, slow strong breathe | air is chemically loaded — persistent |
| **Taint** | `GAS_FOUL_R ≤ r < GAS_TAINT_R` (0.60) | **AMBIENT** | dim violet, slow breathe | a smell is present but mild |
| **Clean** | `r ≥ GAS_CLEAN_R` (0.85) | *(quiescent)* | *(no cue)* | air is as it was — falls through to Aizu Idle |
| *Settling* | baseline not yet seeded (first `GAS_SETTLE_S` ≈ 120 s) | *(no cue)* | *(none — Aizu Idle shows "alive")* | heater warming, learning clean air |

- **Spike is the point.** A *sudden* drop is the nose startling — the transient ALERT that preempts
  the ambient wallpaper for a few seconds, then releases. It answers *"what just happened?"*, where
  Foul answers *"what is the air like?"*. The Spike is what makes Kyūkaku feel like a sense rather
  than a gauge ([KY-2](#decisions)).
- **Violet is the identity, not the severity.** Kanki owns the green→amber→orange→red air ramp;
  Kyūkaku owns **violet**, escalating to red only at a Spike's peak. On one shared pixel the wearer
  must tell *"stuffy"* (Kanki, warm ramp) from *"chemical"* (Kyūkaku, violet) at a glance — so hue is
  a **source identity**, not a global badness scale ([KY-3](#decisions), [AZ-12](../platform/aizu.md#decisions)).
- **The humidity confound.** A MOX resistance also drops when humidity jumps (walk into a bathroom,
  breathe on it, boil a kettle) — indistinguishable from VOC *by resistance alone*. But the BME688
  hands us `bmeHum` in the **same reading**: if a candidate Spike coincides with a matching humidity
  step, Kyūkaku **vetoes / attenuates** it ([KY-4](#decisions)). The same sensor supplies its own
  confound signal — free. Full RH/temperature compensation is BSEC's job and belongs to
  [Kaori](#forward-path); v1 uses the simple veto.
- **Settling / burn-in.** A cold plate reads garbage until it heats and the baseline is seeded.
  For the first `GAS_SETTLE_S` (~120 s) Kyūkaku posts **nothing** — Aizu's Idle already signals
  "alive" — then arms once `R₀` is established over recent clean air ([KY-5](#decisions)).
- **Hysteresis.** Ratio bands hover on edges like Kanki's ppm bands; apply per-band hysteresis so the
  ambient states don't flip-flop. (Aizu's arbitration debounce is separate — it only smooths which
  *winner* renders, per [AZ-1](../platform/aizu.md#arbitration).)
- **Update cadence.** `bmeGas` refreshes each `loop()` as the gas heater fires (~150 ms, at the
  telemetry cadence). Kyūkaku recomputes `R₀`, `r`, and the rate on each fresh reading and posts to
  Aizu, which owns the animation clock. No fast tick of its own.

## Two air-senses on one pixel — the pressure test

**This is what Kyūkaku stresses.** [Aizu](../platform/aizu.md) already proved it can *arbitrate* many
sources by a single priority scalar (AZ-1). But every source before Kyūkaku spoke about a *different*
thing — proximity, a fall, a held thermal read, stale CO₂. Kyūkaku is the first source that is a
**near-twin of an existing one**: on a full rig, **Kanki (CO₂) and Kyūkaku (VOC) are both ambient
air-senses** that can be quietly bad at the same time. Priority tells Aizu which to *render*; it says
nothing about whether the wearer can *tell them apart*.

The finding: **flat priority is sufficient for arbitration, but colour must be a source-owned
identity, not a global severity convention.** If every "bad air" cue trended red, the wearer couldn't
distinguish *"open a window"* (Kanki) from *"something's burning"* (Kyūkaku) on the one pixel. Aizu
already lets each source name its own colour — so **no arbiter change is needed** — but Kyūkaku is the
case that *forces the convention to be explicit*: Kanki keeps the warm green→red ramp; Kyūkaku takes
violet. Two rival ambient senses, legible by hue, arbitrated by priority.

**Where the two rungs land** (committed to Aizu as [AZ-12](../platform/aizu.md#decisions)):

- **Kyūkaku Spike** — an **ALERT** in the safety tier, just **below Hokan Fall SOS** and above the
  interactive/ambient cues. A sudden chemical onset (smoke, gas, solvent) is a co-critical safety
  event, but unlike a fall it **does not latch** — it decays as the air clears. It preempts, then
  releases to whatever was underneath.
- **Kyūkaku Foul / Taint** — **AMBIENT**, ranked in the graduated-warning tier beside Kanki's air
  bands (Foul near Kanki Poor; Taint near Kanki Stuffy). A chemical smell and stale air are peers as
  *degrading ambient air*; colour, not rank, tells them apart.

So the v1 ladder gains: `… Fall SOS > `**`Kyūkaku Spike`**` > Nesshi(held) > Kanki-Bad > Kehai-Approach
> Kanki-Poor ≈ `**`Kyūkaku-Foul`**` > Kanki-Stuffy ≈ `**`Kyūkaku-Taint`**` > Idle`. A Spike outranks
everything but a live collision Reflex or a just-happened fall; sustained chemical air sits with
sustained stale air.

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. Builds on the Aizu + Kanki integration.

1. **Consume the cached BME688 state.** `bmeGas` / `bmeHum` / `bmeHasData` / `bmePresent` are already
   maintained in `loop()` (the heater fires each reading). Kyūkaku reads them — no new sensor servicing.
2. **Track the baseline + ratio.** On each fresh BME reading, update `R₀` (asymmetric EMA), compute
   `r` and its short-window rate, apply the humidity veto, resolve the state (with hysteresis).
3. **Post, don't paint.** `postCue(KYUKAKU, …)` a violet **ALERT** cue for a Spike (short `maxAgeMs`
   so it self-expires as the event passes) or an **AMBIENT** cue for Foul/Taint; `clearCue(KYUKAKU)`
   when Clean or settling. Kyūkaku never touches the NeoPixel — Aizu is the sole writer.
4. **Battery / idle** is Aizu's, inherited: Clean posts nothing, so the sense falls through to Aizu's
   green-breathe (tethered) / dark-plus-heartbeat (battery) Idle — the light means *"the air changed,
   act"* ([AZ-2](../platform/aizu.md#decisions)).
5. **Standalone-safe.** With the BME688 absent (`bmePresent == false`), Kyūkaku posts nothing and
   Aizu falls through to whatever else is active (or Idle). Firmware still boots; other sensors
   unaffected.
6. **No telemetry disturbance.** No `Serial` writes on the telemetry stream, no change to the 1500 ms
   row cadence, BLE notify, or the untethered `!Serial` flash-logging gate.

## Contract impact

**None.** `gas_ohms` and `humidity_pct` are already published in the CSV schema and the Environment
GATT characteristic (`abcdc0de-…`, [CONTRACT.md](../../CONTRACT.md)). Kyūkaku is output-only and adds
no field. Same posture as Kanki, Nesshi, and Aizu.

## Acceptance criteria

1. **Settling:** for the first ~120 s the LED shows Aizu Idle (no Kyūkaku cue) while the baseline
   seeds; Kyūkaku then arms without a false Spike from warm-up drift.
2. **Spike onset:** introducing a fresh VOC near the sensor (a marker cap, a squeeze of citrus peel,
   a whiff of alcohol) fires the violet→red fast-pulse Spike within a few seconds; it **decays** back
   to the underlying state as the air clears — no latch.
3. **Humidity veto:** a bare humidity jump with no VOC (warm moist breath, steam) does **not** fire a
   Spike ([KY-4](#decisions)); a real VOC with a matching humidity component still does.
4. **Sustained Foul:** holding the sensor in genuinely loaded air (an open solvent, a smelly bin)
   settles to the violet slow-breathe Foul ambient after the Spike passes.
5. **Clean returns:** in restored clean air, `r` recovers toward baseline and Kyūkaku clears its cue;
   the LED falls to Aizu Idle.
6. **Colour identity:** Kyūkaku states are violet-family; on a full rig they are visually
   distinguishable from Kanki's green→red air ramp on the shared pixel ([KY-3](#decisions)).
7. **Shared LED:** a Kyūkaku Spike **preempts** an active Kanki ambient (per [AZ-12](../platform/aizu.md#decisions)),
   then **releases** back to the Kanki air colour when it decays; a live Kehai Reflex or Hokan Fall
   SOS still outranks a Spike.
8. **Standalone:** criteria 1–6 hold on the minimal rig (host + BME688 only, all other sensors
   absent), with no BLE central connected.
9. **No regression:** CSV header/order/cadence, BLE notify, and untethered flash-logging unchanged;
   Aizu remains the sole NeoPixel writer.

## Decisions

- **KY-1 — Relative, not absolute (baseline-ratio detection).** Committed. Kyūkaku watches `r =
  gas_ohms / R₀` and its rate against an adaptive clean-air baseline, never raw ohms — no calibration,
  drift-robust. This is the whole thrifty case.
- **KY-2 — Two behaviours: Spike + Foul.** A transient ALERT for *onset* (the nose startling) and a
  calm AMBIENT for *state* (loaded air). The Spike is the identity of the sense; Foul is the Kanki-
  style wallpaper. Kyūkaku is a heads-up, **not** a safety-rated gas alarm (no absolute ppm, no
  CO/combustible selectivity).
- **KY-3 — Violet colour identity.** Kyūkaku owns violet/magenta (→ red only at a Spike peak),
  distinct from Kanki's green→red air ramp, so two air-senses stay legible on one pixel. Hue is a
  source identity, not a severity scale — the convention this spec forces Aizu to make explicit
  ([AZ-12](../platform/aizu.md#decisions)).
- **KY-4 — Humidity confound veto (v1).** A candidate Spike coinciding with a matching `bmeHum` jump
  is vetoed/attenuated, using the same-reading humidity the BME688 already provides. Full RH/temp
  compensation is BSEC's job → [Kaori](#forward-path).
- **KY-5 — Settle quietly.** During the ~120 s burn-in / baseline seed, Kyūkaku posts nothing; Aizu
  Idle already signals "alive". No dedicated warm-up cue (leaner than Kanki's, and the resting green
  already reads as "all clear").
- **KY-6 — Thresholds (ratio-space, tune on-wrist):** `GAS_CLEAN_R = 0.85`, `GAS_TAINT_R = 0.60`,
  `GAS_FOUL_R = 0.35`; Spike = drop of ≥ `GAS_SPIKE_DROP (0.25)` within `GAS_SPIKE_WINDOW (~6 s)`;
  `GAS_SETTLE_S ≈ 120`. Baseline: asymmetric EMA, rise fast / decay slow. All dimensionless — no
  per-sensor calibration.
- **KY-7 — Aizu ranking.** Spike joins the safety tier below Fall SOS, **non-latching**; Foul/Taint
  sit with Kanki's ambient air bands. Recorded in Aizu as [AZ-12](../platform/aizu.md#decisions).

## Cross-spec impact

- **Aizu (on build)** — add **AZ-12**: two new source rungs (**Kyūkaku Spike**, ALERT non-latching in
  the safety tier below Fall SOS; **Kyūkaku Foul/Taint**, AMBIENT beside Kanki's air bands) and the
  **colour-identity confirmation** — two rival ambient air-senses are kept legible by source-owned hue
  (violet vs Kanki's green→red), which Aizu already supports; no arbiter/rendering change, just table
  rows. Kyūkaku is the first *same-category rival* source and the case that makes the hue convention
  explicit.
- **Kanki** — no code change; its [Forward-path](./kanki.md#forward-path) VOC note and **KD-3** ("temp/RH
  and a future BME688 become their own air-quality remix") are **fulfilled** by Kyūkaku (the VOC half).
  On a full rig Kanki gains a violet sibling on the pixel — the pressure-test above.
- **Registry** — on build, Kyūkaku earns its own row in the [Zōkyō table](../../REGISTRY.md#zōkyō)
  beside Kanki, framed as *completing Rokkan's "sixth sense" — the literal sense of smell*. The BME688
  is already in the [parts catalog](../../REGISTRY.md#parts-catalog); note in Tanchi that its gas field
  is now expressed on-body.

## Forward path

- **Kaori (香り) — scent *identification*.** The beefier build: bring in the Bosch **BSEC** library and
  the BME688 gas-scan and the nose stops sensing *change* and starts naming *signatures* — trained to
  recognise a specific room, substance, or person's chemical fingerprint. That's the first BME688
  module to **touch the contract** (a new `scent_class` field / characteristic), likely wanting the
  **N4R2 PSRAM** host from the catalog — the BME688's *Metsuke moment*. Kyūkaku ships first on the raw
  needle; Kaori earns the contract change later.
- **Kiatsu (気圧) — the barometer sibling.** The BME688's *other* unused field, `pressure_hpa`: a
  vertical dead-reckoner (which floor, GPS-denied — feeding [Hokan](./hokan.md)'s Z-axis) and a
  storm-turn precog (a falling barometer, hours out). Another zero-BOM sense from the same part.
- **Haptic sink:** once Aizu's DRV2605 sink lands, a Spike drives light *and* vibration — a chemical
  onset you truly feel, the closest thing yet to a real reflexive nose.
- **HUD echo:** mirror the VOC state into the [Shikai](../../REGISTRY.md#shikai-視界--field-of-view)
  glasses via a future Aizu cue-mirror characteristic — a deliberate contract addition, specced
  separately.
