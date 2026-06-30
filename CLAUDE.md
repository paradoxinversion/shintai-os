# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Shintai-OS is a wearable multi-sensor platform on an **Adafruit QT Py ESP32-S3**.
The project is three modules that meet at one seam — the data contract in
[`CONTRACT.md`](CONTRACT.md):

- `firmware/shintai-os/` — the ESP32-S3 sketch (Shintai-OS core): reads sensors and
  publishes them three ways (serial CSV, onboard FFat flash, BLE notify).
- `groundstation/` — the Python consumer: capture + analysis over serial/flash.
- `android/` — the BLE consumer: a Kotlin/Compose HUD app for RayNeo X3 Pro / phone
  (has its own `android/README.md` and build toolchain).

The board produces; the two consumers only ever meet it at the contract. The
data flow is the architecture:

```
firmware/shintai-os ──USB serial (CSV+human)──▶ groundstation/shintai-logger.py ──▶ logs/*.csv
       │                                                                                  │
       ├── onboard FFat flash (untethered) ──groundstation/shintai-pull.py (dump)──▶ logs/*.csv
       │                                                                                  │
       │                                                  groundstation/{hud,analyze}.py ─┘ ──▶ analysis/
       └── BLE GATT (notify) ─────────────────────────▶ android/ app (RayNeo X3 Pro / phone)
```

## Commands

**Ground-station (Python).** No build/test/lint. Three zsh launchers in
`groundstation/` (each `source`s miniconda + `conda activate shintai`, then runs
the matching `.py` with `python`, **not** `python3` — a pyenv shim shadows the
conda env's pyserial). Shell aliases (`shintai`, `shintailog`, `shintaipull`,
`shintaicsv`) in `~/.zshrc` point at the `groundstation/` launchers. `logs/` and
`analysis/` are resolved `__file__`-relative, so they live under `groundstation/`.

```sh
groundstation/start.sh   # shintai     — live capture: serial → panel + logs/*.csv
groundstation/hud.sh     # shintailog  — build + open analysis/hud.html for newest log
groundstation/pull.sh    # shintaipull — dump onboard flash logs into logs/
python groundstation/analyze.py   # stitch ALL sessions → combined.csv + pngs + route_map.html
```

**Firmware.** The `.ino` lives at `firmware/shintai-os/shintai-os.ino` — Arduino
requires the sketch file to sit in a folder of the same name, so the compile
target is the **directory** `firmware/shintai-os`:

```sh
arduino-cli compile --upload \
  -b esp32:esp32:adafruit_qtpy_esp32s3_nopsram:CDCOnBoot=cdc,PartitionScheme=tinyuf2,FlashSize=8M \
  -p /dev/cu.usbmodem101 firmware/shintai-os
```

**Android.** See `android/README.md` (Gradle 8.9 + Android Studio JBR / JDK 21).

## Key invariants (easy to break)

- **`CONTRACT.md` is the source of truth.** The CSV column schema and the BLE GATT
  characteristics are defined there; the firmware `CSV_HEADER`, the Kotlin UUIDs in
  `android/.../ShintaiGatt.kt`, and any hardcoded column names in `groundstation/`
  all mirror it. Change a field → edit `CONTRACT.md` first, then those sites.
- **CSV framing.** The sketch emits a `timestamp_ms,...` header then numeric rows;
  consumers key off `line.startswith("timestamp_ms")` and `line[0].isdigit()`.
- **Serial modes** `h`/`c`/`b` (human/csv/both): the logger sends `b\n` on connect
  to get both streams + a fresh header. Flash control chars on the same line:
  `L` list, `P` dump, `E` erase.
- **Untethered flash logging is gated on `!Serial`** (`CDCOnBoot=cdc` makes this
  work), so tethered live sessions don't double-write. The partition is **FFat**
  (stock tinyuf2 3.7 MB FAT) — *not* LittleFS, despite `shintai-pull.py`'s docstring.
- **`shintai-pull.py` parses framed serial**: `<<<BEGIN name size>>>` … `<<<END>>>`,
  `<<<DONE>>>`, `<<<NOFS>>>`, `<<<ERASED…>>>`. Pulled files become
  `shintai_log_<pulltime>_flashNNNN.csv` so the glob picks them up.
- **The `shintai_log_*.csv` glob is load-bearing.** `hud.py` and `analyze.py` glob it
  (plus legacy `spidey_log_*`) and skip files under `MIN_ROWS` (50) data rows.
- **Close the Arduino IDE Serial Monitor before running any Python tool** — the
  serial port can't be shared (`Resource busy`).
- **BLE CCCD gotcha** (android): the notify-enable descriptor UUID is
  `00002902-0000-1000-8000-00805f9b34fb` — the `8000` (not `0000`) matters; the typo
  silently kills all notifications. See `CONTRACT.md`.

## Firmware layout (`firmware/shintai-os/shintai-os.ino`)

Sensors over I²C: VL53L4CX (distance), LSM6DSOX (accel) + LIS3MDL (mag/heading),
Adafruit GPS, MLX90640 (thermal cam, 768 px), SCD-40 (CO₂/air temp/humidity). Each
is presence-gated (`*Present` flags) and warns-and-continues if absent — a latent
per-sensor module boundary. Also exposes everything over **BLE** (one characteristic
per sensor group). `Preferences` holds a persistent boot counter naming each flash
file `/shtNNNN.csv`. `setup()` brings up sensors + BLE + FFat; `loop()` reads, prints
(per output mode), BLE-notifies, and — when untethered — appends a flushed flash row.
Flash helpers: `listLogs()`, `dumpAllLogs()`, `eraseLogs()`.

## Ground-station (`groundstation/`)

- `shintai-logger.py` — serial → CSV writer. In a TTY it paints an in-place ANSI
  status panel (`dashboard()`); piped, it falls back to plain `[#N logged]` lines.
- `hud.py` — newest log → self-contained offline Plotly HUD (`analysis/hud.html`):
  GPS route on a base64-baked CartoDB DarkMatter basemap (via `contextily`) + every
  sensor on a synced time axis. **Degrades gracefully** to a tileless route offline.
- `analyze.py` — stitches *all* sessions into one absolute timeline
  (`file_start + timestamp_ms`), reports gaps + headline stats, writes `combined.csv`,
  `timeseries.png`, `route.png`, `route_map.html`.
- `analysis/tour/` is a scratch pile of one-off exploration scripts — not the pipeline.

## Notes

- Python deps (pyserial, pandas, numpy, plotly, matplotlib, folium, contextily, PIL)
  live in the conda env `shintai`; there is no requirements file.
- `logs/` and `analysis/` are gitignored (data + generated output) and live under
  `groundstation/`.
