# Kaori (香り) — spec

*Scent-memory: not "the air changed" but "that's the paint aisle" — the nose that names what it smells.*

**Status:** spec (2026-07-03) · not yet built · **Zōkyō:** Kaori (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) — **changes it (both halves)** · **Shares:** [Kyūkaku](./kyukaku.md) (the reflex that wakes it), [Shikai](../../REGISTRY.md#shikai-視界--field-of-view) (live label), the [ground-station](../../REGISTRY.md#parts-catalog) (scent timeline) · **Needs:** Bosch **BSEC2** + an offline training pass · **Date:** 2026-07-03

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Kaori — "scent / fragrance" — is the BME688's **cognition** layer: it doesn't just notice the air
change, it **names** it. Where [Kyūkaku](./kyukaku.md) is a calibration-free *reflex* ("something
just entered the air"), Kaori runs the BME688 in its **gas-scanner** mode through Bosch's **BSEC2**
classifier and answers *what* — a trained class label with a confidence: `solvent`, `coffee`,
`smoke`, `the paint aisle`, `nothing`. Scent-memory in hardware — the body learns a substance, a
room, or a person's chemical fingerprint and recognises it again.

It is the BME688's **Metsuke moment.** [Metsuke](./metsuke.md) was the first module to touch the
data contract (a new BLE characteristic for the heat grid); Kaori is the first to touch it *for the
BME688*, and the first to touch **both halves at once** — a CSV `scent_class` timeline *and* a live
Scent BLE characteristic. See [Three firsts](#three-firsts).

And it is, deliberately, **the first build in the series that isn't thrifty.** Every module before it
was pure firmware over parts you own; Kaori needs a **closed-source library (BSEC2)**, an **offline
training workflow** (Bosch **BME AI-Studio** + labelled samples), and it wants the **N4R2 PSRAM
host** for headroom. That cost is the point of writing it last: it's the flex you *earn* once the
cheap senses are proven, and this spec is honest about the bill — see [The honest cost](#the-honest-cost).

## Why (and what it costs)

**Why:**
- **The BME688's whole reason to exist.** The `688` (over the `680`) *is* the on-chip gas-scanner + AI
  gas model. Kyūkaku ships on the raw needle; Kaori is the sensor used as designed — the one capability
  no other part in the bin has.
- **Naming beats noticing.** "That's solvent" / "that's your target's cologne" is a categorically
  different tool from "the air changed" — it's recognition, not just alarm. Peak rogue: a nose that
  remembers.
- **Cheap to *express*, once trained.** The output is a tiny label + confidence — trivial to log and to
  show in the HUD (unlike Metsuke's heat grid, which was too heavy for the CSV).

**What it costs (stated up front, [The honest cost](#the-honest-cost)):**
- **BSEC2** — a precompiled, Bosch-licensed blob (not open source); redistribution terms apply ([KrD-6](#decisions)).
- **An offline training pass** — collect labelled scent data on a dev kit, build a classifier in BME
  AI-Studio, flash the `.config`. Kaori is only as good as its training set ([KrD-2](#decisions)).
- **Board headroom** — BSEC2 state + the classifier config + everything else already resident wants
  the **N4R2 (2 MB PSRAM)** host; the plain QT Py may run it but with no slack ([KrD-5](#decisions)).

## Goals

1. Drive the BME688 in **gas-scanner mode** (multi-step heater profile) and run **BSEC2** to produce a
   **classified scent** — a class label + confidence — for a small set of trained classes.
2. Publish it on **both contract halves**: a logged CSV `scent_class` (+ `scent_conf`) for the
   ground-station's **scent timeline**, and a live **Scent BLE characteristic** for the HUD label.
3. **Gate the cognition on the reflex:** run the expensive scan/classify duty-cycle up when
   [Kyūkaku](./kyukaku.md) sees a change, idle it when the air is boring ([KrD-3](#decisions)).
4. Keep `gas_ohms` (and Kyūkaku, and the Environment characteristic) **alive** while Kaori owns the
   heater profile — derive the legacy single-shot resistance from a reference scan step ([KrD-4](#decisions)).
5. Change the contract **once, cleanly, in both halves**, respecting the three-mirror discipline.

## Non-goals

- **Not open-ended smell-to-text.** Kaori recognises the **classes it was trained on**; an untrained
  scent reads as `unknown` / low confidence, never an invented label. It is a closed-set classifier,
  not a general "what is this smell" oracle ([KrD-2](#decisions)).
- **Not a safety-rated detector.** Same limits as Kyūkaku — broad reducing-VOC response, no absolute
  ppm, no CO/combustible selectivity. A trained `smoke`/`gas` class is a *heads-up*, never a
  substitute for a certified alarm.
- **No on-device training (v1).** Training is the **offline** AI-Studio workflow; the firmware ships a
  frozen classifier. Field / on-wrist retraining is [Forward path](#forward-path) ([KrD-2](#decisions)).
- **No Aizu cue (v1).** Like [Metsuke](./metsuke.md), Kaori's v1 output is the contract → consumers
  (HUD + ground-station), not the NeoPixel. A **trained alert-scent** posting an Aizu cue is a clean
  extension, deferred ([KrD-7](#decisions), [Forward path](#forward-path)).
- **Not a replacement for Kyūkaku.** They **layer**: Kyūkaku the reflex (cheap, always-on, ships
  first), Kaori the cognition (heavy, gated). Kaori does not remove Kyūkaku's cue.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [BME688](../../REGISTRY.md#sensors) | gas-scanner mode (multi-step heater) → resistance vector | re-driven by Kaori (was single-shot) |
| **BSEC2 library** | Bosch classifier: resistance vectors → class probabilities | *new dependency* (closed-source blob) |
| **BME AI-Studio** + a dev kit | offline: label samples, build the `.config` classifier | host-side (PC), pre-build |
| [QT Py ESP32-S3 **N4R2**](../../REGISTRY.md#host--infrastructure) | PSRAM headroom for BSEC2 state + config (recommended) | catalog upgrade |
| [Kyūkaku](./kyukaku.md) | the reflex that duty-cycles Kaori's scanning | firmware |
| [Shikai](../../REGISTRY.md#shikai-視界--field-of-view) + **ground-station** | live scent label / logged scent timeline | `android/` · `groundstation/` |

## Behaviour — scan, classify, name

1. **Gas-scanner heater profile.** BSEC2 classification needs the plate cycled through a **multi-step
   heater profile** (a sweep of temperatures), not the single 320 °C/150 ms shot the firmware fires
   today — different VOCs shift resistance differently at different plate temps, and that *vector* is
   what makes them separable. Kaori takes over BME688 servicing and runs the scan; a full scan spans a
   few seconds ([KrD-1](#decisions)).
2. **BSEC2 → probabilities.** Feed each scan's resistance vector (with the on-chip T/RH compensation
   BSEC2 does natively — the calibrated version of [Kyūkaku's humidity veto](./kyukaku.md#decisions))
   to the trained classifier. Out comes a probability per trained class.
3. **Name it.** Take the top class; if its confidence clears `SCENT_CONF_MIN` publish `<label>
   <confidence>`, else publish `unknown` (or `none`). Scents don't move fast, so a new label every few
   seconds is plenty; between scans the last label holds.
4. **The reflex gates the cognition ([KrD-3](#decisions)).** A full gas-scan is expensive in time and
   power, so Kaori doesn't run it flat-out: it idles at a slow background cadence and **spins up when
   [Kyūkaku](./kyukaku.md) fires a Spike** — the cheap change-detector wakes the expensive classifier.
   *Something changed → find out what.* The two senses compose into one layered nose.
5. **Keep the legacy field alive ([KrD-4](#decisions)).** While Kaori owns the heater, it still emits a
   single-shot-equivalent `gas_ohms` from a fixed **reference step** of the scan, so `gas_ohms`, the
   Environment characteristic, and Kyūkaku keep working unchanged.

**Outputs.**
- **Live:** the current `scent_class` + confidence on the Scent BLE characteristic → a HUD label
  ("nose: solvent 0.82") in [Shikai](../../REGISTRY.md#shikai-視界--field-of-view) / the Operator console.
- **Base-side:** the logged `scent_class` timeline on the ground-station — annotate the walk ("entered
  a solvent zone at 14:32, 6 min").

## Three firsts

- **First BME688 contract change** — its Metsuke moment; the gas sensor finally earns a field of its
  own rather than riding the shared `gas_ohms`.
- **First to change *both* contract halves.** [Metsuke](./metsuke.md) changed the **BLE** half (the
  grid), [Hokan](./hokan.md) the **CSV** half (`steps`); Kaori changes **both** — a logged
  `scent_class` timeline *and* a live Scent characteristic — because a scent is both a thing you review
  after and a thing you want to see now, and a label is cheap enough to do both (unlike the heat grid,
  which stayed BLE-only to spare the CSV).
- **First non-thrifty build** — the first module needing a **closed-source library**, an **offline
  training workflow**, and a **recommended board upgrade**. The series' pattern was "pure firmware over
  parts you own"; Kaori is where that ends, on purpose. See [The honest cost](#the-honest-cost).

## Contract change

**Kaori edits [CONTRACT.md](../../CONTRACT.md) on both halves.** Per the invariant, change the
contract **first**, then its three mirrors (firmware `CSV_HEADER` + the new characteristic/UUID; the
Kotlin `ShintaiGatt` UUID; the ground-station column handling).

**CSV half — two appended columns** (append at the end, after `steps`, so name-keyed parsers and the
`line[0].isdigit()` framing are unaffected; old logs still parse):

| Column | Unit / values | Meaning |
|--------|---------------|---------|
| `scent_class` | label (`none`/`unknown` when unrecognised, blank = no BME688) | Kaori BSEC2 top scent class |
| `scent_conf` | 0–1 (blank = none) | classifier confidence for `scent_class` |

`scent_class` is a short **string label** — the CSV already carries one (`cardinal`), so this fits
the schema. It's tiny and slow-changing, so logging it (unlike Metsuke's grid) is cheap and earns its
place in the CSV.

**BLE half — a new Scent characteristic** (`READ | NOTIFY`, UTF-8 string, matching the existing
string set), proposed UUID `abcd5ce7-ab12-ab12-ab12-abcdef123456` (fits the `abcdXXXX-…` pattern):

- Payload: `<label> <confidence>` e.g. `solvent 0.82`, `unknown 0.0` when nothing clears the
  threshold. A **string**, so — unlike Metsuke — **no MTU negotiation and no binary packing**.
- Notifies only while the BME688 is present and a scan has produced a class; slow cadence (a few
  seconds), gated up by the Kyūkaku reflex.
- **CCCD gotcha still applies** — the notify-enable descriptor is `00002902-0000-1000-8000-00805f9b34fb`;
  the `8000` (not `0000`) matters or the subscription silently dies.
- **Consumer coverage:** the Operator console subscribes (full-fidelity); Glass optionally shows the
  label but may skip it to stay lean, the same call it makes on Environment — decided in `android/` at
  build ([KrD-8](#decisions)).

## The honest cost

[Metsuke](./metsuke.md) had an "honest framing" section for staying *cheap* on the no-PSRAM board;
Kaori's is the inverse — it's honest about *spending*:

- **BSEC2 is a closed-source, Bosch-licensed blob.** It ships as a precompiled library, not editable
  source, under Bosch's license (free to use, redistribution restrictions). The first non-open
  dependency in the tree — worth a deliberate license note in the build ([KrD-6](#decisions)).
- **It is only as good as its training.** The classifier is built **offline** from labelled samples in
  BME AI-Studio; classes it never saw read as `unknown`. Garbage/insufficient training → a nose that
  hallucinates or shrugs. Budget the data-collection time, not just the firmware ([KrD-2](#decisions)).
- **Sensor drift + burn-in are real.** MOX response ages and needs conditioning; BSEC2 manages an
  internal state (persistable to flash) to compensate — carry that state across boots.
- **Board headroom.** BSEC2 state + config + the existing sensor/BLE load want the **N4R2 PSRAM**
  variant; the plain QT Py may fit it but with no slack for the next module ([KrD-5](#decisions)).

None of this is hidden — Kaori is the earned flagship, and its cost is documented so the choice to
build it is made with eyes open.

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino` (+ the BSEC2 library).

1. **Add BSEC2** and the trained `.config`; load persisted BSEC state from flash at boot.
2. **Own the BME688 heater.** Replace the single-shot `setGasHeater(320,150)` read with the BSEC2
   gas-scanner profile; still populate `bmeGas`/`gas_ohms` from a reference step ([KrD-4](#decisions)).
3. **Classify.** Feed each scan to BSEC2; take the top class + confidence; hold between scans.
4. **Duty-cycle on Kyūkaku.** Slow background scan cadence, raised when Kyūkaku posts a Spike
   ([KrD-3](#decisions)).
5. **Publish both halves.** Append `scent_class`/`scent_conf` to the CSV row (and flash log); add the
   Scent characteristic in `setup()` with its `BLE2902` CCCD and notify on each new class.
6. **Persist BSEC state** periodically to flash so calibration survives reboots.
7. **Degrades.** BME688 absent → no scan, `scent_class` blank, characteristic never notifies; the
   board boots and logs as before. No change to the 1500 ms telemetry cadence.

## Ground-station & Android / Shikai integration

- **Ground-station** (`groundstation/`, a small `kaori.py` + `analyze.py`): read `scent_class`/
  `scent_conf`; render a **scent timeline** (labelled bands on the session timeline) and a report line
  ("scents: solvent 6 min, coffee 2 min"); degrade cleanly when the columns are absent (old logs).
- **Android** (`android/`): add the Scent UUID to `ShintaiGatt.kt` mirroring the contract (mind the
  `8000` CCCD); the Operator console shows the live label; Glass optionally shows it. Parse is trivial
  (a string) — no MTU work, unlike Thermal Grid.

## Acceptance criteria

1. **Classifies trained scents:** presenting a trained scent yields its `scent_class` with confidence
   ≥ `SCENT_CONF_MIN` within a few seconds; clean air reads `none`.
2. **Rejects the untrained:** a scent absent from the training set reads `unknown` / low confidence —
   never a confidently wrong trained label.
3. **Both halves:** the class appears **both** in the CSV (`scent_class`/`scent_conf`, logged) **and**
   on the Scent BLE characteristic (live); the ground-station draws a scent timeline and a consumer
   shows the live label.
4. **Reflex-gated:** scanning duty-cycle visibly rises after a Kyūkaku Spike and idles in stable air.
5. **Legacy intact:** `gas_ohms`, the Environment characteristic, and Kyūkaku keep working while Kaori
   owns the heater ([KrD-4](#decisions)).
6. **State persists:** BSEC calibration state survives a reboot (no full re-burn-in each boot).
7. **Contract discipline:** CONTRACT.md, firmware `CSV_HEADER` + characteristic/UUID, the Kotlin UUID,
   and the ground-station parser all match; CCCD uses `8000`; old logs without the columns still parse.
8. **No regression:** existing CSV columns/order/cadence, existing characteristics, BLE notify, and
   flash logging are otherwise unchanged.

## Decisions

- **KrD-1 — Gas-scanner mode via BSEC2.** Kaori drives the multi-step heater profile and classifies
  with BSEC2; a full scan spans a few seconds — acceptable, scents are slow.
- **KrD-2 — Closed-set, offline-trained (v1).** Recognise only trained classes; `unknown` otherwise.
  Training is the offline AI-Studio workflow; the firmware ships a frozen `.config`. On-device/field
  retraining is forward.
- **KrD-3 — The reflex gates the cognition.** [Kyūkaku](./kyukaku.md) (cheap, always-on) duty-cycles
  Kaori (expensive, gated): background-slow, spun up on a Spike. Layered senses on one sensor — and a
  power/compute win.
- **KrD-4 — Keep `gas_ohms` alive.** Derive the legacy single-shot resistance from a reference scan
  step so `gas_ohms`, Environment, and Kyūkaku are undisturbed by Kaori owning the heater.
- **KrD-5 — N4R2 recommended, not mandatory.** BSEC2 state + config + the resident load favour the
  PSRAM host; the plain QT Py may run it with no slack. Flag the board choice at build.
- **KrD-6 — BSEC2 license noted.** First closed-source dependency; record its Bosch license /
  redistribution terms in the build docs.
- **KrD-7 — No Aizu cue (v1).** Output is the contract (HUD + ground-station), like Metsuke; a trained
  **alert-scent → Aizu** cue is deferred to keep this already-heavy module scoped.
- **KrD-8 — Consumer coverage is a per-app call.** Operator subscribes to Scent; Glass may skip it to
  stay lean (as it does Environment) — decided in `android/`, not here.

## Cross-spec impact

- **CONTRACT.md (on build)** — the first **both-halves** change: append `scent_class`/`scent_conf` to
  the CSV table **and** add the Scent characteristic (`abcd5ce7-…`, string) to the GATT table, with a
  short Scent section. **Not applied while this is spec-only** — applying it before the firmware exists
  would break the three-mirror linter (`tools/check-contract.py`); the edit lands with the build,
  moving CONTRACT.md + `CSV_HEADER` + Kotlin together.
- **Kyūkaku** — becomes Kaori's **trigger**: its Spike duty-cycles the classifier ([KrD-3](#decisions)).
  Kyūkaku (reflex) and Kaori (cognition) are the two layers of the one nose; no change to Kyūkaku's
  own behaviour.
- **Metsuke / Hokan** — Kaori completes the contract-change matrix they started: Metsuke = BLE half,
  Hokan = CSV half, **Kaori = both**.
- **Registry (build-time)** — Kaori earns a Zōkyō row beside Kyūkaku; note it's the first BME688
  contract change, the first both-halves change, and the first non-thrifty build. The **BSEC2 library**
  and **BME AI-Studio** want a mention in the [parts catalog](../../REGISTRY.md#parts-catalog) as the
  BME688's "AI gas model" upgrade (the catalog already flags the Bosch **BSEC** route there).

## Forward path

- **Alert-scent → Aizu:** flag a specific trained class as dangerous/target and post an Aizu cue when
  it appears — Kaori's own colour on the shared pixel (the KrD-7 deferral).
- **Calibrated IAQ / eCO₂ / bVOC:** BSEC2 also yields self-calibrating air-quality indices — a free
  upgrade path that turns Kyūkaku's raw needle into a *calibrated* air sense.
- **On-device / field retraining:** capture-and-label on the wrist to add a class without the PC
  workflow — the hard, desirable version of scent-memory.
- **More classes / hierarchical scents:** grow the trained set; group classes ("solvent" → acetone /
  toluene) as the classifier and board headroom allow.
