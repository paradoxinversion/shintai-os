# Katsuryoku (活力) — spec

*Vitality: the body's sense of its own energy — how full the tank, how hard it's working. The first sense that looks inward.*

**Status:** proposed (2026-07-08) — spec only; not built · **Zōkyō:** Katsuryoku (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense); the first **interoceptive** Zōkyō) · **Seam:** [CONTRACT.md](../../CONTRACT.md) — **changes both halves** (appends CSV scalars + a `Power` string char) · **Shares:** [Aizu](../platform/aizu.md) (low-battery cue), [Bunshin](./bunshin.md) (per-pod health) · **Date:** 2026-07-08

> Answers the recurring power note that caps [Kōei](./koei.md) / [Ōgi](./ogi.md) ("exceeds the 500 mAh LiPo's comfortable draw"): **instrument before you fix.** Katsuryoku measures the real draw + reserve so the LiPo delivery fix (bigger cell vs. a stiff boost source) is chosen from numbers, not ballparks. Solderless on the Qwiic bus — except the INA260's one in-line insertion.

## What this is

Every Zōkyō so far reads the **outside world** — distance, heat, air, tags. Katsuryoku is the
first that reads the **platform itself**: the body's sense of its own energy. It adds two I²C
power gauges and publishes what they see —

- the **[MAX17048](../../REGISTRY.md#host--infrastructure)** fuel gauge (`0x36`): the **reserve** —
  cell voltage + **state-of-charge %** + charge/discharge rate, modelled from the LiPo's discharge curve;
- the **[INA260](../../REGISTRY.md#host--infrastructure)** power monitor (`0x40`): the **expenditure** —
  actual **current (mA)**, bus voltage, and **power (mW)** through an integrated high-side shunt.

Together: how much is left, and how fast it's burning.

The INA260 is a **portable in-line probe**, not a fixed fixture ([KT-1](#decisions)). It measures
current *through* itself, so it's inserted in series on **whichever chain you want to profile** —
[Bunshin](./bunshin.md)'s `fwd` pod, the `aft` pod, or a bench rig. Whichever chain it's on reports
that chain's draw; move it to profile another. Attribution is **free**: every CSV row already carries
its pod's `board` tag, so a `load_ma` on a `board=fwd` row *is* fwd's draw — no new field needed.

## Why

- **You can't fix a power budget you haven't measured.** The "exceeds comfortable draw" note has
  been theory across three specs. The INA260 makes it a number: watch it while GPS acquires + the
  PMSA fan spins + BLE bursts, and the rail either **sags** (peak-current brownout → needs a stiffer
  source) or just **drains** (runtime → needs a bigger cell). Katsuryoku settles that with data.
- **The reserve is the actionable half.** State-of-charge % is *when to charge* — the one number a
  wearable actually needs about itself, and the basis for the low-battery cue [Aizu](../platform/aizu.md)
  was built to want (its battery-idle logic already treats `!Serial` as "on battery").
- **Bunshin makes it inherently per-pod.** Two pods, two batteries, two draws — you want to see
  **both**, never a merged winner. Katsuryoku realizes Bunshin's [`perBoard` health forward
  path](./bunshin.md#forward-path): each pod's vitality, side by side.

## Goals

1. Read the **MAX17048** (SoC %, cell V, charge rate) and the **INA260** (mA, mW, bus V) each
   telemetry cycle; both plain-bus, both presence-gated, both non-fatal if absent.
2. Publish **reserve + draw** to CSV / flash, appended at the end of the schema, attributed to the
   producing pod by the existing `board` tag.
3. Notify a new **`Power`** BLE string characteristic — the live twin (`SOC:78% 3.92V 214mA 1.06W`).
4. **Keep it per-pod:** power columns are **excluded from the Bunshin authority table** — never
   coalesced to a winner; consumers surface each pod's power side-by-side (the `perBoard` model) ([KT-2](#decisions)).
5. Drive a **low-battery [Aizu](../platform/aizu.md) cue** off SoC — a banded amber→red as the charge
   falls (like Kanki's CO₂ ramp), on a high Aizu priority rung ("you're about to lose the platform") ([KT-3](#decisions)).
6. Degrade **per-part:** INA260 present but no MAX17048 (e.g. on the USB bank, no cell to gauge) →
   `load_*` fill, `batt_*` blank; and vice-versa. Neither part blocks the other.

## Non-goals

- **No power *delivery* hardware (this module).** Katsuryoku only **measures**. Choosing/adding the
  fix — bigger LiPo, boost+charger, bulk caps — is the follow-on the numbers inform, not this spec.
- **No coalesced "system" power.** Each chain's draw is its own; there is no merged all-pods total in
  v1 (summing two independently-clocked pods is a base-side derived concern) ([KT-2](#decisions)).
- **No power *gating*.** Katsuryoku doesn't switch sensors off to save current (a load-switch /
  duty-cycling module is a [forward path](#forward-path)); it reports, it doesn't throttle.
- **No coulomb-counting fusion.** SoC comes from the MAX17048's ModelGauge, not from integrating the
  INA260's current — the two gauges are read and reported independently, not fused ([KT-4](#decisions)).

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [MAX17048](../../REGISTRY.md#host--infrastructure) | LiPo fuel gauge — SoC % + cell V + rate (`batt_pct` / `batt_v`), I²C `0x36` | **new BOM** (Adafruit #5580, STEMMA QT) |
| [INA260](../../REGISTRY.md#host--infrastructure) | high-side current/power monitor — draw (`load_ma` / `load_mw`), I²C `0x40`; **in-line, portable** | **new BOM** (Adafruit #4226, STEMMA QT) |
| [Aizu](../platform/aizu.md) | low-battery SoC cue on the NeoPixel | already built |
| [RayNeo / phone](../../REGISTRY.md#output--feedback) | per-pod power in the `perBoard` health strip | [Shikai](../../REGISTRY.md#shikai-視界--field-of-view) |

> **The one wiring task:** the INA260 measures current through an internal 2 mΩ shunt between VIN+
> and VIN−, so it goes **in series** in the chain's positive supply (source → INA260 → the pod). The
> MAX17048 just senses across the cell. Both read out over I²C (Qwiic) as usual. `0x36` / `0x40` are
> **free** on the current bus — no collision, no mux.

## Behaviour — read, attribute, warn

**Read (telemetry cadence).** Power changes slowly, so both gauges are read in the 1.5 s telemetry
block, not the fast reflex loop. MAX17048 → `batt_pct` (SoC %), `batt_v` (cell V). INA260 →
`load_ma` (current), `load_mw` (power). Each presence-gated (`max17Present` / `inaPresent`); a
missing part blanks only its own columns.

**Attribution (free, via Bunshin).** No new identity field: the row's existing `board` tag says
which pod the reading belongs to. The INA260 is portable — profile `fwd`, then move it to `aft` —
and the `board` column follows automatically. A pod without the probe simply blanks `load_*`.

**Per-pod, never merged.** SoC and draw are **excluded from the authority table** — unlike distance
or air, you never pick one pod's battery *over* the other's. The live apps show each connected pod's
power in the `perBoard` health strip; the base-side merge keeps them per-pod (like `board` itself),
not coalesced ([KT-2](#decisions)).

**Warn.** SoC bands to a low-battery [Aizu](../platform/aizu.md) cue — calm (dark/green) while
healthy, **amber** low, **red** critical — on a high priority rung, since a dying battery outranks
most ambient cues. Meaningful only with a LiPo present (the gauge needs a cell); on the USB bank the
cue rests. Banding + hysteresis mirror [Kanki](./kanki.md)'s CO₂ ramp so the pixel doesn't flicker
at a threshold ([KT-3](#decisions)).

**Degradation.** No MAX17048 (USB bank / no cell) → `batt_*` blank, no SoC cue, `load_*` still fill
from the INA260. No INA260 → `load_*` blank, reserve still reported. Neither present → the whole
sense blanks; everything else runs; BLE keeps advertising.

## Contract impact

**Changes both halves.**

- **CSV — append four scalars:** `batt_pct` (%), `batt_v` (V), `load_ma` (mA), `load_mw` (mW), at the
  **end** of the schema (the `steps`/`board`/`pm*`/`nfc_*` append convention). Each blank when its
  gauge is absent. *(Optional 5th, `bus_v`: the INA260's measured rail voltage — worth adding if you
  probe a **boosted 5 V** rail where it differs from `batt_v`; omitted by default for a direct-cell chain.)*
- **BLE — add a `Power` string characteristic** (new UUID): `SOC:78% 3.92V 214mA 1.06W` — labelled
  tokens the consumer splits, the live twin of the columns. A plain UTF-8 string (no binary), so it
  joins `ShintaiGatt.ALL`; `tools/check-contract.py` gains a `"Power" → "POWER"` row in `CHAR_TO_KOTLIN`.
- **Authority table — deliberately NOT extended.** The power columns are the **first channel kept out
  of the Bunshin authority table** (`CONTRACT.md` → Multi-producer model): they are per-pod, surfaced
  side-by-side, never coalesced. Documented there as the interoceptive exception ([KT-2](#decisions)).

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. Two plain-bus sensors, mirroring the presence-gated
pattern of the SCD-40 / BME688 / PMSA path.

1. **Two libraries** — `Adafruit MAX1704X` (`0x36`) and `Adafruit INA260` (`0x40`) — pinned in
   `sketch.yaml`. Both light; no firmware-blob upload (unlike Ōgi's L5CX).
2. **Objects + flags** `max17` / `ina260`, `max17Present` / `inaPresent`, set by a boot `begin()`
   probe; warn-and-continue if absent.
3. **Read** in the telemetry block: `cellPercent()` / `cellVoltage()` and `readCurrent()` /
   `readPower()`; cache for CSV + BLE + the Aizu band.
4. **Aizu band** — a `KatsuryokuBand.h` (mirroring `KankiBand.h`): SoC → colour band with hysteresis,
   posted to Aizu on a new priority rung `AIZU_PRIO_KATSU_BATT` (high — above ambient, near alerts).
5. **Emit** — human (`POWER : 78% 3.92V  214mA 1.06W`), CSV (append the four columns), BLE (`Power`
   char, notify when either gauge has data).
6. **Diagnostics** — `scanI2C` ('I') now expects `0x36` + `0x40`; a live `'V'` command could dump the
   instantaneous draw for bench profiling (optional).

## Acceptance criteria

1. **Both gauges live:** with a LiPo + both breakouts, `batt_pct` / `batt_v` / `load_ma` / `load_mw`
   read plausibly (SoC 0–100 %, draw rises when GPS+fan+BLE are active).
2. **Portable probe:** move the INA260 from the `fwd` chain to `aft`; `load_*` follows the pod (the
   `board` tag attributes it), blank on the pod without the probe.
3. **Per-pod, not merged:** with two pods, the console shows **both** batteries/draws side-by-side;
   neither overrides the other; the authority table is untouched.
4. **Low-battery cue:** as SoC falls past the bands, Aizu goes calm → amber → red with hysteresis; on
   the USB bank (no cell) the cue rests and `batt_*` blank.
5. **Per-part degrade:** INA260-only fills `load_*` with `batt_*` blank; MAX17048-only the reverse;
   neither blocks the other; BLE advertises regardless.
6. **No regression:** contract linter green; firmware compiles; `android/build.sh detekt lint` green;
   telemetry cadence + flash logging unchanged.

## Decisions

- **KT-1 — Portable in-line probe, per-chain (committed).** The INA260 measures the chain it's
  inserted on (fwd / aft / bench), not a fixed whole-system rail — so it profiles one chain at a time
  and moves between them. Attribution rides Bunshin's existing `board` tag; no new field.
- **KT-2 — Per-pod, excluded from the authority table (committed).** Power is the first channel never
  coalesced: you want *both* pods' vitality, never a winner. Surfaced via the `perBoard` model, kept
  per-pod in the base-side merge — the interoceptive exception to Bunshin's arbitration.
- **KT-3 — SoC drives a banded Aizu cue (committed).** State-of-charge → calm/amber/red on a high
  priority rung, with Kanki-style hysteresis; the actionable "charge me" warning. Draw (mA) is
  logged/streamed but does **not** post a cue (it's diagnostic, not a wearer alert).
- **KT-4 — Two gauges, read independently, not fused (committed).** SoC from the MAX17048 ModelGauge,
  draw from the INA260 shunt — reported side by side, not fused into one coulomb-counted estimate.
  Simpler, and each degrades independently.
- **KT-5 — String `Power` char, not binary (committed).** Four scalars fit a labelled string (the
  Accel/GPS/Climate idiom); no need for Metsuke/Ōgi-style binary packing.

## Cross-spec impact

- **Registry:** Katsuryoku earns a [Zōkyō table](../../REGISTRY.md#zōkyō) row; the **MAX17048** and
  **INA260** join the [parts catalog](../../REGISTRY.md#host--infrastructure) (host/infrastructure —
  they measure the platform, not the world).
- **Contract:** `CONTRACT.md` CSV schema (append `batt_pct` / `batt_v` / `load_ma` / `load_mw`) +
  GATT table (`Power` string char) + a **note in the Multi-producer model** that power is per-pod and
  intentionally absent from the authority table.
- **Aizu:** a new priority rung `AIZU_PRIO_KATSU_BATT` for the low-battery cue; no other AZ change.
- **Bunshin:** realizes the `perBoard` battery/health forward path — each pod's SoC + draw in the
  console's per-pod strip.

## Forward path

- **The delivery fix.** With real numbers, choose it: bigger LiPo (if runtime-bound), a stiff
  boost+charger source (if brownout-bound), and/or bulk caps — then confirm with the INA260 that the
  rail holds under a GPS+fan+BLE peak.
- **Power gating.** A load switch on the hogs (PMSA fan, GPS) that Katsuryoku's draw data justifies —
  duty-cycle the biggest consumers when idle; the measured mA says which are worth gating.
- **`bus_v` on a boosted rail.** If the delivery fix adds a 5 V boost, log the INA260 bus voltage to
  watch the rail hold under load.
- **Runtime estimate.** SoC + average `load_ma` → "≈ 47 min left" — a base-side (or on-glass) derived
  readout, the vitality analogue of Hokan's distance-to-empty.
- **Energy-per-session.** Integrate `load_mw` over a capture to cost each sense — "the thermal cam is
  30 % of your budget" — turning Katsuryoku into the platform's own power profiler.
