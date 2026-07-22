# Enrai (遠雷) — spec

*Distant thunder: sensing a storm's approach and intensity from the electromagnetic
signature of each strike — the first sense that fires on an **event**, not a sample.*

**Status:** built (2026-07-22) · **Zōkyō:** Enrai · **Seam:** [CONTRACT.md](../../CONTRACT.md) — **new CSV columns + a new string characteristic** · **Shares:** the main I²C bus (no mux), [Aizu](../platform/aizu.md)-free (no on-body cue in v1) · **Date:** 2026-07-22

> Built on `research-development-jūshichi`. Brought up live on an **overhead lightning storm** —
> the bench sketch (`firmware/as3935-bringup/`) caught real strikes before the sensor was ever
> wired into the contract. All solderless — Qwiic.

## What this is

Enrai — 遠雷, *distant thunder* — adds an **[AS3935](../../REGISTRY.md#sensors)** "Franklin"
lightning detector to the platform: a sense for **lightning strikes**, each reported with an
estimated **distance** (overhead … out of range) and a relative **energy**. It sits **direct on
the main I²C bus at `0x03`** (no PCA9546 mux — the address is free and collides with nothing),
presence-gated and non-fatal like every other sensor.

The AS3935 is fundamentally different from the platform's other sensors: it is **event-based**.
Distance, thermal, air chemistry — all are *sampled* continuously and every CSV row is a fresh
reading. Lightning **isn't sampled**: a strike either happens or it doesn't, seconds or minutes
apart. So Enrai reshapes the event stream into the contract's sampled world by logging a
**last-strike snapshot** (`lightning_km`, `lightning_energy`) plus a **monotonic count**
(`lightning_strikes`) — each CSV row reports the most recent strike and the running total, not an
instantaneous value. The live edge is preserved on the BLE side: a **new string Lightning
characteristic** notifies **event-driven**, once per validated strike, so the apps can flash the
instant a bolt lands.

No IRQ pin is wired (the SparkFun board breaks one out, but the bench rig is Qwiic-only), so the
firmware **polls** the sensor's interrupt-source register every `loop()` iteration — reading it
clears the latch, and lightning never comes faster than a ~100 Hz poll.

## Why

- **A genuinely new sense.** Every prior Zōkyō sharpened *proximity, sight, motion, air*. Lightning
  is none of those — it's a **storm sense**, a heads-up that weather is turning violent, readable
  at a glance and (forward path) as an ambient cue.
- **The event model is the interesting part.** Folding an intermittent event into a sampled CSV +
  an event-driven BLE notify is the first time the contract carries something that isn't a periodic
  reading — a pattern future event senses (a doorbell, a Geiger tick) can reuse.
- **Thrifty and additive.** One Qwiic sensor on a free address; no mux, no new hardware surface.
  Every existing channel is untouched — the three CSV columns append before `board`, the new
  characteristic is purely additive.

## Goals

1. Drive one AS3935 on the main bus at `0x03`, presence-gated / non-fatal, **polled** (no IRQ).
2. Log a **last-strike snapshot + cumulative count** to the CSV (`lightning_km`,
   `lightning_energy`, `lightning_strikes`), appended **before `board`**.
3. Notify a **new string Lightning characteristic** (`km=<d> e=<energy> n=<count>`) **event-driven**,
   once per validated strike → a full readout + strike-flash in Operator, a lean nearest-km flash in Glass.
4. No mux; reject man-made disturbers well enough to catch real strikes indoors.

## Non-goals

- **No on-body Aizu cue in v1.** The strike surfaces on the phone/glasses, not (yet) as a NeoPixel
  colour or haptic — that's the [Forward path](#forward-path).
- **No IRQ-driven capture in v1.** Polling is enough for lightning's cadence; wiring the INT pin is
  a forward-path precision upgrade, not a requirement.
- **No storm tracking / approach vector.** v1 reports the last strike + a count, not a rate,
  bearing, or approach/recede trend.

## Parts (Tsukiwaza)

- **AS3935** — AMS "Franklin" lightning detector (I²C `0x03`, SparkFun Qwiic board). Detects a
  strike's characteristic sferic, estimates distance (`1` km = overhead … ~40 km, `63` =
  out of range) and a relative energy. Direct on the main bus — **no mux** (`0x03` is unused).
  Factory-tuned antenna; INDOOR gain + watchdog 3 reject the indoor disturber floor.

## Behaviour — poll, snapshot, notify

`serviceEnrai()` (the sole AS3935 read site, called every `loop()` iteration) polls the
interrupt-source register (`readInterruptReg()`):

- **Nothing / disturber / noise** → return (disturbers and noise are ignored here; the watchdog=3
  config thins man-made noise, and a disturber never gates a real strike since masking is off).
- **A validated strike** (`LIGHTNING`, `0x08`) → read `distanceToStorm()` into `lightning_km` and
  `lightningEnergy()` into `lightning_energy`, bump `lightning_strikes`, and — while a central is
  connected — **notify the Lightning characteristic immediately** with `km=<d> e=<energy> n=<count>`.

The CSV row logger reads the snapshot at its own ~1.5 s cadence; blank in every column when the
AS3935 is absent.

## Contract impact

- **New CSV columns** — `lightning_km` / `lightning_energy` / `lightning_strikes`, appended
  **before `board`** (which stays the terminal Bunshin discriminator), so consumers that key on
  column *names* are unaffected and pre-Enrai logs still parse. Documented in
  [CONTRACT.md](../../CONTRACT.md#csv--serial-schema); firmware `CSV_HEADER` mirrors it (order-exact).
- **New string characteristic** — **Lightning** (`abcda535…`, `"a535" ≈ AS3935`), event-driven.
  Mirror sites moved in lock-step (firmware `#define`, `:core` `ShintaiGatt`, the linter's
  `CHAR_TO_KOTLIN`); `tools/check-contract.py` green (30 columns, 11 UUIDs).
- **Bunshin** — the `lightning_*` + Lightning row joins the authority table (**aft → fwd**: ambient
  storm sense rides the pack with air chem; it's direction-agnostic).

## Firmware integration

- `shintai-os.ino` — `SparkFun_AS3935 enrai(0x03)`; `initEnrai` inline in `setup()` (presence-gate
  at `0x03`, INDOOR gain, noise 2, watchdog 3, disturbers unmasked); `serviceEnrai()` (sole read,
  called in the `loop()` fast-service cluster); the new `lightningChar` (via `makeChar`); three CSV
  cells; `'I'`-scan expected-device list updated (`lightning=0x03`).
- `sketch.yaml` — pins `SparkFun AS3935 Lightning Detector Arduino Library (1.4.9)`.
- Bench precursor kept: `firmware/as3935-bringup/` (standalone strike console with live tuning) +
  `tools/lightning-logger.py` (serial → `groundstation/logs/lightning-<date>.csv`), the rig that
  caught the storm before integration.

## Consumer / app integration

- `:core` — `ShintaiGatt.LIGHTNING` (added to `ALL`, so Operator subscribes automatically), a
  `LightningState` parse (`km`/`energy`/`strikes` + a `distanceLabel`), folded into
  `ShintaiReadings.lightning`; `Merge.kt` carries it on a new **Lightning** channel (aft → fwd).
- **Operator** renders the full Enrai readout (distance · energy · count) in a **Lightning panel**
  whose LED **flashes** on each new strike.
- **Glass** surfaces it lean: a **`LightningBadge`** — the nearest-strike distance, shown only once
  a strike has landed, brightened to full for a beat on each new bolt. The energy/count stay on the phone.

## Acceptance criteria

1. With the AS3935 present, a real (or test) strike sets `lightning_km`/`lightning_energy`, bumps
   `lightning_strikes`, and notifies the Lightning characteristic within one poll.
2. Both apps surface a strike live while subscribed (Operator full readout + flash; Glass nearest-km
   badge + flash); absent sensor → no misleading panel.
3. Firmware compiles (`firmware/verify.sh`); host Android (`android/build.sh detekt lint`) +
   `tools/check-contract.py` all green.

## Decisions

- **D-1 — Direct on the bus, no mux.** `0x03` is free and collides with nothing; the AS3935 is a
  single sensor with a fixed address, so the PCA9546 buys nothing here (unlike the colliding ToFs).
- **D-2 — Poll, don't wait for an IRQ.** No INT pin is wired on the Qwiic rig; polling the
  interrupt-source register catches lightning's slow cadence with margin. Wiring INT is a
  forward-path precision upgrade. **The poll is THROTTLED to ≥10 ms** (`ENRAI_POLL_MS`): the
  AS3935's interrupt register needs ~2 ms to settle after an event, and polling every loop
  iteration (sub-ms bursts) read-and-clears it inside that window and loses strikes — the bench
  sketch's `delay(10)` never hit this and caught strikes reliably, so the integrated read matches
  it. A `'K'` serial command reports live strike/disturber/noise tallies to verify the catch rate.
- **D-3 — Last-strike snapshot + count in the CSV.** Lightning is event-based; a sampled CSV can't
  hold an "instant." The snapshot + monotonic count is the honest sampled projection, and the
  event edge lives on the BLE side.
- **D-4 — New string char, event-driven.** The strike is a discrete event, so the characteristic
  notifies once per strike (not on a periodic tick) — letting the apps flash on the edge.
- **D-5 — INDOOR gain + watchdog 3.** The bench config that actually caught the overhead storm:
  maximum sensitivity, disturbers unmasked (so a real strike is never gated behind one), watchdog
  raised to thin the indoor man-made noise floor.

## Forward path

- **Aizu cue.** Map strike distance to a NeoPixel colour / haptic — a storm-overhead heads-up you
  *feel*, closing the loop like Kehai and Kyūkaku.
- **IRQ-driven capture.** Wire the AS3935 INT pin to a free GPIO for interrupt-precise timing and a
  lower duty cycle than polling.
- **Storm tracking.** A ring buffer of recent strikes → an approach/recede trend and a strike rate,
  the richer "is it getting closer?" the single snapshot can't express.
- **Outdoor mode.** Expose the INDOOR/OUTDOOR gain switch (the bench sketch already toggles it live)
  so the wearer can trade sensitivity for fewer false trips in a noisy field.
