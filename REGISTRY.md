# Zōkyō & Tsukiwaza registry

The canonical list of what this platform *does*, in the project's vocabulary —
**Shintai-OS** / **Zōkyō** / **Tsukiwaza**, defined in the
[README](README.md#naming).

Shintai-OS is the body OS; a **Zōkyō** is one augmentation built on it, composed of
**Tsukiwaza** (the discrete modules it attaches). This registry indexes the Zōkyō —
**Rokkan** is the first, not the last — over a [parts catalog](#parts-catalog) any
Zōkyō can draw from.

## Zōkyō

| Zōkyō | Meaning | What it does | Status |
|-------|---------|--------------|--------|
| **[Rokkan (六感)](#rokkan-六感--sixth-sense)** | *"sixth sense"* | Wearable environmental-perception suite — extends the senses past the ordinary five and feeds them back to the wearer. | active |
| **[Kanki (換気)](specs/zokyo/kanki.md)** | *"ventilation"* | Standalone air-quality guardian — maps SCD-40 `co2_ppm` to a calm→alarm colour on the onboard NeoPixel (via [Aizu](specs/platform/aizu.md)). One sense, one cue; runs on host + SCD-40 alone. | active |
| **[Metsuke (目付)](specs/zokyo/metsuke.md)** | *"the gaze"* | Thermal sight — downsamples the MLX90640 frame to an 8×8 grid, streams it over a **new binary BLE characteristic**, and renders a false-colour heat panel in the glasses (via [Shikai](#shikai-視界--field-of-view)). First module to change the contract. | active |
| **[Nesshi (熱視)](specs/zokyo/nesshi.md)** | *"heat-sight"* | Point-and-read thermometer — hold the BOOT button and the MLX90640 surface temp reads out as a calm→alarm colour on the NeoPixel (via [Aizu](specs/platform/aizu.md)); a double-hold finds the scene's hottest point. "Is it safe to touch?" No contract change; shares Aizu's input (HOLD) and output. | active |
| **[Hokan (歩勘)](specs/zokyo/hokan.md)** | *"step-reckoning"* | Pedometer + fall detector + GPS-denied dead-reckoner — live IMU DSP counts steps and catches falls (fall → latching [Aizu](specs/platform/aizu.md) SOS); the ground-station reconstructs the walked path from logged `steps` + heading. First **CSV-half** contract change, first on-device real-time DSP, first module spanning body + base. | active |
| **[Kōei (後衛)](specs/zokyo/koei.md)** | *"rearguard"* | Rear dual-arc proximity — a left/right VL53L4CX pair behind a PCA9546 mux widens the rear radar from one beam to a spread arc; `alert` + [Kehai](#kehai-気配--sensed-presence) key off the nearer arc. First **multi-instance sensor**, first to **reshape** an existing contract field (split `distance_mm`, repack the `Distance` char). | active |
| **[Kyūkaku (嗅覚)](specs/zokyo/kyukaku.md)** | *"olfaction"* | Electronic sense of smell — watches the BME688 `gas_ohms` against an adaptive clean-air baseline (no calibration) and fires a violet **Spike** the instant the air changes (smoke/gas/solvent), plus a calm **Foul** ambient for loaded air (via [Aizu](specs/platform/aizu.md)). Completes Rokkan's *"sixth sense"* as literal smell; first **same-category rival** on Aizu — proves colour is a source identity, not a severity scale. No contract change. | active |
| **[Kiatsu (気圧)](specs/zokyo/kiatsu.md)** | *"atmospheric pressure"* | Barometric sense from the BME688 `pressure_hpa` — base-side floor detection (a storey ≈ 0.4 hPa) tags [Hokan](specs/zokyo/hokan.md)'s dead-reckoned path with a **Z-axis** (3-D GPS-denied nav), while an on-device 3-h trend posts a calm cyan **weather-turn** cue via [Aizu](specs/platform/aizu.md). **Hokan inverted** — a multi-surface module whose signals are *slower* than the log, so it needs neither a contract change nor on-device DSP. | active |
| **[Kaori (香り)](specs/zokyo/kaori.md)** | *"scent"* | Scent *identification* — runs the BME688 gas-scanner through Bosch **BSEC2** to **name** a trained scent (`solvent`/`coffee`/`smoke`…) as a live Scent BLE label **and** a logged `scent_class` timeline; [Kyūkaku](specs/zokyo/kyukaku.md)'s reflex duty-cycles the classifier (reflex wakes cognition). The BME688's **Metsuke moment** — first BME688 contract change, first to change **both** contract halves (CSV + BLE), and the first **non-thrifty** build (closed-source BSEC2 + offline training + N4R2 headroom). | spec |

Rokkan is the first Zōkyō built; Kanki is its small sibling — one sense, one cue,
drawn from the same parts catalog. Metsuke rides the glasses (Shikai) and is the
first module to touch the data contract (one new binary characteristic). Others
land here as their own sections below,
each composed of Tsukiwaza over the same shared [parts catalog](#parts-catalog) and
[data contract](CONTRACT.md).

The Python **ground-station** (`groundstation/`) is base-side tooling you dock to
for capture and analysis — shared across every Zōkyō, not a worn Tsukiwaza of any
one of them.

**[Aizu (合図)](specs/platform/aizu.md)** is the on-body counterpart: a shared host
capability (not a Zōkyō) that owns the onboard NeoPixel — sources post cues, Aizu
arbitrates and renders the winner. Every Zōkyō with local feedback (Kehai-Hikari,
Kanki, Nesshi, Hokan, Kyūkaku) draws on it rather than touching the pixel directly.

## Parts catalog

The shared inventory every Zōkyō draws from — each part and what it does. Each
sensor is a cheaper **baseline / starter** chosen to get the seam working — the
**Alternatives / upgrades** column is the menu for building out a different (or
beefier) Zōkyō against the same [contract](CONTRACT.md). All sensors sit on the
STEMMA QT / Qwiic I²C chain, so most swaps are plug-compatible: change the part,
change its driver in the firmware, done. Each Zōkyō's Tsukiwaza reference these
parts rather than re-describing them — see [Rokkan](#rokkan-六感--sixth-sense) below.

### Host & infrastructure

| Part | What it does | Alternatives / upgrades |
|------|--------------|-------------------------|
| **Adafruit QT Py ESP32-S3** (no PSRAM, 8 MB flash) | The Shintai-OS host: reads every sensor over I²C, logs to onboard FFat flash, and serves the BLE GATT stream. | QT Py ESP32-S3 **N4R2** (2 MB PSRAM — headroom for full thermal frames); Adafruit **Feather ESP32-S3** (more GPIO + onboard LiPo charging); **ESP32-S3 Reverse TFT Feather** (debug screen); Seeed **XIAO ESP32-S3** (smaller footprint) |
| **STEMMA QT / Qwiic chain** | Solderless I²C daisy-chain wiring every sensor to the host. | JST-SH breakouts; a custom carrier PCB once the sensor set is frozen |
| **PCA9546A I²C mux** (0x70) | 4-channel I²C switch isolating the two rear VL53L4CX ToFs (both fixed at 0x29, so they collide on the plain bus) onto separate channels — **ch0 = rear-left arc, ch1 = rear-right arc**. Written one-hot before every bus touch. | **TCA9548A** (8-channel, for more colliding sensors); per-sensor **XSHUT** address reassignment (no mux, but burns a GPIO per sensor) |
| **USB-C battery bank** | Untethered power — the board flash-logs whenever it's on non-computer power. | LiPo cell (the Feather has the charger built in); a **MAX17048** fuel-gauge for real state-of-charge |

### Sensors

| Part | What it does | Alternatives / upgrades |
|------|--------------|-------------------------|
| **VL53L4CX** — ToF distance (I²C 0x29, ×2) | Time-of-flight ranging, multi-target to ~6 m. A **left/right pair** behind the [PCA9546 mux](#host--infrastructure) forms the rear dual-arc — the proximity sense behind `distance_l_mm` / `distance_r_mm` and the `alert`. | **VL53L1X** (cheaper, 4 m, single-target); **VL53L4CD** (short-range budget); **VL53L5CX** (8×8 multizone — a depth *field*, not a point); ultrasonic **HC-SR04** for long/cheap |
| **LSM6DSOX** — 6-DoF IMU (0x6A) | Accelerometer + gyro: motion and tilt. | Combined **LSM6DSOX + LIS3MDL 9-DoF** board (one STEMMA part); **BNO085** (on-chip sensor fusion → *absolute* orientation, no manual heading math — the standout upgrade); **ICM-20948**; downgrade **MPU-6050 / LSM6DS3TR-C** |
| **LIS3MDL** — magnetometer (0x1C) | 3-axis mag → compass heading. | **MMC5603** (higher-res mag); folded into **BNO085** if you take the fused-IMU route above |
| **Adafruit PA1010D** — GPS (0x10) | Mini I²C GPS: fix, lat/lon, altitude, speed. | **Ultimate GPS breakout / PA1616S** (external antenna, better fix); u-blox **SAM-M8Q** / **NEO-M9N** (multi-constellation); **MAX-M10S** (low power) |
| **MLX90640** — thermal camera (0x33) | 32×24 = 768-px IR camera, 55° FOV — the surface-temp scene. | **MLX90640 110°** (wide FOV); **MLX90641** (16×12, cheaper); **AMG8833 Grid-EYE** (8×8 starter); **FLIR Lepton 3.5** (160×120 — real thermal imaging, needs SPI + PSRAM) |
| **SCD-40** — climate (0x62) | Photoacoustic CO₂ + air temp + humidity; updates ~every 5 s. Owns `co2_ppm`; cedes air temp / humidity to the BME688 when both are present (see [contract](CONTRACT.md)). | **SCD-41** (wider range, lower power — pin-compatible upgrade); **SCD-30** (NDIR); note CO₂-less boards drop the `co2_ppm` field |
| **BME688** — environment (0x77) | Gas-sensor resistance (VOC proxy) + barometric pressure; also the authoritative air temp / humidity (precedence over SCD-40 — faster, no self-heat offset). Fills `air_temp_c` / `humidity_pct` / `pressure_hpa` / `gas_ohms`. | **BME680** (identical driver, no AI gas model); **BMP390** (pressure / altitude only); **ENS160 + AHT21** (dedicated air-quality + T/RH); Bosch **BSEC2** library + **BME AI-Studio** for on-chip IAQ + trained scent classification — realized as [Kaori](specs/zokyo/kaori.md) |

### Output & feedback

| Part | What it does | Alternatives / upgrades |
|------|--------------|-------------------------|
| **QT Py ESP32-S3 onboard NeoPixel** | The single onboard RGB pixel — [Aizu](specs/platform/aizu.md)'s primary output surface: every on-body cue (proximity, air, idle) is rendered here. | An external NeoPixel/strip (a future Aizu output sink, not v1); the DRV2605 haptic below is Aizu's planned *second* sink |
| **RayNeo X3 Pro** AR glasses | Binocular AR display running the `android/` BLE-central HUD app. | Any Android phone (the app's fallback target); **XREAL One / Air 2**, **Vuzix**, **Even Realities G1** — anything that can run a BLE-central Android build |
| **DRV2605L + LRA/ERM motor** *(planned)* | Haptic driver + vibration motor for the Kehai proximity alert — registers as [Aizu](specs/platform/aizu.md)'s second output sink so a winning cue drives light and vibration together. | A bare vibration motor + transistor for the minimal path; Adafruit STEMMA haptic breakout |

## Rokkan (六感) — sixth sense

A wearable environmental-perception suite that extends the senses past the ordinary
five and feeds them back to the wearer. It is three Tsukiwaza — all active (Kehai
now built light-first) — each drawing its parts from the [catalog](#parts-catalog) above.

### Tanchi (探知) — *"detection"*

Perception **in**: senses the environment. Runs on `firmware/shintai-os/` + the
sensor rig. **Status: active.**

Parts: [VL53L4CX](#sensors) ×2 (rear dual-arc distance, via mux), [LSM6DSOX](#sensors)
(motion) + [LIS3MDL](#sensors) (heading), [PA1010D](#sensors) (GPS), [MLX90640](#sensors)
(thermal), [SCD-40](#sensors) (climate), [BME688](#sensors) (env: gas/pressure — its gas
field now expressed on-body as smell via [Kyūkaku](specs/zokyo/kyukaku.md), its pressure as
altitude/weather via [Kiatsu](specs/zokyo/kiatsu.md)) —
all wired to the [QT Py ESP32-S3 host](#host--infrastructure).

**Rear dual-arc (ToF) — [Kōei (後衛)](specs/zokyo/koei.md).** The single rear VL53L4CX is
now a **left/right pair** behind a [PCA9546 mux](#host--infrastructure) — both sensors are
fixed at 0x29, so the mux isolates them onto separate channels. Channel→direction mapping
(authoritative):

| Mux channel | One-hot | Arc | CSV column | Range |
|-------------|---------|-----|------------|-------|
| ch0 | `0x01` | rear-left  | `distance_l_mm` | ~40 mm – 6 m (multi-target) |
| ch1 | `0x02` | rear-right | `distance_r_mm` | ~40 mm – 6 m (multi-target) |

Non-fatal per channel: a missing mux disables both arcs, a missing arc blanks only its
column, and `alert` + [Kehai](#kehai-気配--sensed-presence) key off the **nearer** arc.
BLE packs both into the one `Distance` characteristic (`L:.. R:.. mm`). **Status:
active** (bench bring-up on the USB power bank — two ToF + mux exceed the 500 mAh LiPo's
comfortable draw). See [CONTRACT.md](CONTRACT.md).

### Shikai (視界) — *"field of view"*

Perception **out** (sight): binocular HUD overlay, driven by `android/`.
**Status: active.**

Parts: [RayNeo X3 Pro](#output--feedback) (or a phone as fallback).

### Kehai (気配) — *"sensed presence"*

Perception **out** (touch): the "spidey-sense" proximity reflex. Reacts to
`distance_mm` / the edge-triggered `alert` (see [CONTRACT.md](CONTRACT.md)).
**Status: active — light-first.** Built as
[Kehai-Hikari](specs/zokyo/kehai-hikari.md): the reflex renders on the onboard
NeoPixel via [Aizu](specs/platform/aizu.md) (amber Approach pulse → red Reflex),
running on-host independent of any BLE central. The **haptic** path (DRV2605L +
motor) is still *planned* — it drops in behind Aizu as a second output sink, no
sensing change.

Parts: [QT Py onboard NeoPixel](#output--feedback) (built); [DRV2605L haptic
driver + motor](#output--feedback) *(planned second sink)*.

The output side splits by modality: **Shikai** you *see*, **Kehai** you *feel* (or,
today, *see as light*). Kehai's slot already exists in the contract — `alert` is a
distinct edge-triggered event, separate from `distance_mm` — so it's a reserved
seam, not a retrofit.
