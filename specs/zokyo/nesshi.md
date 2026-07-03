# Nesshi (熱視) — spec

*Heat-sight: point, hold, and read a surface's temperature as light — "is it safe to touch?"*

**Status:** built (2026-07-03) · **Zōkyō:** Nesshi (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) (no change) · **Shares:** [Aizu](../platform/aizu.md) (output *and* input) · **Date:** 2026-07-02

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Nesshi — "heat-sight" — turns the [MLX90640](../../REGISTRY.md#sensors) thermal camera into an
**on-demand point-and-read thermometer**. Aim the rig at a surface, **hold the BOOT button**, and
the temperature of whatever's at the centre of the frame is read out as a colour on the onboard
NeoPixel (and, if present, the number in the glasses): green when it's safe, escalating to red when
it's hot enough to burn. Release the button and it's idle again.

It answers a concrete everyday question — *is this safe to touch?* — for a stove, a pan, a phone
charger, a laptop vent, a car brake; and doubles as a **hot-object finder** (an overheating device,
a warm body in the dark) via the scene's hottest point.

Today the firmware ships four thermal summary numbers mid-CSV and does nothing with them on the
body. Nesshi is the first module to make the 768-px thermal camera **do something you can feel**,
on demand, with no screen required.

Like [Kanki](./kanki.md), Nesshi is **one interaction, one cue** — a candidate **new Zōkyō** beside
Rokkan, drawn from the shared [parts catalog](../../REGISTRY.md#parts-catalog). And like Kanki, its
real value in the spec series is what it *stresses*: **Kanki proved Aizu's output must be shared;
Nesshi proves Aizu's input must be too.** See [The shared button](#the-shared-button--the-pressure-test).

## Why (the thrifty case)

- **Zero new BOM.** MLX90640, BOOT button, and NeoPixel are all already on the rig; `thermal_ctr` /
  `thermal_max` / `hotspot_delta` are already computed and in the contract.
- **A real tool from an underused sensor.** The thermal cam is the priciest sensor in the bin and
  currently the least expressed. Nesshi turns it into a $50 IR-thermometer-gun equivalent.
- **Exercises Aizu's input side** — the BOOT button, so far only a deferred mute — before more
  modules pile on.

## Goals

1. On **button-hold**, read the centre-of-frame surface temperature and express it as a calm→alarm
   colour cue, fast enough to feel responsive.
2. Double as a **hot-object finder**: a modifier reads the scene's hottest point (`thermal_max` /
   `hotspot_delta`) instead of the centre.
3. Run on-host, glasses-optional (NeoPixel cue is the core; a numeric readout in the HUD is an
   enhancement).
4. **Share the BOOT button** with Aizu's mute cleanly — hold vs click, no collision.
5. **Post to Aizu** for output; add nothing to the contract.

## Non-goals

- **No contract change.** Reads existing `thermal_ctr` / `thermal_max` / `hotspot_delta`; no new CSV
  column, no new GATT characteristic. ([Contract impact](#contract-impact).)
- **Not a calibrated instrument.** IR surface temp depends on emissivity (the MLX90640 assumes
  ~0.95; shiny metal reads low) and it measures *surface*, not core. Nesshi signals **bands for
  safety**, not a certified reading. Clinical fever measurement is explicitly out of scope
  ([ND-2](#decisions)).
- **No thermal image.** Rendering the 768-px frame into the glasses is a different module
  ([Metsuke](#forward-path)); Nesshi is a single reduced number → a cue.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [MLX90640](../../REGISTRY.md#sensors) | thermal input (`thermal_ctr` spot; `thermal_max`/`hotspot_delta` scene) | already read in `loop()` |
| **BOOT button (GPIO0)** | hold-to-measure trigger | via [Aizu's input layer](../platform/aizu.md#input--the-boot-button) |
| **Onboard NeoPixel** | the hot/cold cue | via [Aizu](../platform/aizu.md) |
| [RayNeo / phone](../../REGISTRY.md#output--feedback) *(optional)* | numeric readout | [Shikai](../../REGISTRY.md#shikai-視界--field-of-view) |

## Behaviour — the read and the cue

While the BOOT button is **held**, Nesshi posts a cue for the current reading; on **release**, it
clears its cue (back to Aizu idle / whatever's underneath).

**Which pixel.**
- **Spot (default):** `thermal_ctr` — the centre of the FOV, i.e. what you're pointing at. This is
  the "is it safe to touch?" read.
- **Scene (modifier):** `thermal_max` + `hotspot_delta` — the hottest point anywhere in view, for
  finding an overheating object or a warm body. Selected by a **double-hold** ([ND-3](#decisions)).

**Temperature → colour** (bands keyed on the read °C; the burn-risk edges are the point):

| Band | °C | Colour | Meaning |
|------|-----|--------|---------|
| Cold | `< 0` | blue | freezing |
| Cool | `0 – 40` | green | **safe to touch** |
| Warm | `40 – 50` | amber | uncomfortably hot — caution |
| Hot | `50 – 60` | orange | burn risk on contact |
| Danger | `> 60` | red | **do not touch** |

Skin pain begins ~45 °C and burns rise sharply past 50 °C, so the amber→red edges sit where the
safety decision actually flips. Motion is steady/colour-dominant (you're reading a value, not being
alarmed) — closer to Kanki's calm voice than Kehai's frantic one.

- **Responsiveness.** The MLX90640 runs at 2 Hz in firmware (`setRefreshRate(MLX90640_2_HZ)`), so a
  fresh centre temp lands ~every 0.5 s — acceptable for point-and-read. Raising the refresh rate
  while a read is active would tighten it at some noise cost — not in v1 ([ND-4](#decisions)).
- **Glasses (optional).** When Shikai is present, show the numeric °C big; the NeoPixel band is the
  no-screen fallback and always runs.

## The shared button — the pressure-test

[Aizu](../platform/aizu.md#input--the-boot-button) owns GPIO0 and, per **AZ-3**, treats a **single press as a
mute toggle**. Nesshi needs the *same button* as a **hold-to-measure** trigger. That's the input-side
analogue of the collision Kanki found on the output side: **one physical button, two claimants.**

**Proposed resolution — Aizu's input becomes a small gesture layer** (the input twin of its cue
bus): Aizu debounces GPIO0 and emits **gesture events** — `CLICK` (short press-release) and
`HOLD` (press-and-hold, with `HOLD_START` / `HOLD_END`) — that modules subscribe to, rather than one
module owning the raw pin:

- **`CLICK` → mute toggle** (Aizu's own function, unchanged intent from AZ-3).
- **`HOLD` → Nesshi measure** (post the temp cue while held; clear on `HOLD_END`).

Click and hold are cleanly separable by duration (e.g. hold ≥ 400 ms), so the two coexist on one
button with no mode switch. This is a small **amendment to Aizu** — its input section grows from
"single press = mute" to "a gesture layer routing CLICK/HOLD to subscribers." Sensing logic
elsewhere is untouched. Flagged in [Cross-spec impact](#cross-spec-impact).

## Aizu output — a new cue source + priority rung

Nesshi posts an **interactive, high-priority** cue while the button is held: the user is *actively
asking* for this reading, so it should dominate the LED — but a safety reflex still outranks it.
Recommended rung in Aizu's [priority ladder](../platform/aizu.md#arbitration):

```
Kehai Reflex  >  Nesshi (while held)  >  Kanki Bad  >  Kehai Approach  >  Kanki Poor/Stuffy  >  idle
```

So holding the button to read a hot pan overrides Kanki's air colour (you're measuring), but if
something's about to hit you, the collision reflex still wins. On release, the LED falls back to
whatever was underneath. This is the second small Aizu amendment ([Cross-spec impact](#cross-spec-impact)).

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. Builds on Aizu + the existing thermal read.

1. **Consume cached thermal state.** `thermalCtr` / `thermalMax` / `hotspotDelta` are already
   computed each loop. Nesshi reads them — no new sensor servicing.
2. **Subscribe to the gesture layer.** On `HOLD_START`, begin posting the temp cue; refresh it as
   new thermal frames land; on `HOLD_END`, `clearCue`. Nesshi never touches the NeoPixel or GPIO0
   directly — both go through Aizu.
3. **Band with hysteresis.** Apply a small °C hysteresis on the band edges so a reading hovering at
   a boundary doesn't flip colour (as Kanki does on ppm).
4. **No telemetry disturbance.** No serial writes, no change to the 1500 ms cadence, BLE notify, or
   the untethered flash-logging path.
5. **Degrades.** With the MLX90640 absent (`thermalPresent == false`), a hold posts nothing (or a
   distinct "no sensor" cue); the board still boots and logs.

## Contract impact

**None.** `thermal_ctr` / `thermal_max` / `hotspot_delta` are already in the CSV schema and the
Thermal GATT characteristic ([CONTRACT.md](../../CONTRACT.md)). Nesshi is output-only and adds no
field. Same posture as Kehai-Hikari and Kanki.

## Acceptance criteria

1. **Hold to read:** holding BOOT while aimed at a surface shows a colour matching its temperature
   band within ~1 s; releasing clears it.
2. **Safety edges:** a cool surface reads green; something ~45–50 °C reads amber; a hot surface
   (>60 °C) reads red — with no flip-flop at the edges (hysteresis).
3. **Scene modifier:** the chosen modifier reads the scene's hottest point instead of the centre.
4. **Button coexistence:** a **short click** still toggles mute; a **hold** measures — the two never
   trigger each other.
5. **Arbitration:** while held, Nesshi's cue overrides Kanki's ambient; a Kehai Reflex still
   preempts Nesshi; on release the LED returns to what was underneath.
6. **Glasses-optional:** criteria 1–2 hold with no BLE central connected.
7. **No regression:** CSV schema/cadence, BLE notify, and flash logging unchanged.

## Decisions

All five opening questions are resolved; recorded here as the build contract.

- **ND-1 — Gesture split (committed).** Aizu's BOOT-button input becomes a gesture layer: `CLICK`
  (short) → mute; `HOLD` (press-and-hold, ~400 ms threshold; `HOLD_START`/`HOLD_END`) → Nesshi
  measure. Drives the two Aizu amendments (AZ-9 input, AZ-10 rung) in
  [Cross-spec impact](#cross-spec-impact).
- **ND-2 — Fever deferred.** v1 = surface-safety + hot-object only. A caveated forehead/fever read is
  a later add (surface temp is a poor core-temp proxy).
- **ND-3 — Scene via double-hold.** Spot (`thermal_ctr`) is the default read; a **double-hold**
  selects scene (`thermal_max`/`hotspot_delta`).
- **ND-4 — Keep 2 Hz.** No MLX90640 refresh-rate bump during a read in v1; revisit only if
  point-and-read feels laggy.
- **ND-5 — Bands committed.** `< 0` blue · `0–40` green (safe) · `40–50` amber · `50–60` orange ·
  `> 60` red, with edge hysteresis. The colour flip sits at the real burn-safety line.

## Cross-spec impact

- **Aizu — input (amendment):** its BOOT-button handling grows from "single press = mute" (AZ-3)
  into a small **gesture layer** emitting `CLICK` / `HOLD` events to subscribers; `CLICK` routes to
  mute, `HOLD` to Nesshi. Sensing logic unchanged. (Committed [ND-1](#decisions); applied to [aizu.md](../platform/aizu.md#input--the-boot-button) as AZ-9.)
- **Aizu — output (amendment):** add a **Nesshi (while held)** rung to the priority ladder, just
  below Kehai Reflex and above Kanki Bad. (Committed [ND-1](#decisions); applied to [aizu.md](../platform/aizu.md#arbitration) as AZ-10.)
- **Registry (build-time):** Nesshi earns its own row in the [Zōkyō table](../../REGISTRY.md#zōkyō)
  beside Rokkan and Kanki.

## Forward path

- **Metsuke (目付) — thermal vision in the glasses:** the full 768-px frame downsampled into a HUD
  overlay — the richer sibling to Nesshi's single-number read; its own spec.
- **Emissivity presets:** a gesture to pick skin / metal / matte emissivity for more honest reads.
- **Latching read:** a click-while-holding to freeze the last value on the LED for hands-free
  comparison.
