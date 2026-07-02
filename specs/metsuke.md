# Metsuke (目付) — spec

*The gaze: a live low-res thermal overlay in the glasses — heat-vision on the no-PSRAM board.*

**Status:** spec (unbuilt) · **Zōkyō:** Metsuke (candidate — thermal sight, realized through [Shikai](../REGISTRY.md#shikai-視界--field-of-view)) · **Seam:** [CONTRACT.md](../CONTRACT.md) — **changes it** (first spec to)

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What this is

Metsuke — "the gaze / watchfulness" — puts a **live thermal image in the HUD**. The
[MLX90640](../REGISTRY.md#sensors) already captures a 32×24 (768-px) frame every loop; today only
four summary numbers (`thermal_min/ctr/max/mean`) ever leave the board. Metsuke **downsamples the
frame on-device to a coarse grid, streams it over BLE, and the glasses render it as a false-colour
heat panel** — a "predator vision" view of warm bodies, hot machinery, and heat leaks, at the
thermal camera's native ~2 Hz.

It is the first module that is genuinely **sight-out rich**: where [Shikai](../REGISTRY.md#shikai-視界--field-of-view)
today shows sensor *readouts*, Metsuke shows an *image*. It's a candidate **new Zōkyō** (a new sense
— thermal vision) but, unlike its siblings, it is **not standalone**: it rides the glasses HUD
(Shikai) and it is the **first module to add to the data contract**.

Two structural firsts, called out because they break the series' pattern:
- **It changes [CONTRACT.md](../CONTRACT.md)** — Kehai-Hikari, Kanki, and Nesshi were all output-only
  from existing signals; Metsuke needs a **new BLE characteristic** to carry the grid.
- **It uses Shikai, not [Aizu](./aizu.md)** — the output surface is the glasses, not the NeoPixel.
  Metsuke posts no cue and touches no button.

## Why (the thrifty case)

- **Heat-vision without the expensive path.** The registry's upgrade menu for "real thermal imaging"
  is a **FLIR Lepton (needs SPI + PSRAM)** or the **PSRAM QT Py** for full frames. Metsuke gets a
  *usable* heat overlay from the **MLX90640 you already own on the no-PSRAM board** by sending a
  downsampled grid — coarse, but real thermal vision, for zero new BOM.
- **The frame already exists.** `thermalFrame[768]` is already in RAM each loop; Metsuke only reduces
  and transmits it.
- **Turns Shikai from a dashboard into a sense.** The single most striking thing the glasses could
  show, and it's one BLE characteristic away.

## Goals

1. Downsample the MLX90640 frame on-device to a small grid and stream it over a **new BLE
   characteristic** at ~2 Hz.
2. Render it in the glasses as a **false-colour heat panel**, auto-ranged so warm/cool read clearly.
3. Stay within the no-PSRAM board's limits — coarse grid, negotiated MTU, gated on subscription.
4. Change the contract **once, cleanly**, respecting the three-mirror discipline (CONTRACT → firmware
   → Kotlin).

## Non-goals

- **Not a registered AR overlay (v1).** The MLX90640 isn't boresighted to the eyes, so a pixel-aligned
  see-through overlay needs mounting + calibration. v1 is a **HUD panel** (a heat mini-image in a
  corner), not a world-locked overlay ([MD-3](#decisions), [Forward path](#forward-path)).
- **Not logged.** The grid is a **live, ephemeral** stream for the HUD — it is **not** added to the
  CSV schema or the flash log (that would bloat every row / the flash). The existing summary temps
  stay the logged representation. So Metsuke touches only the **BLE half** of the contract.
- **Not high-res / not video.** ~2 Hz, coarse grid. "Hot blob" awareness, not a FLIR image.
- **No new sensor.** MLX90640 only; the Lepton/PSRAM route is the upgrade path, not this spec.

## Parts (Tsukiwaza)

| Part | Role | Source |
|------|------|--------|
| [MLX90640](../REGISTRY.md#sensors) | 768-px thermal frame → downsampled grid | already read in `loop()` |
| [RayNeo X3 Pro / phone](../REGISTRY.md#output--feedback) | renders the heat panel | via [Shikai](../REGISTRY.md#shikai-視界--field-of-view) / `android/` |
| **BLE (new characteristic)** | carries the grid | firmware ↔ `android/` |

## Behaviour — frame → grid → heat panel

1. **Downsample on-device.** Reduce the 32×24 frame to an **8×8 grid** (v1) by block-averaging
   (each cell = mean of its 4×3 source block, skipping NaNs). Coarse but legible; fits one BLE
   notification ([Contract change](#contract-change)). 16×12 is the upgrade ([MD-1](#decisions)).
2. **Auto-range.** Carry the frame's `min`/`max` temp alongside the grid so the glasses scale the
   palette to the current scene (a cool room and a hot engine both read well).
3. **Stream at camera rate.** Emit on each new thermal frame (~2 Hz), **only while a central is
   subscribed** (save power + BLE airtime when the glasses aren't watching).
4. **Render false-colour.** The glasses map each cell through a thermal palette (ironbow / inferno),
   auto-ranged by min/max, and draw an 8×8 heat panel in the HUD — placed per the RayNeo display
   mode (2D-duplicated vs true-stereo; the app already adapts by aspect ratio).

Motion is not "video" — at 2 Hz it's a pulsing heat map; that's expected and fine for awareness.

## Contract change

**This is the notable part — Metsuke is the first module to edit [CONTRACT.md](../CONTRACT.md).**
Per the project invariant, change the contract **first**, then its three mirrors: firmware
characteristic + UUID, the Kotlin UUID in `android/.../ShintaiGatt.kt`, and this table.

A new **Thermal Grid** characteristic (`READ | NOTIFY`), proposed UUID
`abcd7890-ab12-ab12-ab12-abcdef123456` (fits the existing `abcdXXXX-…` pattern):

- **First *binary* characteristic.** Every existing characteristic is a **UTF-8 string**; a heat grid
  as text is wasteful. Metsuke carries **packed bytes** ([MD-2](#decisions)):
  - Header: `int16 min_dC`, `int16 max_dC` (temperature ×10, little-endian) — 4 bytes.
  - Body: `64 × uint8`, row-major 8×8, each cell = `round((t − min)/(max − min) × 255)`.
  - Total **68 bytes**.
- **First characteristic that needs MTU negotiation.** 68 bytes exceeds the default 20-byte ATT
  payload, so the central must **request a larger MTU** (~185+) on connect. The existing short-string
  characteristics never needed this; Metsuke does.
- **CCCD gotcha still applies.** The notify-enable descriptor is the Bluetooth base UUID
  `00002902-0000-1000-8000-00805f9b34fb` — the `8000` (not `0000`) matters, or notifications
  silently die (see [CONTRACT.md](../CONTRACT.md)).

The CSV schema is **unchanged** — the grid is BLE-live-only.

## The no-PSRAM constraint (the honest framing)

The registry names the PSRAM QT Py and the FLIR Lepton as the paths to "full thermal frames / real
imaging." Metsuke deliberately does **not** need them: an 8×8 (or 16×12) grid is small enough to
reduce and transmit on the no-PSRAM board over BLE. The coarse resolution is the *cost* of staying
thrifty, and it's honest — this is heat *awareness*, and the upgrade path (bigger grid on the PSRAM
board, or a Lepton) is real and documented, not hidden.

## Firmware integration

Target: `firmware/shintai-os/shintai-os.ino`.

1. **Add the characteristic.** Create the Thermal Grid characteristic in `setup()` (alongside the
   others), with its `BLE2902` CCCD.
2. **Downsample + pack.** When `thermalOk`, reduce `thermalFrame[768]` → 8×8, pack header + cells,
   `setValue()` + `notify()` — gated on `deviceConnected` **and** the grid CCCD being subscribed.
3. **Rate.** Tie emission to the thermal frame cadence (~2 Hz), independent of the 1500 ms telemetry.
4. **MTU.** Allow a larger negotiated MTU (ESP32 BLE supports it); the central drives the request.
5. **No log / no CSV.** Do not write the grid to flash or the CSV row.
6. **Degrades.** MLX90640 absent → the characteristic simply never notifies (like every other
   presence-gated sensor).

## Android / Shikai integration

Target: `android/` (Shikai HUD).

1. **New UUID** in `ShintaiGatt.kt` mirroring the contract; subscribe by writing the CCCD (mind the
   `8000`).
2. **Request MTU** (~247) on connect so a 68-byte notification arrives whole.
3. **Parse** header (min/max) + 64 cells; **render** an 8×8 false-colour panel (ironbow/inferno),
   auto-ranged by min/max.
4. **Placement** adapts to the RayNeo display mode (720×480 2D-duplicated vs 1280×480 true-stereo) —
   the app already branches on aspect ratio; the heat panel sits in a HUD corner.

## Acceptance criteria

1. **Stream:** with the glasses subscribed, an 8×8 heat grid arrives at ~2 Hz; moving a warm object
   (a hand, a mug) visibly moves the hot cells.
2. **Auto-range:** a cool scene and a hot object both render with usable contrast (palette scales to
   min/max).
3. **Whole frames:** with MTU negotiated, each 68-byte notification is received intact (no truncation).
4. **Gated:** the board notifies the grid **only** while a central is subscribed; unsubscribed → no
   grid traffic.
5. **Contract discipline:** CONTRACT.md, the firmware characteristic/UUID, and the Kotlin UUID all
   match; CCCD uses `8000`.
6. **No regression:** existing characteristics, the CSV schema/cadence, and flash logging are
   unchanged; the grid is never logged.

## Decisions

All five opening questions are resolved; recorded here as the build contract.

- **MD-1 — 8×8 grid (v1).** Block-average the 32×24 frame to 8×8 (64 cells) — fits a modest negotiated
  MTU. 16×12 is the upgrade (MTU ~247, or the PSRAM board).
- **MD-2 — Binary payload.** Packed bytes: `int16 min_dC`, `int16 max_dC`, then `64 × uint8` cells
  (68 B total). The contract's **first binary characteristic** — document it as such.
- **MD-3 — HUD panel (v1).** A false-colour heat panel in a HUD corner, not a world-locked overlay;
  the registered overlay (boresight + calibration) is [Forward path](#forward-path).
- **MD-4 — Ironbow palette.** Default ironbow (reads intuitively as heat), with a small toggle for
  inferno / grayscale.
- **MD-5 — 2 Hz while subscribed.** Emit at the camera's ~2 Hz, only while a central is subscribed;
  no throttling below that.

## Cross-spec impact

- **CONTRACT.md** — add the Thermal Grid characteristic row (UUID, binary payload, MTU note). This is
  the first contract edit in the series; the three mirror sites (firmware, Kotlin, contract) must move
  together.
- **Shikai** — Metsuke extends it from a readout HUD to one that renders an image; the panel is a new
  Shikai surface.
- **Registry (build-time)** — Metsuke earns a Zōkyō row beside Rokkan/Kanki/Nesshi, noting it depends
  on Shikai and is the first contract-touching module.

## Forward path

- **Registered overlay:** boresight the MLX90640 to the eyeline, calibrate FOV, and warp the grid to
  a world-locked see-through thermal overlay — true heat-vision.
- **Higher resolution:** 16×12 on a negotiated MTU, or the **PSRAM QT Py** / **FLIR Lepton** for real
  imaging — the registry's documented upgrade path.
- **Interpolation + logging option:** bilinear upscaling of the coarse grid in the glasses for a
  smoother panel; an opt-in "record thermal" mode if a session ever needs the grid on disk.
