# Aizu (合図) — spec

*The cue. One shared on-body output bus: sources post, the arbiter renders.*

**Status:** built (2026-07-02) · **Kind:** shared host capability (not a Zōkyō) · **Seam:** [CONTRACT.md](../../CONTRACT.md) (no change) · **Used by:** [Kehai-Hikari](../zokyo/kehai-hikari.md), [Kanki](../zokyo/kanki.md), [Nesshi](../zokyo/nesshi.md), [Hokan](../zokyo/hokan.md), [Kyūkaku](../zokyo/kyukaku.md), [Kiatsu](../zokyo/kiatsu.md), future remixes · **Date:** 2026-07-01

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Aizu — "signal / cue" — is the **shared feedback layer** the [Kehai-Hikari](../zokyo/kehai-hikari.md) and
[Kanki](../zokyo/kanki.md) specs both delegate to. The QT Py has exactly **one** onboard RGB NeoPixel, and
more than one module wants to speak on it. Aizu is the small arbiter that makes that safe: modules
**post cues**; Aizu decides which cue wins and **renders** it on the pixel (and later fans the same
cue out to a DRV2605 haptic). No module touches the pixel directly.

It is **not a Zōkyō and not a Tsukiwaza of one**. Like the Python ground-station (`groundstation/`)
is shared base-side tooling docked to by every Zōkyō, Aizu is shared **on-body output
infrastructure** every Zōkyō with local feedback draws on. It has no sense of its own — it only
expresses what sources hand it.

**Why it exists:** writing [Kanki](../zokyo/kanki.md) second put two claimants on one LED and proved the
Kehai `driveReflex()`-owns-the-pixel framing wrong. KD-1 committed this arbiter and its name; this
spec is its home — the interface, the arbitration, the rendering, idle, input, and the haptic
fan-out that were previously smeared across the two feature specs.

## Goals

1. Let any number of sources share the one NeoPixel without clobbering each other — **highest-
   priority active cue wins**, everything else waits.
2. Be the **sole writer** of the NeoPixel and the sole owner of its power pin, brightness, and
   animation clock.
3. Separate *intent* (a source posts colour + motion + priority) from *rendering* (Aizu animates),
   so sources never block or busy-loop on the LED.
4. Own the shared policies that were leaking into feature specs: **idle** (Kehai-Hikari D-1), the
   **all-clear heartbeat** (Kanki KD-2), brightness cap, and arbitration anti-flicker.
5. Be **modality-agnostic**: adding the DRV2605 haptic later means registering a second output
   sink, not touching any source or the arbiter core.
6. Own the **input** side too: the deferred BOOT-button mute (Kehai-Hikari D-2).
7. Make **adding a new source** a one-row change (a priority-table entry + a `postCue` call).

## Non-goals

- **No contract change.** Aizu expresses signals already in the firmware; it adds no CSV column and
  no GATT characteristic. Mirroring cue state over BLE is a possible future ([Forward path](#forward-path)),
  explicitly not v1.
- **No sensing.** Aizu never reads a sensor. Sources compute bands; Aizu only arbitrates + renders.
- **Not a general animation engine.** A fixed, small motion vocabulary ([Rendering](#rendering)) —
  enough for one pixel and one motor, nothing more.
- **No multi-pixel / strip support.** One onboard NeoPixel (+ later one haptic). A strip is a
  future output sink, not v1 scope.

## The cue

A source expresses itself by posting one **cue**. A cue is pure data — no behaviour, no pixel:

```
Cue {
  source    // enum: KEHAI, KANKI, SYSTEM, … (extensible; see the source table)
  priority  // scalar; higher wins. Coarsely: ALERT range > AMBIENT range > QUIESCENT
  colour    // RGB
  motion    // {kind, periodMs} — see Rendering
  maxAgeMs  // liveness: a cue older than this without a refresh is dropped
}
```

Interface (one source occupies at most one slot; posting **replaces** that source's slot):

```
postCue(source, priority, colour, motion, maxAgeMs)   // add or refresh this source's cue
clearCue(source)                                       // withdraw this source's cue
```

- **Sources post at their own cadence**, not Aizu's: Kehai from its ~100–150 ms reflex tick, Kanki
  on each ~5 s SCD reading. Posting is cheap — it just writes a slot. **Aizu renders on its own
  clock** ([Rendering](#rendering)).
- **Liveness / staleness.** A held cue persists until replaced or cleared, *but* Aizu drops any cue
  not refreshed within its `maxAgeMs` (e.g. Kanki ~15 s = 3× its update). This means a wedged or
  unplugged source can't freeze a stale colour on the pixel. A source going inactive (e.g. SCD
  removed) should also `clearCue` itself; `maxAgeMs` is the backstop.
- **Quiescent ≠ a cue.** "All good" states (Kehai *Clear*, Kanki *Fresh*) post **nothing** (or
  `clearCue`). With no active cue, Aizu shows [Idle](#idle) — which is green/dark, i.e. "all good"
  falls through to the idle wallpaper naturally, regardless of which module was quiet.

## Arbitration

Every render tick, Aizu: drops stale cues → picks the **highest-priority live cue** → renders it.
No live cue → [Idle](#idle).

**The source/priority table (v1 default).** Adding a source = adding a row. Priority is the single
knob; `ALERT` cues outrank `AMBIENT` cues, resolving the two-class model of KD-1 into concrete
numbers and filling the gap KD-1's ladder left — **where Kehai's *Approach* sits** ([AZ-1](#decisions)):

| Rank | Cue | Class | Colour / motion | Rationale |
|------|-----|-------|-----------------|-----------|
| 1 (highest) | **Kehai Reflex** | ALERT | red, fast pulse / solid | imminent collision — milliseconds matter |
| 2 | **Hokan Fall SOS** | ALERT | red, urgent pulse (latched) | a fall just happened — safety emergency ([AZ-11](#decisions)) |
| 3 | **Kyūkaku Spike** | ALERT | violet→red, fast pulse (transient, non-latched) | something just entered the air — chemical onset; co-critical but decays as air clears ([AZ-12](#decisions)) |
| 4 | **Nesshi** (while held) | INTERACTIVE | temp-band colour, steady | user is actively requesting a read — dominates ambient, yields to a safety alert ([AZ-10](#decisions)) |
| 5 | **Kanki Bad** (≥2000 ppm) | ALERT | red, slow strong pulse | dangerous air — act now, but not collision-urgent |
| 6 | **Kehai Approach** | AMBIENT⁺ | amber, ramping pulse | something physically approaching — more *immediate* than slow air |
| 7 | **Kanki Poor** (1200–2000) | AMBIENT | orange, slow breathe | open a window |
| 8 | **Kyūkaku Foul** (`r < 0.35`) | AMBIENT | violet, slow strong breathe | chemically loaded air — peer of stale air, told apart by hue ([AZ-12](#decisions)) |
| 9 | **Kanki Stuffy** (800–1200) | AMBIENT | amber, slow breathe | ventilation slipping |
| 10 | **Kyūkaku Taint** (`0.35 ≤ r < 0.60`) | AMBIENT | dim violet, slow breathe | a mild smell is present ([AZ-12](#decisions)) |
| 11 (lowest ambient) | **Kiatsu Weather-turn** | AMBIENT | cyan, slow breathe | barometer falling — storm hours out; the calmest, longest-timescale cue ([AZ-13](#decisions)) |
| — | Kehai *Clear* / Kanki *Fresh* / Kyūkaku *Clean* / Kiatsu *Steady* | quiescent | *(no cue)* | falls through to Idle |
| bottom | **Idle** | system | green dim / dark+heartbeat | nothing to report ([Idle](#idle)) |

So on a full rig: the three safety alerts (a collision Reflex, a Hokan fall SOS, a Kyūkaku chemical
Spike) top everything; a **deliberate Nesshi read** (button held) dominates the ambient wallpaper
while active but still yields to a safety alert; the reds sit above the graduated warnings; a physical
*approach* outranks *degrading* air; the two **rival air-senses** (Kanki's stale-air ramp and
Kyūkaku's chemical bands) interleave in the ambient tier and are told apart **by hue, not rank**
(Kanki green→red, Kyūkaku violet — [AZ-12](#decisions)); all-clear collapses to the green/dark idle. A
Reflex or Spike always preempts, then **releases back** to whatever was underneath (a Nesshi read, an
air colour, or idle).

**Anti-flicker.**
- **Upward preempt is instant** for ALERT-class cues — you never debounce a collision warning.
- **Downward / ambient transitions debounce** (a short minimum dwell, ~200–300 ms) so two
  near-equal cues flipping around a boundary don't strobe the pixel.
- Per-*source* band hysteresis (Kehai distance bands, Kanki ppm bands) stays in the sources; Aizu's
  debounce is only about arbitration between winners.

## Rendering

Aizu is the **only** thing that writes the NeoPixel, on its own render tick (**~20 ms / ~50 fps**,
tunable — fast enough that a breathe reads smooth, [AZ-6](#decisions)). Sources name a motion; Aizu
owns the clock and the math.

Motion vocabulary (fixed, small):

| Motion | Light | (Future) haptic analogue |
|--------|-------|--------------------------|
| `STEADY` | constant at brightness | constant weak hold |
| `BREATHE(period)` | smooth sine fade 0↔peak | slow swell |
| `PULSE(period)` | sharp on / dim, at rate | buzz bursts at rate |
| `SOLID` | peak, no motion | strong continuous |
| `HEARTBEAT(interval)` | one brief blink per interval | one tap per interval |

- **Brightness.** A single global cap `AIZU_MAX_BRIGHT` (start ~40/255 — eye comfort + battery)
  governs peak; gamma-correct the fade so breathe looks linear. (Generalises Kehai's
  `KEHAI_MAX_BRIGHT`.)
- **Modality-agnostic dispatch.** Rendering goes through an output-**sink** abstraction. v1
  registers one sink (NeoPixel). The DRV2605 later registers a second sink that maps the *same*
  winning cue's class/motion to a haptic pattern. Sources and the arbiter are untouched by that
  addition ([AZ-5](#decisions)).

## Idle

When no cue is live, Aizu renders the idle wallpaper — the home for Kehai-Hikari **D-1** and Kanki
**KD-2**, now owned here ([AZ-2](#decisions)):

- **Tethered** (`Serial` present): dim green **breathe** — a calm "alive, all clear."
- **Field / battery** (`!Serial` — a USB power bank presents no CDC host, the same proxy the
  flash-logger uses): **LED off**, plus a **dim-green heartbeat** (one soft blink ~every 30 s,
  tunable) so the wearer knows it's alive despite the dark resting state.

Idle brightness obeys `AIZU_MAX_BRIGHT`. The green idle *is* the "everything's fine" signal, so
quiescent source states need post nothing.

## Input — the BOOT button

Aizu owns GPIO0 (the QT Py BOOT button) at runtime and exposes it as a small **gesture layer** — the
input twin of the cue bus — emitting debounced events that modules subscribe to ([AZ-9](#decisions)).
This is where the deferred Kehai-Hikari **D-2** mute lands, now sharing the button with
[Nesshi](../zokyo/nesshi.md):

- **`CLICK`** (short press-release) → **toggle mute** (Aizu's own function). Muted = pixel fully dark
  regardless of cues (stealth / dark room); click again restores.
- **`HOLD`** (press-and-hold ≥ ~400 ms; `HOLD_START` / `HOLD_END`) → routed to a subscriber. First
  subscriber is [Nesshi](../zokyo/nesshi.md) (hold-to-measure). Click and hold are separable by duration,
  so mute and measure share the one button with no mode switch.
- **Debounced;** GPIO0 doubles as the bootloader strap, so it's only read as input after boot.
- **Mute is absolute** in v1 — an explicit, deliberate gesture wins over everything, including a
  Reflex ([AZ-8](#decisions)); a "mute-except-critical" mode is a future mobility-aid option.
  Gestures beyond CLICK/HOLD (double-click, long-hold → brightness/profiles) are [Forward path](#forward-path).

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`.

1. **One module, one owner.** Aizu is the only code that `#include`s / uses `Adafruit_NeoPixel` and
   the only reader of GPIO0. `Aizu.begin()` in `setup()` (NeoPixel init, `NEOPIXEL_POWER` HIGH,
   button `pinMode`); `Aizu.tick()` called every `loop()` iteration (it self-rate-limits rendering
   to the render tick).
2. **Sources post, never paint.** Kehai calls `postCue` from its reflex tick; Kanki calls it on each
   SCD update. Neither references the NeoPixel. This is the generalisation of Kehai's `driveReflex()`
   and the reason both feature specs were amended.
3. **Non-intrusive.** `Aizu.tick()` never blocks, never writes the Serial telemetry stream, and
   never touches `lastUpdate`, the 1500 ms telemetry cadence, BLE notify, or the untethered
   `!Serial` flash-logging gate.
4. **Degrades to nothing.** With no source posting (all relevant sensors absent), Aizu just renders
   Idle — the board still boots and logs as before.

## Contract impact

**None.** Aizu is on-body output over signals already in the firmware. No CSV column, no GATT
characteristic. Same posture as the two feature specs. (A future "cue state" BLE characteristic
would be a contract change and is out of scope — [Forward path](#forward-path).)

## Acceptance criteria

1. **Single source (Kehai):** with only Kehai posting, the LED matches the Kehai-Hikari spec's ACs
   exactly (Aizu adds no behaviour of its own in the single-source case).
2. **Single source (Kanki):** likewise for Kanki-only.
3. **Preempt + release:** a Kehai Reflex posted over an active Kanki ambient takes the pixel within
   one render tick; when the Reflex clears, the LED returns to the Kanki cue (or Idle).
4. **Ladder:** given two simultaneous live cues, the higher-ranked per the [source table](#arbitration)
   renders (spot-check Reflex vs Kanki-Bad, and Kehai-Approach vs Kanki-Poor).
5. **Anti-flicker:** two near-boundary cues do not strobe (downward debounce holds), yet a Reflex
   still preempts with no perceptible debounce.
6. **Idle:** no live cue → dim green breathe tethered; dark + ~30 s heartbeat on battery.
7. **Mute:** a BOOT press blanks the pixel regardless of active cues; a second press restores.
8. **Staleness:** a source that stops refreshing has its cue dropped after `maxAgeMs` (no frozen
   stale colour).
9. **Sole writer / no regression:** only Aizu writes the NeoPixel; CSV header/order/cadence, BLE
   notify, and flash-logging are unchanged.
10. **Extensible (by inspection):** adding a hypothetical new source needs only a priority-table row
    + `postCue` calls — no change to the arbiter or rendering core.
11. **Haptic-ready (by inspection):** a second output sink (DRV2605) can be registered without
    editing any source or the arbiter core.

## Decisions

- **AZ-1 — Priority via an explicit source table.** Arbitration is a single scalar; ALERT outranks
  AMBIENT. The v1 ladder in [Arbitration](#arbitration) is committed in full, including the Kehai
  *Approach* rung ([AZ-7](#decisions)).
- **AZ-2 — Idle owned by Aizu.** Kehai-Hikari D-1 + Kanki KD-2 are now a single Aizu idle policy
  (green breathe tethered · dark + heartbeat on battery), inherited by every source.
- **AZ-3 — Mute is absolute (v1).** An explicit BOOT press → full dark, over everything, including a
  Reflex ([AZ-8](#decisions)).
- **AZ-4 — Aizu is the sole NeoPixel writer** and sole GPIO0 reader; owns power pin, brightness,
  animation clock.
- **AZ-5 — Modality-agnostic via output sinks.** v1: one sink (NeoPixel). Haptic = a second sink,
  no source/arbiter change.
- **AZ-6 — Render tick ~20 ms (~50 fps), tunable.** Smooth breathe without meaningful battery cost;
  posting is decoupled and cheaper still.
- **AZ-7 — Approach rung committed.** Kehai *Approach* ranks above Kanki *Poor/Stuffy* and below
  Kanki *Bad*. Full v1 ladder: `Reflex > Nesshi(held) > Kanki-Bad > Kehai-Approach > Kanki-Poor >
  Kanki-Stuffy`. A physical approach outranks *degrading* air but not *dangerous* air. (Alternative —
  suppressing Approach whenever Kanki is present — rejected: it guts Kehai's designed approach ramp.)
- **AZ-8 — Mute does not pierce for a Reflex (v1).** Mute stays absolute; a collision Reflex does
  *not* override it, because muting is a deliberate stealth act. Revisit only if Aizu is ever fielded
  as a mobility aid, where a safety cue piercing mute would be warranted.
- **AZ-9 — Input is a gesture layer.** GPIO0 is exposed as debounced `CLICK` / `HOLD` events routed
  to subscribers (the input twin of the cue bus): `CLICK` → mute, `HOLD` → a subscriber (first is
  [Nesshi](../zokyo/nesshi.md)). Generalises AZ-3's single-press mute so mute and hold-to-measure share the
  one button. (Introduced by [Nesshi](../zokyo/nesshi.md).)
- **AZ-10 — Nesshi rung.** A **Nesshi (while held)** INTERACTIVE cue sits just below Kehai Reflex and
  above Kanki Bad — a deliberate read dominates the ambient wallpaper but yields to a collision.
  (Introduced by [Nesshi](../zokyo/nesshi.md).)
- **AZ-11 — Hokan fall SOS rung.** A **Hokan Fall SOS** ALERT cue joins Kehai Reflex at the top as a
  co-critical safety alert (rank 2, above the interactive/ambient cues); it **latches** until resolved
  or muted. The two safety alerts don't meaningfully co-occur. (Introduced by [Hokan](../zokyo/hokan.md).)
- **AZ-12 — Kyūkaku rungs + colour is a source identity.** Two new source rungs: a **Kyūkaku Spike**
  ALERT in the safety tier (rank 3, below Fall SOS) — a chemical onset that is co-critical but
  **non-latching** (it decays as the air clears, unlike a fall); and **Kyūkaku Foul/Taint** AMBIENT
  cues interleaved with Kanki's air bands. Kyūkaku is the first *same-category rival* source (a second
  ambient air-sense beside Kanki), which forces a convention the ladder alone never stated: **hue is a
  per-source identity, not a global severity scale.** Kanki keeps the green→amber→orange→red air ramp;
  Kyūkaku owns violet (→ red only at a Spike peak). Aizu already lets sources name their own colour, so
  arbitration and rendering are unchanged — this decision only makes the convention explicit and adds
  table rows. (Introduced by [Kyūkaku](../zokyo/kyukaku.md).)
- **AZ-13 — Kiatsu weather rung (the calm floor).** A **Kiatsu Weather-turn** AMBIENT cue joins as the
  **lowest** ambient rung (rank 11, just above Idle) — cyan, slow breathe, for a falling barometer
  (storm hours out). It is the opposite pole from a Kehai Reflex: the calmest, longest-timescale cue in
  the system, and the one most readily preempted. Extends the source-hue palette to cyan (per the
  [AZ-12](#decisions) identity convention: Kanki green→red, Kyūkaku violet, Kehai amber, Kiatsu cyan).
  No arbiter/rendering change. (Introduced by [Kiatsu](../zokyo/kiatsu.md).)

## Cross-spec impact

- **Kehai-Hikari & Kanki** — both already amended to post cues rather than own the pixel; this spec
  is where those cues are defined. No further edits needed; the Approach rung is committed ([AZ-7](#decisions)).
- **Nesshi** — introduced the input **gesture layer** (AZ-9) and the interactive **Nesshi rung**
  (AZ-10). Aizu now shares *both* seams: the cue bus (output) and the CLICK/HOLD bus (input).
- **Hokan** — adds a top-tier **Fall SOS** ALERT rung (AZ-11), latching until resolved; the first
  cue source whose *other* output is base-side (the dead-reckoned path).
- **Kyūkaku** — adds a **Spike** ALERT rung (safety tier, non-latching) and **Foul/Taint** AMBIENT
  rungs (AZ-12); the first *same-category rival* source, which makes explicit that **colour is a
  source-owned identity** (violet vs Kanki's green→red) so two air-senses stay legible on one pixel —
  no arbiter/rendering change, only table rows and the convention.
- **Kiatsu** — adds the **lowest** ambient rung, a cyan **Weather-turn** cue (AZ-13) — the calmest,
  longest-timescale cue, opposite pole from a Reflex; extends the source-hue palette to cyan. No
  arbiter/rendering change.
- **Registry (build-time)** — Aizu wants an entry as a **shared host capability** (beside the
  ground-station's shared-tooling note, not in any one Zōkyō). Also: the **onboard NeoPixel is not
  yet in the [parts catalog](../../REGISTRY.md#parts-catalog)** — it should be added under
  *Output & feedback* as Aizu's primary output surface, with the DRV2605 as its second sink.

## Forward path

- **Haptic sink:** register the DRV2605 as a second output sink; the winning cue drives light and
  vibration together — Kehai's original "sensed presence" reflex, now truly felt.
- **Profiles / brightness:** BOOT long-press to cycle brightness or a "proximity-priority" vs
  "air-priority" profile (re-weighting the ladder without a rebuild).
- **Cue mirroring:** an optional BLE "current cue" characteristic so the [Shikai](../../REGISTRY.md#shikai-視界--field-of-view)
  HUD can echo the on-body cue in the glasses — a deliberate contract addition, specced separately.
- **More sources:** Nesshi (hot/cold), a NeoPixel compass, a gait alert — each is a table row + a
  `postCue` call, nothing more.
