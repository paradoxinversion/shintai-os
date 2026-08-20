# Hitokage (人影) — spec

*A shape in the radar before you'd ever see it.*

**Status:** active (2026-08-20) — **validated on hardware**: XM125 @ `0x52`, presence detected/range end-to-end (serial panel + CSV `presence_detected`/`presence_distance_mm` + BLE). A distance-detector investigation (HK-4) found and **fixed a real firmware bug**: the presence-mode and distance-mode onboard-app clients were both touching the module unconditionally, corrupting whichever one was actually running — now mutually exclusive, verified clean on the full shipped firmware with reliable two-peak detection. Presence-mode plumbing complete across all seam sites; a bench-only diagnostic characteristic for the distance-mode investigation remains in `shintai-os.ino` ("DIAGNOSTIC TEMP" tagged, not in `CONTRACT.md`). · **Zōkyō:** Hitokage (sibling to [Fuda](./fuda.md), another new-BOM identity-adjacent sense) · **Seam:** [CONTRACT.md](../../CONTRACT.md) (**both halves** — 2 CSV columns + 1 BLE characteristic) · **Shares:** none in v1 · **Date:** 2026-08-20

## What this is

Hitokage — "the shadow/silhouette of a person" — is a **forward presence sense**. It adds an
[Acconeer XM125](../../REGISTRY.md#sensors) (SparkFun SEN-24540, Qwiic) — a 60 GHz pulsed
coherent radar module with its own co-packaged STM32 MCU — to the sensor chain and publishes its
onboard **I2C Presence Detector** result: a detected/not-detected flag and, when detected, a
range in the configured 200–2000 mm zone. Both consumers render the same readout — there's no
raw-vs-derived split to lean out of, unlike PM2.5 or a tag UID.

Unlike every other sense on the platform, the XM125 does its own DSP on-module. The firmware here
is a **register-protocol I²C client**, not a raw-signal driver: `presence_detected` and
`presence_distance_mm` are simply that register's result, already resolved by the sensor's own
detection algorithm. There is no lower-level signal to read differently later — the module's
onboard firmware *is* the sense.

## Why (the case for adding a chip)

Hitokage is a **new-BOM sensor**. Nothing already on the chain does radar-based presence
detection; the closest analog, Zanshin's VL53L5CX rear depth field, is optical time-of-flight and
answers a different question (range to any reflecting surface) than "is a person moving in this
zone" (which the XM125's presence algorithm specifically models via inter/intra-frame motion
scoring, not raw range).

- **Presence-through-obscurants is unreachable from the existing BOM.** Radar's headline property
  — it sees through fabric, thin plastic, drywall in a pinch — has no optical or acoustic analog
  already on the chain.
- **It exercises a new architecture pattern.** Every prior sense is either a raw-register read
  (AS3935, PMSA003I) or a driver over a conventional sensor bus. The XM125 is the platform's first
  **sensor-with-its-own-firmware** — the module's onboard MCU runs Acconeer's detection algorithm,
  and swapping *which* algorithm runs (presence vs. distance) means **reflashing the module**, not
  changing a register value. That's a real architectural distinction worth having on the platform.
- **Plain bus, no mux.** The XM125 answers at 7-bit `0x52` — a free address (see
  `firmware/i2c-scan`) — and drops onto the STEMMA QT chain like the BME688 or PMSA003I.

## Goals

1. Read the XM125's presence-detector result (`busyWait()` + the Presence Distance register) each
   telemetry cycle and publish it as `presence_detected` (0/1) and `presence_distance_mm` — CSV +
   flash log.
2. Notify a **periodic Presence** BLE characteristic (`present=1 dist=260mm` / `present=0
   dist=--`) every telemetry cycle — like Particulate, not event-driven like Fuda/Lightning, since
   presence is a continuous scalar, not a discrete event.
3. Render it on **both** consumers, identically — the raw payload, no lean-vs-full split (there's
   no natural "one number that matters" subset the way PM2.5 or a UID is).
4. Degrade cleanly: absent XM125 → blank columns, no notify, no crash; old logs without the columns
   still parse (append-at-end, before `board`).
5. Add the sensor to the ground-station capture panel (via `tools/check-contract.py`'s automatic
   CSV-column check) and the Bunshin authority table.

## Non-goals

- **No occupancy counting / multi-person tracking (v1).** The I2C Presence Detector's register map
  has exactly one `Presence Result` bit and one `Presence Distance` value — no count, no per-target
  list. Two people moving in the zone read as one signal, not two (see [Decisions](#decisions)).
- **No distance-mode integration (v1).** The onboard **I2C Distance Detector** application (a
  separate firmware image, reporting up to 9 simultaneous reflection peaks + strength) was spiked
  live to answer "can it see multiple entities" — it can report multiple *ranges*, not multiple
  *people* — but is not wired into `shintai-os.ino`. Switching modes means physically reflashing
  the module over its own USB-C bootloader; the shipped firmware targets presence mode only.
- **No on-body cue (v1).** Hitokage does **not** post to [Aizu](../platform/aizu.md). A forward
  presence cue is a natural Aizu addition later, distinct from [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence)'s
  existing rear-ToF reflex.
- **No zone tuning UI.** The 200–2000 mm range is a firmware constant (mirroring the bench
  bring-up), not a runtime-adjustable register like Enrai's Lightning Control.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [Acconeer XM125](../../REGISTRY.md#sensors) | 60 GHz pulsed coherent radar — presence detected + range over I²C `0x52` | **new BOM** (SparkFun SEN-24540) |

## Decisions

- **HK-1 — a chip, not a reuse.** Radar-based presence has no analog proxy on the chain; it needs
  a 60 GHz PCR front-end. Hitokage accepts the new-BOM cost for the reason Enmu and Fuda did — the
  sense is otherwise unreachable.
- **HK-2 — single-target, measured not assumed.** The presence detector's register map (`Presence
  Result` @ 0x0010: one `PRESENCE_DETECTED` bit + sticky bit; `Presence Distance` @ 0x0011: one
  uint32 mm value) has no count field. This was confirmed against the Acconeer I2C Presence
  Detector User Guide before writing the CSV/BLE schema — `presence_detected`/`presence_distance_mm`
  intentionally mirror what the register actually reports, not an aspirational "how many people."
- **HK-3 — periodic, not event-driven.** Unlike Fuda (a discrete tap) or Enrai (an intermittent
  strike), presence is a continuous state — something is either in the zone or not, every cycle.
  The Presence characteristic notifies every telemetry tick, matching Particulate's idiom.
- **HK-4 — the distance-detector spike is resolved, and found a real firmware bug in the process.**
  A live test (reflashing to `i2c_distance_detector.bin`,
  `SparkFunXM125Distance.getNumberDistances()`/`getPeakDistance()`) confirmed the API surface for
  multiple simultaneous range peaks. Standalone (no other sensors, no BLE), it got a clean two-peak
  read (a wall + a book at a closer range) with normal positive signal strengths — the detector
  genuinely can report two reflectors at once given a strong-enough second target; a bare hand next
  to a wall was likely just too weak a reflector, not a threshold or geometry problem. Two blind
  guesses at the CFAR `setThresholdSensitivity` register (900 and 50, vs. default 500) both made
  even the wall's own detection worse — that register's polarity isn't the obvious "higher = more
  sensitive," and the default proved best.

  Wiring the same config into the main firmware as a temporary diagnostic channel
  (`PRESENCE_DIAG`/`abcdd1a6-...`, `hitokageDistDiag`) reproduced a consistent
  `MEASURE_DISTANCE_ERROR` (`rdErr=5`) on every read, regardless of target or range, on the full
  sensor rig — while the identical config stayed clean standalone. A methodical elimination search
  ruled out, one at a time: BLE (both advertising-only and the full stack disabled), the rear
  VL53L5CX ToF field, the MLX90640 thermal cam, and the I2C bus clock speed (forced to 100kHz,
  matching every standalone test — no change). `firmware/i2c-scan` then revealed the actual bench
  rig only had **GPS (0x10) and the XM125 (0x52)** physically wired — every other "ruled out"
  sensor's init code had just been probing empty addresses. Rebuilding as an *additive* test (a
  clean Wire/Serial/BLE/XM125-only baseline, re-adding sensors one at a time) confirmed GPS wasn't
  the cause either — both alone and combined with the diagnostic, distance mode stayed clean.

  **The actual cause, confirmed by direct reproduction**: `hitokagePresent`'s presence-mode setup
  (`SparkFunXM125Presence.begin()` + `detectorStart()`) was running unconditionally in every failing
  test, immediately after the diagnostic's own distance-mode setup succeeded. It issues presence
  register-protocol writes against a module that's actually running distance firmware — the two
  onboard apps' register maps overlap in address but not in meaning, and letting both library
  objects touch the same `0x52` device corrupts whichever one is actually configured, breaking every
  subsequent measurement for the rest of that boot. Adding *only* that block back onto the clean
  minimal+GPS baseline reproduced the identical `rdErr=5` instantly. **Fixed**: the presence-mode
  setup is now gated on `!hitokageDiagPresent` — mutual exclusion between the two onboard-app
  clients, not merely "the wrong one fails harmlessly," which was the (incorrect) assumption the
  original integration shipped with. Verified clean afterward in the actual full shipped firmware
  (all sensors, full BLE): consistent two-peak detection, no errors.

  Multi-peak-vs-person-count is answered too (weak reflectors like a hand don't reliably produce a
  second peak; a book does) — the finding that took most of the investigation was the mutual-
  exclusion bug, not the sensing question. If distance mode is wired in for real, the fix above is
  the thing to carry forward; the peak-count question no longer needs re-litigating.
- **HK-5 — reflashing needs the XM125's own USB-C, not the QT Py's.** The module's UART bootloader
  (BOOT+RST buttons) is only reachable over its own USB-serial bridge; the Qwiic I²C link to the
  QT Py carries no bootloader path. Flashed with `acconeer-exptool`
  (`python -m acconeer.exptool.flash flash XM125 --port <port> --image <bin>`) — firmware itself
  requires an Acconeer Developer Account to download.
- **HK-6 — FATAL loops repeat, not print-once.** Bring-up sketches that halt on a fatal error
  (`begin()`/`detectorStart()` failure) print the message in a `while(1)` loop, not once before
  halting — a terminal that attaches after native-USB re-enum (>1.5 s) would otherwise see nothing,
  ever. Bit the bring-up live; fixed in both `firmware/xm125-bringup/` and
  `firmware/xm125-bringup-distance/`.
- **HK-7 — numeric columns, appended before `board`.** `presence_detected`/`presence_distance_mm`
  are **numeric** (unlike Fuda's string `nfc_uid`/`nfc_type`), appended at the end of the schema
  like every prior addition. Pre-Hitokage logs still parse.
- **HK-8 — Bunshin precedence fwd → aft.** Presence rides the forward pod — you face what you're
  scanning for, the same rationale as Fuda.

## Forward path

- **Distance-mode integration.** HK-4's mutual-exclusion bug is fixed and verified clean on the full
  shipped firmware, and multi-peak reflector behavior is understood — nothing blocking left to wire
  the I2C Distance Detector application in as a real build/runtime mode alongside (or instead of)
  presence, beyond the CONTRACT.md/CSV/BLE/Android integration work itself (the `PRESENCE_DIAG`
  channel, `abcdd1a6-...`, is a bench-only diagnostic, not that integration).
- **On-body confirm cue.** An Aizu cue distinct from Kehai's rear reflex — a forward "something's
  there" pulse.
- **Zone tuning.** A writable control characteristic (Lightning Control's pattern) for live
  start/end range adjustment instead of a firmware constant.
