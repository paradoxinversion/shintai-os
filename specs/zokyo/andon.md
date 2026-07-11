# Andon (行灯) — spec

*The lantern: an on-body LED-matrix surface — ambient flair now, a sensor-driven face later.*

**Status:** built (2026-07-10) · **Zōkyō:** Andon (sibling to [Rokkan](../../REGISTRY.md#rokkan-六感--sixth-sense)) · **Seam:** [CONTRACT.md](../../CONTRACT.md) (no change) · **New BOM:** Modulino LED Matrix (ABX00152) · **Date:** 2026-07-10

> Built on `research-development-jūgo`.

## What this is

Andon — "paper lantern" — adds an **8×12 monochrome LED matrix** (Arduino Modulino
[ABX00152](https://docs.arduino.cc/hardware/modulino-matrix), 96 LEDs, same panel as the
UNO R4 WiFi's built-in matrix) to the rig as a small **output surface** worn on the body.
Its name points at the *surface*, not any one thing it shows: today it renders a gentle
**falling-raindrop** animation — flair, a sign of life — and it is the panel that future
sensor-driven faces (a rear-proximity bar, a compass glyph, a thermal thumbnail) will paint.

It is deliberately **not a sensor**: Andon produces no telemetry, so it touches nothing in
the [contract](../../CONTRACT.md). It is the platform's **third output surface**, after
[Aizu](../platform/aizu.md)'s onboard NeoPixel (a single arbitrated cue) and the SSD1306
tertiary OLED pane (a paged text carousel) — the first that is a **spatial pixel field**,
and the first output device that is **new BOM** rather than something already on the rig.

## Why

Andon is honest about its footing — unlike most Zōkyō it is **not zero-BOM and not
sensor-driven yet**. Its value in the series is different:

- **Proves a third output modality.** The NeoPixel says one thing in colour; the OLED says
  words; Andon can say *shape and motion* in an 8×12 field. That's the substrate a lot of
  future glanceable output wants (a looming proximity blob, a heading arrow).
- **Flair is a feature.** A visible, calm idle animation is a "the rig is alive" signal with
  zero cognitive load — the body-side twin of a breathing status LED.
- **Exercises a new integration shape** — a Modulino module (on-module MCU behind a simple
  I²C register protocol), an **address collision** on the spine, and the **library-scope**
  question (Arduino's own lib vs. raw Wire) — see [Bus & bring-up](#bus--bring-up).

## Goals

1. Drive the Modulino LED Matrix as a presence-gated output device — absent → panel dark,
   nothing else changes (the same non-fatal contract every sensor follows).
2. Render a **falling-raindrop** flair animation by default, non-blocking, off the telemetry
   cadence, smooth (~10 fps) and calm.
3. Add **zero telemetry** and **zero new firmware library dependency**.
4. Leave the surface ready for later sensor-driven modes (the frame path is generic).

## Non-goals

- **No sensor coupling yet.** The rain is flair, not a readout. Sensor-driven faces are a
  [forward path](#forward-path), a later branch.
- **No contract change.** No CSV column, no GATT characteristic — Andon is output-only.
- **No Aizu integration.** Andon is its *own* surface with its own render loop; it is not (yet)
  an [Aizu](../platform/aizu.md) output sink. Mirroring the arbitrated cue here is a candidate
  forward mode, not v1.
- **No colour.** The panel is single-colour; brightness (via grayscale) is its only intensity axis.

## Parts (Tsukiwaza)

- **Modulino LED Matrix (ABX00152)** — 8×12 mono panel on the STEMMA QT / Qwiic I²C chain
  (new [Output & feedback](../../REGISTRY.md#output--feedback) part). On-module MCU; simple
  I²C protocol (mode tag + frame). Default 7-bit address **0x39** — see below.

## Bus & bring-up

Two snags, both resolved during bring-up (bench tools:
`firmware/i2c-scan` + `firmware/modulino-matrix-bringup`):

- **Address collision → readdress.** The matrix ships at 7-bit **0x39** (the store's `0x72`
  is its 8-bit write form: `0x39 << 1`). On the Shintai-OS spine **0x39 is already the
  APDS9960** gesture sensor (the OLED pane's swipe input), whose address is fixed. On the
  shared bus they contend. Fix: the matrix's address is changeable and stored on-module, so
  it is **readdressed once to 0x3F** on the bench (Modulino `examples/Utilities/AddressChanger`,
  adapted to `Wire` @ 41/40). The firmware talks to it at 0x3F.
- **Library scope → raw Wire.** The Arduino_Modulino C++ library builds and runs on the
  QT Py ESP32-S3 (verified — the bring-up sketch compiles clean against the `qtpy` profile),
  and its `Modulino_LED_Matrix::begin()` calls `Wire.begin()` with no pins, so on the QT Py it
  needs `Wire.setPins(41,40)` first (the bench sketch does this). But pulling the library into
  the **firmware** drags `Modulino.cpp`'s entire sensor-driver chain (VL53L4CD/ED, LSM6DSOX,
  LPS22HB, HS300x, LTR381RGB) into the build for one LED panel. In its default
  4-bit grayscale mode the protocol is trivial — write `"GS4"` (padded to 12 bytes) once,
  then 48-byte frames (row-major, 2 px/byte, high nibble = even pixel; each nibble a
  0..15 brightness) — so the
  firmware talks to it with **raw Wire** (like the PCA9546 mux), adding **no library**. The
  Modulino library stays as the bench smoke-test only.

## Behaviour — the rain

`AndonPanel.h` owns the driver + animation. A small fixed set of drops (`ANDON_DROPS`), each a
column and a head row that advances one row per `ANDON_TICK_MS` (~90 ms); a drop starts above
the top edge (staggered), draws a **bright head fading to a dim tail** (the `ANDON_LEVELS` ramp,
e.g. `{15, 5, 1}` in 4-bit grayscale), and respawns at a fresh random column once its whole
streak clears the bottom. Each tick composes a 48-byte grayscale frame and pushes it; overlapping
drops keep the brighter pixel.
The **data** layout is exact; which physical edge is "up" (the fall direction) depends on the
on-body mount and is validated on-wrist (a one-line bit-flip flips it).

## Firmware integration

- `firmware/shintai-os/AndonPanel.h` — self-contained raw-Wire driver + raindrop state:
  `andonBegin()` (presence-gated, non-fatal) and `andonService(now)` (non-blocking, self-paced).
- `shintai-os.ino` — include; `andonBegin()` in `setup()` beside the other output devices;
  `andonService(millis())` in the fast section of `loop()`, before the telemetry gate, so the
  animation runs off the 1500 ms cadence like the Kehai reflex and OLED render.
- `sketch.yaml` — **unchanged** (no new library).
- `firmware/i2c-scan` — `0x39` labelled with the collision; `0x3F` added as Andon's home.

## Contract impact

**None.** Andon is output-only and produces no telemetry — no CSV column, no GATT
characteristic, no consumer (groundstation / Glass / Operator) change. `tools/check-contract.py`
is unaffected.

## Acceptance criteria

1. With the panel present at 0x3F, boot prints `[OK] Andon LED matrix @ 0x3F` and a calm
   raindrop animation runs; with it absent, boot prints the `[WARN]` line and everything else
   (telemetry, BLE, reflex, OLED) is unaffected.
2. The animation is smooth and does not hitch the Kehai reflex or the telemetry cadence
   (self-paced, non-blocking) — validate on-wrist.
3. Firmware compiles via `firmware/verify.sh` with **no new library** in `sketch.yaml`.
4. `tools/check-contract.py` passes unchanged.

## Decisions

- **D-1 — Raw Wire, not the Modulino library, in firmware.** Avoids dragging six unrelated
  sensor drivers into the build; matches the project's existing raw-Wire I²C idiom. Protocol
  verified byte-for-byte against Arduino_Modulino 0.9.0.
- **D-2 — Readdress to 0x3F, not mux.** The matrix is write-only output; a persistent on-module
  readdress keeps it directly on the bus and leaves the PCA9546 mux reserved for the colliding
  rear ToFs.
- **D-3 — Name the surface (Andon), not the animation.** Rain is today's default; the name
  stays true once sensor-driven faces land on the same panel.
- **D-4 — Its own render loop, not an Aizu sink (v1).** Andon is a spatial field, not a single
  cue; folding it under Aizu's arbiter is a forward mode, not a launch requirement.

## Forward path

- **Sensor-driven faces** on the same surface: a rear-proximity bar (nearer of
  `distance_l/r_mm` → a looming column), a compass glyph (`heading_deg`/`cardinal`), or a
  thumbnail of the Metsuke thermal grid dithered to 8×12.
- **Aizu mirror mode** — render the currently-winning arbitrated cue as an icon, making Andon a
  second, higher-bandwidth output sink beside the NeoPixel.
- **Grayscale** for anti-aliased shapes / a brightness axis.
