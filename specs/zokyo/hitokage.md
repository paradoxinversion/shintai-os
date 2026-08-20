# Hitokage (人影) — spec

*A shape in the radar before you'd ever see it.*

**Status:** active (2026-08-20) — **validated on hardware, distance mode as the real default**:
XM125 @ `0x52`, detected/nearest-range end-to-end (serial panel + CSV `presence_detected`/
`presence_distance_mm` + the `Presence` characteristic), plus the full multi-peak list live on the
new `Radar Peaks` characteristic — consistent two-peak detection confirmed on the actual shipped
firmware (all sensors, full BLE). A presence-mode fallback (the original, simpler onboard app)
covers a module still flashed with the older firmware; the two are CONFIRMED mutually exclusive
(HK-4) after a real firmware bug was found and fixed. Both contract halves complete across all seam
sites, Operator's console rendering the peak list as an A-scope. · **Zōkyō:** Hitokage (sibling to
[Fuda](./fuda.md), another new-BOM identity-adjacent sense) · **Seam:** [CONTRACT.md](../../CONTRACT.md)
(**both halves** — 2 CSV columns + 2 BLE characteristics) · **Shares:** none in v1 · **Date:** 2026-08-20

## What this is

Hitokage — "the shadow/silhouette of a person" — is a **forward radar sense**. It adds an
[Acconeer XM125](../../REGISTRY.md#sensors) (SparkFun SEN-24540, Qwiic) — a 60 GHz pulsed
coherent radar module with its own co-packaged STM32 MCU — to the sensor chain. The module runs
**one of two onboard applications**, and this firmware is a register-protocol I²C client to
whichever is flashed, not a raw-signal driver:

- **I2C Distance Detector** (the real default, tried first at boot) reports up to **9 simultaneous
  reflection peaks** — range + relative signal strength each. `presence_detected` /
  `presence_distance_mm` and the `Presence` characteristic report the **nearest** peak; the full
  list streams live on the new **Radar Peaks** characteristic, which Operator renders as an
  A-scope (range/strength along one axis — the sensor has no angle data, so a rotating-sweep PPI
  display would have to fake a direction that doesn't exist).
- **I2C Presence Detector** (a **fallback**, attempted only if distance-mode setup fails) reports
  one detected flag and one range, no peak list — for a module still flashed with the older
  firmware.

Either way, `presence_detected`/`presence_distance_mm`/`Presence` mean the same thing to a
consumer — "something's there, this far away" — regardless of which onboard algorithm produced it.
Neither mode counts or identifies *people*: a peak is a reflector, and two people at the same range
still read as one.

## Why (the case for adding a chip)

Hitokage is a **new-BOM sensor**. Nothing already on the chain does radar-based sensing; the
closest analog, Zanshin's VL53L5CX rear depth field, is optical time-of-flight and answers a
different question (range to any reflecting surface) than what a 60 GHz PCR module measures.

- **Presence-through-obscurants is unreachable from the existing BOM.** Radar's headline property
  — it sees through fabric, thin plastic, drywall in a pinch — has no optical or acoustic analog
  already on the chain.
- **It exercises a new architecture pattern.** Every prior sense is either a raw-register read
  (AS3935, PMSA003I) or a driver over a conventional sensor bus. The XM125 is the platform's first
  **sensor-with-its-own-firmware** — the module's onboard MCU runs Acconeer's detection algorithm,
  and swapping *which* algorithm runs (presence vs. distance) means **reflashing the module**, not
  changing a register value. This firmware handles both, at runtime, by trying one and falling
  back to the other — a real architectural pattern worth having on the platform.
- **Plain bus, no mux.** The XM125 answers at 7-bit `0x52` — a free address (see
  `firmware/i2c-scan`) — and drops onto the STEMMA QT chain like the BME688 or PMSA003I.

## Goals

1. Try the XM125's **distance detector** first at boot; fall back to the **presence detector**
   only if distance-mode setup fails.
2. Resolve `presence_detected` (0/1) and `presence_distance_mm` from whichever mode is active —
   the nearest peak in distance mode, the single reading in presence mode — each telemetry cycle,
   published to CSV + flash log.
3. Notify a **periodic Presence** BLE characteristic (`present=1 dist=260mm` / `present=0
   dist=--`) every telemetry cycle from the same resolved value — like Particulate, not
   event-driven like Fuda/Lightning, since presence is a continuous scalar, not a discrete event.
4. Notify a **periodic Radar Peaks** BLE characteristic (`n=<count> i:mm/strength ...`, up to 9
   peaks) every telemetry cycle **while distance mode is active** — silent in the presence-mode
   fallback, which has no peak-list concept.
5. Render `Presence` on **both** consumers identically (no lean-vs-full split — there's no
   natural "one number that matters" subset the way PM2.5 or a UID is). Render `Radar Peaks` on
   **Operator only**, as an A-scope — no HUD-appropriate rendering for a peak list on Glass.
6. Degrade cleanly: absent XM125 → blank columns, no notify, no crash; old logs without the columns
   still parse (append-at-end, before `board`).
7. Add the sensor to the ground-station capture panel (via `tools/check-contract.py`'s automatic
   CSV-column check) and the Bunshin authority table.

## Non-goals

- **No occupancy counting / multi-person tracking.** Distance mode's 9-peak list is reflectors,
  not identities — two people at the same range still read as one peak, and a strong single
  reflector can produce more than one peak (multipath). Presence mode's single reading has even
  less resolution: one `PRESENCE_DETECTED` bit, one range, no count at all (see
  [Decisions](#decisions)).
- **No on-body cue (v1).** Hitokage does **not** post to [Aizu](../platform/aizu.md). A forward
  presence cue is a natural Aizu addition later, distinct from [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence)'s
  existing rear-ToF reflex.
- **No zone tuning UI.** The configured ranges (500–5000 mm distance mode, 200–2000 mm presence
  fallback) are firmware constants, not a runtime-adjustable register like Enrai's Lightning
  Control.
- **No angle/azimuth, ever.** The XM125 is a single-beam radar in both onboard apps — this isn't a
  v1 scope cut, it's a hardware ceiling. A peak's position on the A-scope is range only.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [Acconeer XM125](../../REGISTRY.md#sensors) | 60 GHz pulsed coherent radar — presence/nearest-range + full multi-peak list over I²C `0x52` | **new BOM** (SparkFun SEN-24540) |

## Decisions

- **HK-1 — a chip, not a reuse.** Radar-based sensing has no analog proxy on the chain; it needs
  a 60 GHz PCR front-end. Hitokage accepts the new-BOM cost for the reason Enmu and Fuda did — the
  sense is otherwise unreachable.
- **HK-2 — nearest-peak semantics, not raw presence-register semantics.** With distance mode as
  the real default, `presence_detected`/`presence_distance_mm` are the nearest of up to 9 peaks,
  not directly a presence-detector register value (that only happens in the fallback). This was a
  deliberate choice ([mode relationship](#hk-9--distance-mode-is-the-real-default)): a consumer
  reading these two fields shouldn't need to know or care which onboard app produced them.
- **HK-3 — periodic, not event-driven.** Unlike Fuda (a discrete tap) or Enrai (an intermittent
  strike), presence is a continuous state — something is either in range or not, every cycle. Both
  `Presence` and `Radar Peaks` notify every telemetry tick (when their mode is active), matching
  Particulate's idiom rather than Fuda/Lightning's.
- **HK-4 — a real mutual-exclusion firmware bug, found and fixed.** An early distance-mode spike
  (reflashing to `i2c_distance_detector.bin`, `SparkFunXM125Distance.getNumberDistances()`/
  `getPeakDistance()`) confirmed the API surface for multiple simultaneous range peaks. Standalone
  (no other sensors, no BLE), it got a clean two-peak read (a wall + a book at a closer range)
  with normal positive signal strengths — the detector genuinely can report two reflectors at once
  given a strong-enough second target; a bare hand next to a wall was likely just too weak a
  reflector, not a threshold or geometry problem. Two blind guesses at the CFAR
  `setThresholdSensitivity` register (900 and 50, vs. default 500) both made even the wall's own
  detection worse — that register's polarity isn't the obvious "higher = more sensitive," and the
  default proved best.

  Wiring the same config into the main firmware as a bench diagnostic channel reproduced a
  consistent `MEASURE_DISTANCE_ERROR` (`rdErr=5`) on every read, regardless of target or range, on
  the full sensor rig — while the identical config stayed clean standalone. A methodical
  elimination search ruled out, one at a time: BLE (both advertising-only and the full stack
  disabled), the rear VL53L5CX ToF field, the MLX90640 thermal cam, and the I2C bus clock speed
  (forced to 100kHz, matching every standalone test — no change). `firmware/i2c-scan` then
  revealed the actual bench rig only had **GPS (0x10) and the XM125 (0x52)** physically wired —
  every other "ruled out" sensor's init code had just been probing empty addresses. Rebuilding as
  an *additive* test (a clean Wire/Serial/BLE/XM125-only baseline, re-adding sensors one at a
  time) confirmed GPS wasn't the cause either.

  **The actual cause, confirmed by direct reproduction**: the presence-mode setup
  (`SparkFunXM125Presence.begin()` + `detectorStart()`) was running unconditionally in every
  failing test, immediately after the diagnostic's own distance-mode setup succeeded. It issues
  presence register-protocol writes against a module that's actually running distance firmware —
  the two onboard apps' register maps overlap in address but not in meaning, and letting both
  library objects touch the same `0x52` device corrupts whichever one is actually configured,
  breaking every subsequent measurement for the rest of that boot. Adding *only* that block back
  onto the clean minimal+GPS baseline reproduced the identical `rdErr=5` instantly. **Fixed**: the
  presence-mode setup is now gated on distance-mode setup having failed — real mutual exclusion,
  not the (incorrect) "the wrong one fails harmlessly" assumption the earliest integration shipped
  with. Verified clean on the actual full shipped firmware (all sensors, full BLE): consistent
  two-peak detection, no errors.
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
- **HK-7 — numeric CSV columns, string BLE peak list, appended before `board`.**
  `presence_detected`/`presence_distance_mm` are **numeric** (unlike Fuda's string
  `nfc_uid`/`nfc_type`), appended at the end of the schema like every prior addition; `Radar
  Peaks`' full list is BLE-live-only, no CSV column, the same relationship Metsuke's Thermal Grid
  has to `thermal_*`. Pre-Hitokage logs still parse.
- **HK-8 — Bunshin precedence fwd → aft.** Both `Presence` and `Radar Peaks` ride the forward pod
  — you face what you're scanning for, the same rationale as Fuda; they share one sensor, so one
  precedence row.
- **HK-9 — distance mode is the real default, presence is a fallback, not a peer.** The module
  runs one onboard app at a time, so "real" distance-mode integration meant deciding how it
  relates to the already-shipped presence mode rather than adding a parallel surface. Distance
  mode's richer data (multi-peak vs. one flag+range) makes it the natural default; presence stays
  supported so a module someone reflashes back to the older firmware still works, without a
  separate contract surface to maintain long-term.
- **HK-10 — CSV gets the summary, BLE gets the full list.** A variable-length up-to-9-peak list
  doesn't fit a fixed CSV schema without either capping peaks arbitrarily (several columns, often
  empty) or open-ended schema churn. Matching Metsuke's Thermal Grid / Zanshin's Rear Depth Grid
  precedent — CSV/flash log gets one simple summary value, BLE gets the full live picture — avoids
  both.

## Forward path

- **On-body confirm cue.** An Aizu cue distinct from Kehai's rear reflex — a forward "something's
  there" pulse.
- **Zone tuning.** A writable control characteristic (Lightning Control's pattern) for live
  start/end range adjustment instead of a firmware constant.
- **Multi-peak person disambiguation.** If a future need arises to distinguish "one big reflector"
  from "two people," it would need a different sensing approach entirely (the hardware has no
  angle data to lean on) — not a v1-scope question, a genuinely open one.
