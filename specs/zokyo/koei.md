# Kōei (後衛) — spec

*Rearguard: two ToF beams watching your back left and right — the rear radar grown from one point to a spread arc.*

**Status:** built (2026-07-03) · **Zōkyō:** Kōei (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) — **changes it** (both halves) · **Shares:** [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence) (feeds the reflex) · **Date:** 2026-07-03

> Sibling to the Rokkan series; benched on the USB power bank (two ToF + mux exceed the 500 mAh LiPo's comfortable draw). All solderless — Qwiic, no iron.

## What this is

Kōei — "rearguard" — grows the single rear [VL53L4CX](../../REGISTRY.md#sensors) into a
**left/right pair**, so the rear proximity sense stops being one beam and becomes a
**spread arc**: something coming up on your left and something on your right are now
distinguishable, not collapsed into one "behind you" number. It's the original
rear-radar scaled to the dual-arc the roadmap always wanted — the project, not an
accessory.

The obstacle is that both VL53L4CX are hard-wired to I²C address **0x29** and collide on
the plain bus. Kōei puts a **PCA9546 mux (0x70)** between them and the host: each sensor
lives on its own channel (**ch0 = left, ch1 = right**), selected one-hot **before every
bus touch**. No XSHUT address-juggling, no extra GPIO — one solderless Qwiic part.

It earns two **firsts** in the series (see [Two firsts](#two-firsts)):
1. **First multi-instance sensor.** Every prior Tsukiwaza took one of a kind; Kōei runs
   two identical sensors that can't coexist without the mux — the first part in the
   [catalog](../../REGISTRY.md#host--infrastructure) whose job is *arbitrating the bus*,
   not sensing.
2. **First reshape of an existing contract field.** Metsuke *added* a binary
   characteristic and Hokan *appended* a CSV column; Kōei is the first to **split a CSV
   column** (`distance_mm` → `distance_l_mm` / `distance_r_mm`) and **repack an existing
   characteristic's payload** (`Distance` now carries `L:.. R:.. mm`) rather than adding
   a new one.

## Why (the thrifty case)

- **Left/right from a sensor you already ship.** A second VL53L4CX + a $1 mux turns "an
  object is behind you" into "an object is behind you, on your left" — the single most
  useful thing you can add to a rear proximity sense, for the price of one breakout.
- **The mux is reusable infrastructure.** The PCA9546 solves 0x29-vs-0x29 today and any
  future address collision tomorrow (a third arc, a second IMU) — it's a platform part,
  not a one-off.
- **No new consumer.** [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence)'s reflex and
  the single `alert` bit already exist; Kōei feeds them the **nearer** arc, so the whole
  on-body reflex path lights up with zero new output code.

## Goals

1. Run **two VL53L4CX** behind the PCA9546 mux, each on its own channel, with
   select-before-touch on every access.
2. Publish **both arcs** end to end — serial (human + CSV), onboard flash, and BLE.
3. Keep the on-body reflex + `alert` working off the **nearer** arc, so the wearer is
   warned by whichever beam sees the closest object.
4. **Non-fatal at every level:** a missing mux disables both arcs; a missing arc blanks
   only its column; BLE keeps advertising regardless.

## Non-goals

- **No true bearing.** Each arc is a fixed left/right beam, not an angle-resolved contact:
  the app overlays place the two blips at fixed left/right bearings (radius = distance),
  not the object's real azimuth. A steered / multizone ToF (VL53L5CX) is a different module
  ([forward-path](#forward-path)).
- **No new BLE characteristic.** Both arcs ride the existing `Distance` char as one
  packed string — the codebase's idiom for multi-component readings (Accel, GPS,
  Climate) ([KO-2](#decisions)).
- **No XSHUT / address reassignment.** The mux is the isolation mechanism; per-sensor
  address rewriting (and the GPIO it burns) is explicitly not taken ([KO-1](#decisions)).

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [VL53L4CX](../../REGISTRY.md#sensors) ×2 | rear-left / rear-right ToF (`distance_l_mm` / `distance_r_mm`) | second unit added; both at 0x29 |
| **[PCA9546A mux](../../REGISTRY.md#host--infrastructure)** (0x70) | isolates the two 0x29 sensors onto ch0 / ch1 | new Qwiic part |
| [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence) | consumes the nearer arc → Aizu reflex + `alert` | already built |
| [RayNeo / phone](../../REGISTRY.md#output--feedback) | **both** apps render both arcs — glass: nearer hero + rear overlay; operator: two blips + L/R | [Shikai](../../REGISTRY.md#shikai-視界--field-of-view) |

## Behaviour — select, read, reconcile

**The mux discipline.** The VL53L4CX library hits I²C on init *and* every read, so **every
access to an arc is preceded by `muxSelect(ch)`** — a one-hot write (`1 << ch`) to 0x70.
ch0 selects left, ch1 selects right. Init: select ch0 → init left; select ch1 → init
right. Read loop (the reflex tick, the sole read site): select ch0 → read left → select
ch1 → read right. Never touch a sensor without selecting its channel first.

**Channel → direction** (authoritative; also in [REGISTRY.md](../../REGISTRY.md#rokkan-六感--sixth-sense)):

| Mux channel | One-hot | Arc | CSV column |
|-------------|---------|-----|------------|
| ch0 | `0x01` | rear-left  | `distance_l_mm` |
| ch1 | `0x02` | rear-right | `distance_r_mm` |

**The nearer arc.** `alert` and Kehai's reflex band both key off `nearerMm(l, r)` — the
closer of the two valid ranges (ignoring `-1`/no-target). One object on one side still
trips the reflex; the wearer feels the *closest* threat regardless of which beam saw it.
The single `alert` bit is unchanged in shape.

**Degradation.** Missing mux (0x70 no-ACK) → both arcs disabled, everything else runs.
Missing one arc (InitSensor fails on its channel) → that column blanks, the other arc and
the reflex keep going. A ranging sensor can also return `-1`/invalid per read; the
existing validity guard (`RangeStatus == 0`) still filters before publish.

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. Mirrors the single-ToF path, doubled.

1. **Two sensor objects** `sensorL` / `sensorR` (mux ch0/ch1), two presence flags
   `hasTofL` / `hasTofR`, two caches `cachedMmL` / `cachedMmR`. `muxSelect()` +
   `nearerMm()` helpers; `tofMuxPresent` from a boot ACK of 0x70.
2. **`initTof(sensor, ch, label)`** — `muxSelect` → `begin` → `InitSensor(0x29)` →
   reflex-tune → start, per arc, non-fatal. Called for both only if the mux ACKed.
3. **`serviceTofArc()`** in the reflex tick reads each arc under its `muxSelect`; the tick
   posts the Kehai band for `nearerMm(cachedMmL, cachedMmR)`. Still the **sole read site**
   — telemetry consumes the caches, so the two consumers never fight over `dataReady`.
4. **Emit both** everywhere the single distance went: human (`L … R …`), CSV (two
   columns), BLE (`Distance` = `L:1234 R:1180 mm`, per-arc `--` = no target).
5. **Diagnostics** — `probeTof` ('T') probes both arcs; `scanI2C` ('I') expects 0x70 and
   notes 0x29 is now gated behind the mux (absent from a naive scan is *correct*).

## Contract impact

**Changes both halves** — the first Tsukiwaza to reshape rather than append.

- **CSV.** `distance_mm` → `distance_l_mm` / `distance_r_mm` (keeps the schema's `_mm`
  unit-suffix convention). Old logs without the columns still parse; the ground-station
  tools key on column *names*, so the split is transparent to them.
- **BLE.** The `Distance` characteristic (`abcd1234…`, unchanged UUID) now carries a
  packed `L:.. R:.. mm` string — no new characteristic, so `ShintaiGatt`, the CCCD, both
  apps' subscribe lists, and `tools/check-contract.py` are untouched. The `:core` parser
  (`Readings.kt`) splits the payload into `distanceLMm` / `distanceRMm`; **both apps render
  both arcs** — the glass HUD keeps a nearer-arc hero plus a rear dual-arc overlay, the
  operator console shows two tracker blips + an L/R readout (and a two-column `distance_l` /
  `distance_r` in its on-phone BLE recording).

Both mirror sites edited in lock-step with `CONTRACT.md`; `python3 tools/check-contract.py`
stays green.

## Acceptance criteria

1. **Both arcs live:** with the mux and both sensors attached, `distance_l_mm` and
   `distance_r_mm` both read valid mm on their channels (verify with 'T').
2. **Select-before-touch:** every read/init is preceded by `muxSelect`; the arcs never
   cross-read (left value never appears on the right column).
3. **Nearer-arc reflex:** an object on either side trips Kehai + `alert`; with objects on
   both sides, the reflex tracks the closer one.
4. **Non-fatal:** BLE advertises with the mux absent, with one arc absent, and with both
   absent; a missing arc blanks only its column.
5. **Serial / CSV / flash / BLE** all carry both arcs; the packed `Distance` payload
   round-trips through `:core` into `distanceLMm` / `distanceRMm`.
6. **No regression:** contract linter green; firmware compiles; `android/build.sh detekt
   lint` green; the 1500 ms telemetry cadence and flash logging unchanged.

## Decisions

- **KO-1 — Mux, not XSHUT (committed).** The PCA9546 isolates the two 0x29 sensors;
  per-sensor address reassignment (and the GPIO per sensor it needs) is not taken. The
  mux doubles as reusable infrastructure for future collisions.
- **KO-2 — One reshaped char, not a new UUID (committed).** Both arcs pack into the
  existing `Distance` string (`L:.. R:.. mm`), mirroring Accel/GPS/Climate multi-value
  chars — over a second `Distance R` characteristic. Keeps the GATT table + all three
  mirror sites lean; cost is a one-time `:core` parser change.
- **KO-3 — `distance_l_mm` / `distance_r_mm` (committed).** Symmetric rename keeping the
  `_mm` unit suffix the rest of the schema uses, over the shorter `distance_l` /
  `distance_r`.
- **KO-4 — Per-arc UI in both apps (built).** The **operator** console renders both arcs —
  two contacts on the motion tracker (left arc upper-left, right upper-right) plus an L/R
  readout, hero numeral = the nearer arc. The **glass** HUD keeps the nearer-arc DSEG hero
  and adds a compact **rear dual-arc overlay** (origin at top = you, left contact down-left,
  right down-right); each blip is phosphor until its arc breaks NEAR_MM, then red/blink —
  amber is not used, per the waveguide's alarm-fatigue rule.
- **KO-5 — ch0 = left, ch1 = right (committed).** The physical channel→direction mapping,
  recorded authoritatively in [REGISTRY.md](../../REGISTRY.md#rokkan-六感--sixth-sense).

## Cross-spec impact

- **Registry (build-time):** Kōei earns its own row in the [Zōkyō table](../../REGISTRY.md#zōkyō)
  beside Rokkan; the PCA9546 joins the [parts catalog](../../REGISTRY.md#host--infrastructure)
  and the VL53L4CX row is marked ×2; the channel→direction table lives under
  [Tanchi](../../REGISTRY.md#tanchi-探知--detection).
- **Contract:** `CONTRACT.md` CSV schema + `Distance` GATT example (both mirror sites)
  updated together.
- **Kehai:** no code change to the reflex spec — it simply consumes the nearer arc where
  it used to consume the lone distance.

## Forward path

- **Angle-resolved rear field.** Both app overlays (operator tracker, glass rear arc) are
  built against `distanceLMm` / `distanceRMm`, but show two *fixed* left/right beams. A
  steered or multizone ToF (VL53L5CX) would give a real rear **azimuth** — upgrading both
  overlays from two blips to a swept contact.
- **Directional haptics.** Once [Aizu](../platform/aizu.md)'s planned DRV2605L sink lands,
  left/right arcs drive left/right motors — "something's on your left" you *feel* on your
  left.
- **A third arc / wider spread.** The mux has spare channels; a centre-rear or side arc
  drops onto ch2 with the same discipline, no new address problem.
