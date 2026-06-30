# Shintai-OS

Wearable multi-sensor platform built on an **Adafruit QT Py ESP32-S3**. A single
Arduino sketch reads every sensor and exposes the data three ways: a USB-serial
stream (human-readable **and** CSV), autonomous logging to onboard flash, and a
**BLE** broadcast. Around that firmware sit two clients — a Python toolchain that
logs and visualizes the serial/flash data, and a Kotlin/Compose Android app that
reads the BLE stream live on **RayNeo X3 Pro** AR glasses (or a phone).

## Naming

The project's vocabulary borrows from Japanese, with each term owning a distinct
role:

- **Shintai-OS (身体OS)** — *"body OS."* The platform: the firmware and the system
  that runs on the body and coordinates everything beneath it.
- **Zōkyō (増強)** — *"augmentation / enhancement."* The general concept and
  category — the discipline of augmenting the body, and the class any given
  capability belongs to. Uncountable: you *do* Zōkyō; you don't count "a Zōkyō."
- **Tsukiwaza (付き技)** — *"attached technique."* A single, discrete augmentation
  module that attaches to the body — countable and named. This sensor rig (the
  QT Py ESP32-S3 + its sensor suite) is one Tsukiwaza; the HUD and the glasses
  readout are how you interface with it.

In short: **Shintai-OS** runs on you, **Zōkyō** is the practice, and each
**Tsukiwaza** is a module you attach.

## Components

Three modules meet at one seam — the data contract in
[`CONTRACT.md`](CONTRACT.md). The board (Shintai-OS core) produces; the
ground-station and the Android HUD are two independent consumers.

| Path | Module | What it is |
|------|--------|------------|
| `firmware/shintai-os/` | Shintai-OS core | ESP32-S3 sketch — sensors, serial output, onboard flash logging, BLE GATT server |
| `groundstation/` | Consumer (serial + flash) | Python toolchain: `shintai-logger` (capture), `shintai-pull` (flash dump), `hud` (offline HUD), `analyze` (stitched timeline) |
| `android/` | Consumer (BLE) | Kotlin BLE-central HUD app for RayNeo X3 Pro / phone — see [`android/README.md`](android/README.md) |
| `CONTRACT.md` | — | The CSV schema + BLE GATT contract: the single source of truth all three cite |

## Architecture (data flow)

```
                          ┌─ USB serial (human + CSV) ─▶ groundstation/shintai-logger.py ─▶ logs/*.csv ─┐
 firmware/shintai-os ─────┤                                                                             ├─▶ hud.py / analyze.py ─▶ analysis/
 (QT Py ESP32-S3)         ├─ onboard FFat flash ───────▶ groundstation/shintai-pull.py (USB) ─▶ logs/*.csv ┘
   ▲ the data contract    │
   ▼ (see CONTRACT.md)    └─ BLE GATT (notify) ─────────▶ android/ app  (RayNeo X3 Pro / phone)
```

The sensors (each an optional *Tsukiwaza* — the firmware warns and continues if
one is absent): VL53L4CX distance (ToF), LSM6DSOX accelerometer + LIS3MDL
magnetometer (heading), Adafruit GPS, MLX90640 thermal camera, and an SCD-40
(CO₂ / air temp / humidity).

## Running

1. Plug the QT Py into a USB-C port.
2. **Close the Arduino IDE Serial Monitor** if it's open — it and the logger
   can't share the serial port (you'll get a `Resource busy` error otherwise).
3. Start the logger:

   ```sh
   shintai      # start logging  (alias -> start.sh; activates conda env 'shintai')
   ```

   This auto-detects the board, opens a live view, and saves a timestamped CSV
   to `logs/`. Press **Ctrl+C** to stop (it prints the sample count and file path).

4. Open the captured data afterward:

   ```sh
   shintailog   # builds + opens the cyberpunk HUD for the most recent log
   shintaicsv   # (old quick-look) opens the most recent logs/*.csv in Numbers
   ```

   `shintailog` runs `hud.py`: it auto-picks the newest log and builds an
   offline HUD (`analysis/hud.html`) — the GPS route on a baked-in DarkMatter
   street basemap, plus every sensor on a synced time axis. It needs the
   network once (to fetch the basemap tiles); offline, it falls back to a
   tileless route so the view always builds.

> **First time / new shell:** the `shintai` and `shintailog` aliases live in
> `~/.zshrc`, so open a new terminal or run `source ~/.zshrc` once.
> Without the alias, run it directly: `groundstation/start.sh` (or
> `conda activate shintai && python ~/shintai-os/groundstation/shintai-logger.py`).
> Use `python`, **not** `python3` — a pyenv shim shadows the conda env's pyserial.

## Untethered field logging (onboard flash)

You don't need the computer to gather data. While running on external power (a
USB battery bank or wall charger — anything that isn't a computer running a live
session), the board logs every reading to its own internal flash (FFat, the
stock 3.7 MB FATFS partition) — a new file per power-up (`/shtNNNN.csv`,
numbered by a persistent boot counter). Each row is flushed immediately, so a
dying battery only ever loses the current sample.

> **"Keep logging while my laptop is closed":** that's a *power* question, not a
> tethering one. Logging needs power, not the computer. macOS usually cuts USB
> power when the lid closes, so power the board from a **battery bank / wall
> charger** — it'll log the whole time, lid shut or laptop across the room.

```sh
# 1. Power the board from a battery and walk around — it logs to flash.
# 2. Plug back into USB-C, then:
shintaipull    # dumps every flash file into logs/, then offers to erase the board
```

`shintaipull` saves each flash file as `logs/shintai_log_<pulltime>_flashNNNN.csv`,
so `shintailog` (the HUD) and `analyze.py` pick them up like any other log.

Notes:
- Flash logging is **gated on being untethered** (`!Serial`), so tethered live
  sessions via `shintai` are unaffected and don't double-write.
- Capacity: 3.7 MB FFat partition, rows ~90 bytes at one per 1.5 s ≈ **10+ hours**
  per charge. `shintaipull` shows usage; erase when you've pulled what you need.
- Manual control over a serial monitor: `L` lists files, `P` dumps them, `E`
  erases and starts a fresh file.

### Flashing the firmware

```sh
arduino-cli compile --upload \
  -b esp32:esp32:adafruit_qtpy_esp32s3_nopsram:CDCOnBoot=cdc,PartitionScheme=tinyuf2,FlashSize=8M \
  -p /dev/cu.usbmodem101 firmware/shintai-os
```

> The sketch lives at `firmware/shintai-os/shintai-os.ino` — Arduino requires the
> `.ino` to sit in a folder of the same name, so the compile target is the
> `firmware/shintai-os` **directory**, not the file.

The board is 8 MB flash; the stock **tinyuf2** scheme gives a 3.7 MB `ffat`
partition (so the firmware uses **FFat**, not LittleFS — LittleFS can't mount a
FAT partition). `CDCOnBoot=cdc` is what makes the untethered gate work.

### What you should see

In a real terminal the logger shows a **live status panel** that refreshes in
place each sample (the `[#N]` counter ticks up), while logging every reading to
the CSV:

```
SHINTAI-OS — live             [#142]
──────────────────────────────────────
DISTANCE   no reading
HEADING    169° S
ACCEL      X 1.8  Y 0.0  Z 9.8 m/s²
THERMAL    ctr 23.1°C  scene 22.6–31.4°C  (+8.5°C hot)
CLIMATE    23.0°C  41%RH  750ppm
GPS        no fix
──────────────────────────────────────
logging → shintai_log_20260623_120000.csv   Ctrl+C to stop
```

- **DISTANCE** shows `⚠ TOO CLOSE` when an object is within 20 cm.
- **THERMAL** `ctr` is the surface the cam is aimed at; `scene` is the coldest–
  hottest in view. `(+X°C hot)` appears only when something warm is in frame.
- **CLIMATE** is the air around the SCD-40; it reads `warming up…` for the first
  few seconds after boot (and stays blank if that sensor isn't connected).
- **GPS** needs a clear sky view and a minute or two for a first fix indoors.

Notes:
- The SCD-40 updates ~every 5 s, so its values refresh slower than the rest.
- If you pipe or redirect the output (`shintai > out.txt`), it falls back to plain
  `[#N logged]` lines instead of the panel, so no escape codes end up in the file.

## Serial output modes

The sketch streams human-readable text **and** CSV by default. Toggle live by
sending a character over serial (Serial Monitor input, or it's automatic via the
logger):

| Send | Mode  | Output                                  |
|------|-------|-----------------------------------------|
| `h`  | human | labelled lines only                     |
| `c`  | csv   | pure CSV only (clean for copy / plotter)|
| `b`  | both  | human + CSV (default; logger requests this) |

## Data contract (CSV + BLE)

The full CSV column schema and the BLE GATT characteristics live in one place —
[`CONTRACT.md`](CONTRACT.md). That's the source of truth the firmware and both
consumers cite; the tables aren't repeated here so they can't drift.

Quick note on the two temperature families: `thermal_*` are *surface* temps (the
MLX90640 IR camera) while `air_temp_c` / `humidity_pct` / `co2_ppm` are *air* (the
SCD-40, which updates ~every 5 s and is blank for the first few seconds after boot).

## BLE companion app (RayNeo X3 Pro / phone)

`android/` is a Kotlin + Jetpack Compose **BLE-central** app that connects to the
board by a hardcoded MAC (no scanning — the glasses' own BLE starves a scan),
subscribes to the seven notify characteristics, and renders a live HUD: big
distance readout plus heading, accel, GPS, climate, and thermal. It adapts to the
glasses' display mode (side-by-side stereo vs. a duplicated 2D screen), supports
metric/imperial units, and has an on-screen settings panel. Full build/install
instructions and the GATT contract are in [`android/README.md`](android/README.md).

## Repository layout

```
firmware/shintai-os/   Shintai-OS firmware sketch (.ino)
groundstation/         Python consumer — logger / pull / hud / analyze + launchers
android/               BLE consumer — RayNeo X3 Pro / phone HUD app
CONTRACT.md            the CSV + BLE GATT contract (single source of truth)
docs/                  notes on exploring the logged data
```

- `groundstation/shintai-logger.py` — serial → terminal panel + CSV logger
- `groundstation/shintai-pull.py` — pull onboard flash logs over USB
- `groundstation/hud.py` / `analyze.py` — visualization (HUD, stitched timeline)
- `groundstation/start.sh` / `hud.sh` / `pull.sh` — launchers behind the `shintai*` aliases
- `groundstation/logs/` — timestamped CSV captures
- `groundstation/analysis/` — generated HUD / maps / stitched outputs
- `docs/visualizing-data.md` — tools & ideas for exploring the logged data

## License

Copyright © 2026 Jedai Saboteur. Licensed under the **GNU Affero General Public
License v3.0** ([`LICENSE`](LICENSE)).

You're free to use, study, modify, and self-host this for your own sensors and
data. The AGPL's intent here is deliberate: any derivative — **including one run
as a network service** — must release its complete source under the same license.
Use it freely; just keep it open.
