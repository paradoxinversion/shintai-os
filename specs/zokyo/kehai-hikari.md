# Kehai-Hikari (気配・光) — spec

*The proximity reflex, delivered as light.*

**Status:** spec (unbuilt) · **Zōkyō:** [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense) · **Tsukiwaza:** [Kehai](../../REGISTRY.md#kehai-気配--sensed-presence) · **Seam:** [CONTRACT.md](../../CONTRACT.md)

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

[Kehai](../../REGISTRY.md#kehai-気配--sensed-presence) — "sensed presence," the perception-**out**
touch channel of the Rokkan Zōkyō — is listed *planned*, blocked on a **DRV2605 haptic driver
+ motor we do not own**. Kehai-Hikari unblocks it with a part already soldered to the host:
the **QT Py ESP32-S3's onboard NeoPixel**. It turns `distance_mm` / `alert` into a coloured,
pulsing proximity light — the same reflex Kehai was always meant to be, rendered as *light*
instead of *vibration*.

This is **not a new Zōkyō and not a new sense.** It is the first *buildable* implementation of
the existing Kehai Tsukiwaza. The seam does not move: Kehai reacts to `distance_mm` and the
edge-triggered `alert` (already in [CONTRACT.md](../../CONTRACT.md)), and Kehai-Hikari reads exactly
those. When the DRV2605 arrives, the haptic path drops in **behind the same feedback call** —
light-first, haptic-later, or both at once. See [Forward path](#forward-path).

## Why (the thrifty case)

- **Zero new BOM.** The NeoPixel is on the board; the ToF is already read every loop.
- **Unblocks a `planned` module tonight** instead of waiting on a parts order.
- **Works with no phone.** The reflex is a *local* loop on the host — it fires whether or not a
  BLE central is connected, unlike the `alert` characteristic which only notifies a subscriber.
- **De-risks the haptic.** We prove the band logic, cadence, and seam in software now; the
  DRV2605 becomes a feedback-sink swap, not a redesign.

## Goals

1. Drive the onboard NeoPixel from ToF proximity: a calm→alarm gradient the wearer reads
   peripherally, without looking at a screen.
2. React fast enough to feel like a *reflex* (low latency), decoupled from the 1500 ms telemetry
   cadence.
3. Run entirely on-host, independent of BLE connection and of the `h`/`c`/`b` serial output mode.
4. Behave on battery: bounded idle current, capped brightness.
5. Route all feedback through the **shared feedback arbiter** ([Aizu](../platform/aizu.md)):
   Kehai *posts* an Alert-class cue, it does not own the pixel — so it coexists with other sources
   (e.g. Kanki) and a DRV2605 haptic drops in behind the same bus without touching sensing logic.

## Non-goals

- **No contract change.** Kehai-Hikari is output-only, derived from existing `distance_mm` /
  `alert`. It adds **no** CSV column and **no** GATT characteristic. ([Contract impact](#contract-impact).)
- **Not the haptic.** The DRV2605 path is future Kehai work, reserved behind the seam here.
- **No sensor fusion.** "Hot *and* close" (thermal × ToF) is a distinct future idea, explicitly
  out of scope — Kehai is proximity only.
- **No animation framework.** A single pixel, a few states, a pulse. Nothing more.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [VL53L4CX](../../REGISTRY.md#sensors) ToF | proximity input (`distance_mm`, `alert`) | already read in `loop()` |
| **QT Py ESP32-S3 onboard NeoPixel** | the feedback sink (this spec) | on the [host board](../../REGISTRY.md#host--infrastructure) |
| BOOT button (GPIO0) | *optional* mute/toggle | on the host board |

New firmware dependency: **`Adafruit_NeoPixel`** (single pixel). Use the board's `PIN_NEOPIXEL`
and `NEOPIXEL_POWER` macros — **do not hardcode GPIOs**; the S3 requires driving `NEOPIXEL_POWER`
HIGH to power the pixel at all.

## Behaviour — the distance→light mapping

Four bands, keyed on `mm` (the ToF reading; `mm <= 0` means *no target*) against the existing
constants `NEAR_MM` (200) and `FAR_MM` (2000 — **currently defined but unused; this gives it a job**).

| Band | Condition | Colour | Motion |
|------|-----------|--------|--------|
| **Idle** | `mm <= 0` (no target) | **dim green breathe** when tethered · **off** on battery/field ([D-1](#decisions)) | slow breathe (~2–4 s) |
| **Clear** | `mm > FAR_MM` | dim green | steady |
| **Approach** | `NEAR_MM < mm <= FAR_MM` | amber | pulse; **rate scales with closeness** (parking-sensor metaphor: closer → faster) |
| **Reflex** | `0 < mm <= NEAR_MM` | red | fast pulse or solid — the Kehai "too close" reflex; mirrors `alert == 1` |

- **Perceptual pulse ([D-3](#decisions)).** In Approach, map `mm` to a pulse period with a
  **nonlinear curve that shortens the period sharply near `NEAR_MM`** — human rate perception is
  ratio-based and the actionable zone is the last half-metre, so resolution belongs there, not in
  the far distances. Concrete default (tune at build): normalise `f = (mm - NEAR_MM) / (FAR_MM - NEAR_MM)`
  clamped to `[0,1]`, then `period = PERIOD_MIN + (PERIOD_MAX - PERIOD_MIN) * f^2` — e.g.
  `PERIOD_MAX ≈ 1200 ms` (far edge) easing down to `PERIOD_MIN ≈ 120 ms` near `NEAR_MM`. Below
  `PERIOD_MIN` the pulse reads as effectively solid — which is the Reflex band's alarm.
- **Brightness cap.** A single `KEHAI_MAX_BRIGHT` const (start ~40/255) governs peak output —
  eye comfort and battery. Idle is off or minimal.
- **Alert coherence.** The Reflex band must correspond to `alert == 1` so the light and the BLE
  `alert` characteristic never disagree.

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`. Constraints below are load-bearing.

1. **Decoupled reflex tick.** The reflex must not inherit the 1500 ms `UPDATE_MS` cadence. Add a
   separate fast timer (`REFLEX_MS`, ~100–150 ms). On each tick: service ToF, update the LED.
2. **Single ToF read site.** Today ToF is read *inside* the `UPDATE_MS` block and self-restarts
   via `VL53L4CX_ClearInterruptAndStartMeasurement()`. Two consumers would fight over `dataReady`.
   **Move the ToF read into the reflex tick, cache the latest `mm`, and have the 1500 ms telemetry
   block consume the cached value** for CSV / BLE. Net effect: snappier reflex *and* unchanged
   telemetry semantics. **Lower the VL53L4CX timing budget ([D-4](#decisions))** so a fresh range
   lands close to the reflex-tick period — the current slow cadence was an early-testing artifact,
   not a requirement. The LED updates every tick off the most recent range, holding the last value
   between fresh samples.
3. **The seam — post to Aizu, don't own the pixel.** Kehai computes its band + requested motion
   and **posts an Alert-class cue** to the shared feedback arbiter — `postCue(KEHAI, ALERT, colour, motion)`
   — defined in [Aizu § the cue](../platform/aizu.md#the-cue). The arbiter
   is the sole writer of the NeoPixel; Kehai never names the pixel directly, and the future DRV2605 is
   another sink *behind* the arbiter, not a change to Kehai. *(Supersedes the earlier
   `driveReflex()`-owns-the-LED framing — see [Kanki, cross-spec impact](./kanki.md#cross-spec-impact).)*
4. **NeoPixel power & idle — owned by the arbiter ([D-1](#decisions)).** The arbiter (Aizu), as
   sole pixel writer, drives `NEOPIXEL_POWER` HIGH in `setup()` and renders Idle when no cue is
   active, keyed off the **tethered vs field** signal the flash-logger already uses (`!Serial` — a
   USB battery bank presents no CDC host, so it reads as field): **tethered → hold power, dim green
   breathe**; **field/battery → LED off** (write colour 0; optionally cut `NEOPIXEL_POWER` if
   measured idle draw justifies it). D-1 is thus an *arbiter-level* policy Kehai and Kanki both
   inherit, not a Kehai-private behaviour.
5. **Independent of everything.** The reflex runs regardless of `deviceConnected` (no phone
   needed) and regardless of `outputMode` (`h`/`c`/`b`). It must not print to `Serial` on the
   telemetry stream (no CSV/human-line pollution).
6. **Don't break untethered logging.** Flash logging is gated on `!Serial`; the reflex tick must
   not touch that gate, the log cadence, or `lastUpdate`. Kehai-Hikari is *most* useful untethered
   (walking around on battery) — verify it coexists with field logging.
7. **Degradation.** ToF is not `*Present`-gated today; if it returns no data, `mm <= 0` → Idle.
   Firmware still boots and all other sensors are unaffected.
8. **BOOT mute — deferred ([D-2](#decisions)).** Not in v1. The GPIO0 mute/toggle rolls into the
   shared feedback/input layer that later Tsukiwaza will share (see [Forward path](#forward-path)).

## Contract impact

**None.** No new CSV column, no new GATT characteristic, no cadence change to the 1500 ms
telemetry row. [CONTRACT.md](../../CONTRACT.md) is untouched. Kehai-Hikari derives entirely from the
already-published `distance_mm` and `alert`. (A future "reflex state" field is conceivable but is
explicitly *not* part of this spec.)

## Acceptance criteria

1. **No target:** with nothing in ToF range, the LED idles per D-1 — dim green breathe when
   tethered, off on battery/field.
2. **Approach:** an object at ~1.5 m shows amber, pulsing slowly; walking it in visibly speeds the
   pulse, and the *rate change is steepest in the last half-metre* (the perceptual curve, D-3).
3. **Reflex:** an object crossing inside `NEAR_MM` (200 mm) shows red within **≤ 150 ms** (with the
   lowered timing budget, D-4), and this coincides with `alert == 1`.
4. **No central:** criteria 1–3 all hold with **no BLE device connected**.
5. **Untethered:** criteria 1–3 hold on battery power, and field flash-logging still writes rows
   as before (untethered `!Serial` path intact).
6. **No telemetry regression:** the CSV header, column order, ~1500 ms row cadence, and BLE
   notify behaviour are unchanged; no stray reflex output on the serial telemetry stream.
7. **Battery bound:** idle draw is bounded (LED off/minimal at rest); peak brightness respects
   `KEHAI_MAX_BRIGHT`.
8. **Seam:** Kehai emits feedback only by posting cues to the arbiter; sensing code contains no
   direct NeoPixel calls. The arbiter is the sole pixel writer, and a DRV2605 could be added as
   another sink behind it.

## Decisions

All four opening questions are resolved; recorded here as the build contract.

- **D-1 — Idle state:** dim green **breathe** when **tethered**, **off** on **battery/field**.
  Battery vs tethered is read from `!Serial` (the same untethered proxy the flash-logger uses —
  a USB power bank presents no CDC host, so it counts as field). Hold `NEOPIXEL_POWER` when
  breathing; write colour 0 when off (cutting the power pin is optional, only if idle draw warrants).
  *Now an arbiter/[Aizu](../platform/aizu.md#idle)-level idle policy shared by all cue
  sources, not Kehai-private.*
- **D-2 — BOOT mute:** **deferred** — not in v1. Folds into the shared feedback/input layer with
  the other remixes (see [Forward path](#forward-path)).
- **D-3 — Pulse curve:** **perceptual, not linear.** Human rate perception is ratio-based and the
  actionable zone is the last half-metre (reaction time ~200–250 ms), so the period must shorten
  *sharply* near `NEAR_MM` and waste no resolution on far distances the wearer can't discriminate.
  Default `period = PERIOD_MIN + (PERIOD_MAX - PERIOD_MIN) * f²`, `f = clamp((mm-NEAR_MM)/(FAR_MM-NEAR_MM), 0, 1)`,
  `PERIOD_MAX ≈ 1200 ms → PERIOD_MIN ≈ 120 ms`. Tune the exponent/endpoints on-wrist.
- **D-4 — Timing budget:** **lower the VL53L4CX timing budget** to favour reflex latency; the
  1500 ms cadence was an early-testing artifact. Target reflex latency **≤ 150 ms** (AC-3). Telemetry
  keeps consuming the cached range at its own 1500 ms cadence, so this doesn't touch the contract.

## Forward path

- **Kehai proper (haptic):** implement `driveReflex()` for the DRV2605 + LRA/ERM. Light and
  vibration can then run together or independently. No sensing change.
- **Aizu (the shared feedback bus):** the `driveReflex()`→`postCue()` generalization is now
  committed (via [Kanki](./kanki.md)) — the arbiter that other remixes (Nesshi's hot/cold cue, a
  NeoPixel compass) and the DRV2605 haptic all post to. Wants its own spec and a registry entry as a
  shared host capability. The BOOT-button mute (D-2) folds in here.
- **Registry:** on build, update the [Kehai row](../../REGISTRY.md#kehai-気配--sensed-presence) from
  *planned* to reflect the light-first path, linking this spec.
