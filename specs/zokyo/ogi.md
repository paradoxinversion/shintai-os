# Ōgi (扇) — spec

*The folding fan: the rearguard stops pointing and starts sweeping — a rear field resolved into a fan of bearings, so "behind you" gains an angle.*

**Status:** proposed (2026-07-08) — spec only; not built · **Zōkyō:** Ōgi (successor to [Kōei (後衛)](./koei.md); sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) — **changes both halves** (appends CSV scalars + adds the **second binary** characteristic) · **Shares:** [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence) (feeds the reflex, unchanged) · **Date:** 2026-07-08

> Realizes [Kōei](./koei.md)'s [*Angle-resolved rear field*](./koei.md#forward-path) forward path. Inherits Kōei's power note: a multizone ToF draws more than the two VL53L4CX it replaces, so Ōgi stays a **bench / USB-power-bank** sense until the LiPo story changes. Solderless — Qwiic.

## What this is

Kōei grew the rear sense from one beam to **two** (left / right). Ōgi grows it from two
beams to a **fan**. It swaps Kōei's pair of single-zone [VL53L4CX](../../REGISTRY.md#sensors)
for one **multizone [VL53L5CX](../../REGISTRY.md#sensors)** — an 8×8 grid of ranges across a
~45° field of view — and collapses that grid into a horizontal **fan of azimuth bins**, so a
contact behind you carries a real **bearing**, not just a side. "Something on your left"
becomes "something at −18° behind you, 1.2 m."

The rear overlay stops being two fixed blips and becomes a **swept contact** at its true
azimuth; the reflex keeps firing on the nearest object anywhere in the fan; and — the day
[Aizu](../platform/aizu.md)'s DRV2605L haptics land — the bearing drives a *positioned* buzz,
not just a left/right motor.

Ōgi is deliberately scoped as **replace, not augment**: one L5CX *becomes* the rear field
([OG-1](#decisions)). One coherent sensor → one coherent fan → one clean contract. Re-adding
Kōei's two VL53L4CX as **wide flankers** past the L5CX's cone is a real coverage gain, but it
muddies the fan abstraction, so it is deferred to the [forward path](#forward-path) — which the
[PCA9546 mux](../../REGISTRY.md#host--infrastructure) Kōei already installed makes cheap (spare
channels, same select-before-touch discipline).

## Why

- **The single most useful upgrade to a rear sense is an angle.** Kōei proved left-vs-right is
  worth more than a lone "behind you" number; Ōgi proves a *bearing* is worth more than
  left-vs-right — where exactly the thing is, swept continuously, from one part.
- **The platform already solved "a sensor makes a small array, not a scalar."** [Metsuke](./metsuke.md)
  is the one binary GATT characteristic (a packed thermal grid, parsed in `:core`, rendered
  from a shared palette). A rear fan is the *same shape of problem*, so Ōgi reuses that whole
  idiom end to end — it is the **second binary characteristic**, not a new invention ([OG-3](#decisions)).
- **Nothing downstream has to break.** Ōgi keeps `distance_l_mm` / `distance_r_mm` (now derived
  from the fan's left/right halves), so Kōei's `Distance` char, the `alert` bit, Kehai's reflex,
  and *both apps' existing overlays* keep working unchanged — the azimuth is **additive**
  ([OG-2](#decisions)). Old logs still parse.

## Goals

1. Run **one VL53L5CX** in 8×8 continuous ranging behind the PCA9546 mux (ch0), select-before-touch.
2. **Collapse** the grid vertically into a horizontal fan of **N = 8 azimuth bins**, each the
   nearest valid range at that bearing (0 = straight back, − = wearer's left, + = right).
3. Publish two **summary scalars** — `rear_azimuth_deg` / `rear_range_mm` (the nearest contact's
   bearing + distance) — to CSV / flash, appended at the end of the schema.
4. Publish the **full fan** over a new **binary `Rear Field`** BLE characteristic (à la Metsuke),
   parsed in `:core`, rendered by both apps as a swept rear arc.
5. **Preserve Kōei's outputs:** derive `distance_l_mm` / `distance_r_mm` from the fan halves so
   the `Distance` char, `alert`, Kehai's reflex, and the current overlays are untouched.
6. Reflex keys off **min over the fan** (identical "nearest object" semantics, generalized).
7. **Non-fatal:** missing mux or missing L5CX → the rear sense blanks, everything else runs;
   BLE keeps advertising.

## Non-goals

- **No elevation (v1).** The grid is collapsed to a horizontal fan; the vertical axis (is the
  contact high or low?) is discarded. Using it is a [forward path](#forward-path).
- **No wide flankers (v1).** The two VL53L4CX retire; re-adding them past the L5CX's ~45° cone is
  the *augment* [forward path](#forward-path), not this module ([OG-1](#decisions)).
- **No multi-target per zone.** Each bin takes the nearest valid target in its column; the L5CX's
  optional second-target-per-zone data is not published.
- **No new *string* characteristic.** The fan rides a **binary** char (Metsuke's idiom); the
  `Distance` string char is unchanged (still `L:.. R:.. mm`, now fan-derived) ([OG-3](#decisions)).
- **No XSHUT / address juggling.** Same as Kōei — the mux is the isolation mechanism ([KO-1](./koei.md#decisions)).

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [VL53L5CX](../../REGISTRY.md#sensors) | multizone rear ToF — 8×8 grid → azimuth fan (`rear_azimuth_deg` / `rear_range_mm` + `Rear Field` char) | **new BOM** (multizone; **replaces** Kōei's 2× VL53L4CX) |
| **[PCA9546A mux](../../REGISTRY.md#host--infrastructure)** (0x70) | fronts the L5CX on ch0; keeps ch1–ch3 free for the flanker forward path | already installed (Kōei) |
| [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence) | consumes min-over-fan → Aizu reflex + `alert` | already built, unchanged |
| [RayNeo / phone](../../REGISTRY.md#output--feedback) | both apps render the swept fan — glass: nearer hero + rear arc sweep; operator: polar fan | [Shikai](../../REGISTRY.md#shikai-視界--field-of-view) |

> **Hardware figures to confirm on the datasheet / bench before build:** FoV (≈45°×45° / 63° diagonal — sets the bin→bearing map), max range at 8×8 (≈4 m white target), continuous-ranging rate at 8×8 (≈15 Hz), current draw (sets the power headroom vs. the two L4CX), and the ULD firmware-blob size uploaded at init (≈84 KB — see [firmware](#firmware-integration)).

## Behaviour — read, collapse, reconcile

**The fan.** The L5CX returns an 8×8 grid of `(distance, target_status)` per zone. Ōgi keeps
only **valid** zones (target status in the library's "good range" set) and collapses each of the
**8 columns** to its **minimum valid distance** — the nearest object at that bearing. Result: a
fan of 8 `(bin → distance)` values.

**Bin → bearing.** Bin `i` (0…7) maps to azimuth `az(i) = ((i + 0.5)/N − 0.5) × FoV_h`, so the
fan spans `±FoV_h/2` about **0° = straight back**; **negative = wearer's left, positive = right**
(the physical orientation is recorded authoritatively in [REGISTRY.md](../../REGISTRY.md), like
Kōei's ch0/ch1). Column-order → left/right handedness is a mount-time calibration ([OG-5](#decisions)).

**Summary.** `nearest_bin = argmin(fan)`; `rear_range_mm` = its distance; `rear_azimuth_deg` =
`az(nearest_bin)`. Both blank when no bin has a valid target.

**Back-compat halves.** `distance_l_mm = min(left-half bins)`, `distance_r_mm = min(right-half
bins)`. So `nearerMm(l, r)` is unchanged, and with it Kōei's `Distance` char, the `alert` bit,
Kehai's reflex band, and both apps' existing L/R overlays — all keep working with **zero change**.
The fan is strictly *more* information layered on top.

**The reflex** keys off `min(fan)` (== `nearerMm(l, r)`): one object anywhere in the rear cone
trips Kehai + `alert`; with several, it tracks the closest. Shape of the `alert` bit unchanged.

**Degradation.** Missing mux (0x70 no-ACK) → rear sense off, everything else runs. Missing L5CX
(init fails on ch0) → `rear_*` and both distance columns blank, no `Rear Field` notify, reflex
idle on the rear. Per-frame invalid zones are filtered by target status before the collapse.

## Contract impact

**Changes both halves.**

- **CSV — append two scalars.** `rear_azimuth_deg` (0 = back, ± left/right, blank = no contact)
  and `rear_range_mm` (blank = no contact), at the **end** of the schema (the `steps` / `board` /
  `pm*` / `nfc_*` append convention). `distance_l_mm` / `distance_r_mm` **stay** (now fan-derived),
  so consumers keying on column names — and pre-Ōgi logs — are unaffected.
- **BLE — add the second binary characteristic.** A new **`Rear Field`** char (new UUID) carries
  the packed fan (below). Like [Metsuke](./metsuke.md)'s `Thermal Grid` it goes in
  `ShintaiGatt.BINARY`, is kept **out** of `ShintaiGatt.ALL` (the string set), and each app appends
  it explicitly; `:core` `foldBinary` parses it into a `RearField` model. The `Distance` string
  char (`abcd1234…`) is **unchanged** — still `L:.. R:.. mm`, now fan-derived. `tools/check-contract.py`
  gains a `"Rear Field" → "REAR_FIELD"` row in `CHAR_TO_KOTLIN`.

### Rear Field (binary)

The **second binary characteristic** (Metsuke's grid was the first). Little-endian; N = 8 bins,
so **22 bytes** — well under the default ATT payload, and far under the MTU the apps already
negotiate for Metsuke (247).

| Offset | Type | Field | Meaning |
|--------|------|-------|---------|
| 0 | `uint8` | `bins` | fan width N (8) |
| 1 | `uint8` | `nearest_bin` | index of the nearest contact (`0xFF` = no contact) |
| 2 | `int16` | `nearest_az_ddeg` | nearest contact azimuth ×10 (°·10, signed; `−225` = 22.5° left) |
| 4 | `int16` | `fov_ddeg` | horizontal FoV ×10 (so the consumer places bins without hardcoding) |
| 6 | `N × uint16` | `dist_mm` | per-bin nearest range in mm, left→right (`0xFFFF` = no target) |

- The consumer draws a rear arc spanning `fov_ddeg`, one blip per bin at `az(i)` and radius =
  `dist_mm[i]`, and highlights `nearest_bin`. Each blip is phosphor until its range breaks
  `NEAR_MM`, then red/blink — **amber unused** on the waveguide (Kōei's [KO-4](./koei.md#decisions)
  alarm-fatigue rule carries over).
- **Live-only** — like the thermal grid, the fan is BLE-only; the CSV/flash keep the summary
  scalars (`rear_azimuth_deg` / `rear_range_mm`) + the derived halves as the logged representation.
- CCCD gotcha still applies (the `8000`, per [CONTRACT.md](../../CONTRACT.md)).

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. **This is a new driver, not "Kōei doubled."**

1. **New library.** The L5CX uses ST's **ULD** (Ultra-Lite Driver) — a different API from the
   VL53L4CX class, and it **uploads a ~84 KB firmware image to the sensor at init** (seconds of
   boot time + flash budget; the sketch is ~42% of flash today). Pin **`STM32duino VL53L5CX`** in
   `sketch.yaml` (same vendor as the L4CX lib) ([OG-4](#decisions)).
2. **One sensor on ch0.** Replace `sensorL` / `sensorR` (and `hasTofL` / `hasTofR`) with a single
   `rearField` object + `hasRearField`, inited under `muxSelect(0)` (keep the mux discipline so the
   flanker forward path stays a drop-in). `tofMuxPresent` boot-ACK of 0x70 unchanged.
3. **`serviceRearField()`** replaces `serviceTofArc()` as the sole rear read site (still in the
   reflex tick, still under `muxSelect`): read the 8×8 grid, filter by target status, collapse to
   the 8-bin fan, compute `nearest_bin` / `rear_range_mm` / `rear_azimuth_deg`, and derive
   `cachedMmL` / `cachedMmR` from the halves so telemetry + reflex read the caches as before.
4. **Emit** — human (`fan: -18° @ 1.2 m` + a compact bin strip), CSV (append `rear_azimuth_deg`,
   `rear_range_mm`; `distance_l_mm` / `distance_r_mm` unchanged, now derived), BLE (`Distance` =
   `L:.. R:.. mm` unchanged + the new `Rear Field` binary notify, gated on a subscribed central like
   Metsuke's grid).
5. **Diagnostics** — `probeTof` ('T') reports the fan (per-bin ranges) instead of two arcs;
   `scanI2C` ('I') still expects 0x70 and notes 0x29 is gated behind the mux.

## Acceptance criteria

1. **Fan live:** with the mux + L5CX attached, the 8-bin fan reads valid mm across the FoV; 'T'
   shows per-bin ranges.
2. **Bearing correct:** an object swept left→right behind the wearer moves `rear_azimuth_deg`
   monotonically −→+; a centred object reads ≈0°.
3. **Back-compat intact:** `distance_l_mm` / `distance_r_mm`, the `Distance` char, `alert`, and
   Kehai's reflex behave exactly as under Kōei (nearest-object semantics), now fan-derived.
4. **Binary round-trip:** the `Rear Field` payload parses in `:core` into the `RearField` model;
   both apps render the swept arc; `nearest_bin` highlights the closest contact.
5. **Non-fatal:** BLE advertises with the mux absent and with the L5CX absent; a missing sensor
   blanks the rear columns only.
6. **No regression:** contract linter green; firmware compiles; `android/build.sh detekt lint`
   green; 1500 ms telemetry cadence + flash logging unchanged; boot time increase from the ULD
   firmware upload is within tolerance.

## Decisions

- **OG-1 — Replace, not augment (committed for v1).** One L5CX *becomes* the rear field — a single
  coherent fan, a clean contract — over splicing an 8-bin center grid with two coarse L4CX flankers.
  Wider spread (re-adding the L4CX on the mux's spare channels) is a coverage gain deferred to the
  forward path, where the fan/flanker fusion can be designed on its own terms.
- **OG-2 — Keep `distance_l_mm` / `distance_r_mm`, fan-derived (committed).** The azimuth is
  additive; deriving the halves from the fan means Kōei's char, `alert`, the reflex, and both
  overlays need **zero** change and old logs still parse. Cheaper and safer than reshaping the
  `Distance` char again.
- **OG-3 — Binary `Rear Field` char, reusing Metsuke's idiom (committed).** The fan is a small
  array → a packed binary characteristic (the platform's second), parsed in `:core`, kept out of
  `ALL`, appended per-app — exactly as `Thermal Grid`. Over encoding the fan into a bloated string.
- **OG-4 — ULD driver, accept the firmware upload (committed).** The L5CX needs ST's ULD + its
  ~84 KB init blob; accepted as the cost of multizone. Flag flash budget + boot time in bring-up.
- **OG-5 — 8 bins, horizontal only (committed for v1).** 8×8 collapsed to an 8-wide horizontal fan;
  elevation and 4×4-@-60 Hz are forward paths. 8 bins across ~45° ≈ 5.6°/bin — enough for a
  meaningful swept bearing without a heavy payload.

## Cross-spec impact

- **Registry:** Ōgi earns a row in the [Zōkyō table](../../REGISTRY.md#zōkyō); the **VL53L5CX** joins
  the [sensors catalog](../../REGISTRY.md#sensors) (the VL53L4CX ×2 row is marked *retired by Ōgi /
  reserved for the flanker forward path*); the bin→bearing orientation is recorded under
  [Tanchi](../../REGISTRY.md#tanchi-探知--detection) beside Kōei's ch0/ch1 map.
- **Contract:** `CONTRACT.md` CSV schema (append `rear_azimuth_deg` / `rear_range_mm`) + GATT table
  (`Rear Field` binary char + a "Rear Field (binary)" section) + the Bunshin authority table (a
  `rear_*` row — rear arc rides the pack, **aft → fwd**, matching Kōei's distance precedence).
- **Kōei:** superseded, not deleted — its spec's *Angle-resolved rear field* forward path points
  here; its mux, reflex wiring, and L/R contract fields live on underneath Ōgi.
- **Kehai:** no reflex-spec change — it consumes min-over-fan where it used to consume the nearer arc.

## Forward path

- **Wide flankers (the augment).** Re-add Kōei's two VL53L4CX on the mux's spare channels (ch1/ch2)
  as wide left/right beams past the L5CX's ~45° cone — fuse them into the fan's outer bins for
  ~90°+ rear coverage with a fine centre. The mux discipline already supports it.
- **Elevation.** Stop collapsing the vertical axis — publish an 8×8 rear *field* (not just a fan)
  so "high on your left" (a branch) reads differently from "low on your left" (a curb). A larger
  binary payload, still under MTU.
- **Directional haptics.** When [Aizu](../platform/aizu.md)'s DRV2605L sink lands, `rear_azimuth_deg`
  drives a **positioned** buzz around a belt/temple array — you feel *where*, not just *which side*.
- **Approach vector.** Δ`rear_range_mm` over Δ`rear_azimuth_deg` gives closing speed + crossing
  direction — "overtaking on your left, fast" — a base-side (or on-device) derived cue.
