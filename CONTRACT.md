# Shintai-OS data contract

The single source of truth for the interface between the **firmware** (`firmware/`)
and its consumers (`groundstation/`, `android/`). The board produces; everyone else
only meets it here. Change a field → update this file first, then the three sites
that mirror it (firmware `CSV_HEADER`, the Kotlin UUIDs, and any hardcoded column
names in `groundstation/`).

## CSV / serial schema

The firmware emits a header row beginning `timestamp_ms`, then numeric data rows.
Consumers key off `line.startswith("timestamp_ms")` (header) and `line[0].isdigit()`
(data). Same column order is used for the onboard flash log.

| Column | Unit / values | Meaning |
|--------|---------------|---------|
| `timestamp_ms` | ms | Milliseconds since boot (`millis()`) |
| `distance_mm` | mm (blank = none) | VL53L4CX time-of-flight distance |
| `alert` | 0 / 1 | 1 when an object is within `NEAR_MM` (200 mm) |
| `heading_deg` | 0–360° | Compass heading (0 = North) |
| `cardinal` | N…NW | Heading as a cardinal label |
| `accel_x` / `accel_y` / `accel_z` | m/s² | Acceleration per axis (~9.8 on the up axis at rest) |
| `gps_fix` | 0 / 1 | 1 when the GPS has a fix |
| `lat` / `lon` | decimal degrees | Position (blank until fix) |
| `alt_m` | m | Altitude (blank until fix) |
| `speed_kmh` | km/h | Ground speed (blank until fix) |
| `sats` | count | Satellites used (blank until fix) |
| `thermal_min` / `thermal_ctr` / `thermal_max` / `thermal_mean` | °C | MLX90640 surface temps (coldest / center / hottest / mean of 768 px) |
| `hotspot_delta` | °C | `thermal_max` − ambient air temp |
| `co2_ppm` | ppm (blank = none) | SCD-40 CO₂ (SCD-40-only; blank when absent) |
| `air_temp_c` | °C | Ambient **air** temperature — BME688 when present, else SCD-40, else blank |
| `humidity_pct` | %RH | Relative humidity — BME688 when present, else SCD-40, else blank |
| `pressure_hpa` | hPa (blank = none) | BME688 barometric pressure |
| `gas_ohms` | Ω (blank = none) | BME688 gas-sensor resistance (VOC proxy; lower = more VOC) |

`thermal_*` are *surface* temps (the IR camera); `air_temp_c`/`humidity_pct`/`co2_ppm`
are *air*. `air_temp_c` and `humidity_pct` are **shared semantic slots**: whichever
climate sensor is present fills them, the **BME688 taking precedence over the SCD-40**
(it responds faster and lacks the SCD-40's photoacoustic self-heating offset).
`pressure_hpa`/`gas_ohms` are BME688-only; `co2_ppm` is SCD-40-only. A field is blank
when no present sensor supplies it — consumers key on the *column*, never on which chip
produced it, so a SCD-40 ↔ BME688 swap never moves a field. (SCD-40 warms up ~5 s and
updates ~every 5 s, blank until then.)

Serial output modes (toggle live by sending a byte): `h` human · `c` CSV · `b` both
(default; the logger requests `b`). Onboard-flash control bytes: `L` list · `P` dump · `E` erase.

## BLE GATT

Device name `ShintaiOS`. Service `12345678-1234-1234-1234-123456789abc`. Every
characteristic is `READ | NOTIFY`. All but one carry a **UTF-8 string** (no binary
packing); the exception is **Thermal Grid**, which carries a packed **binary**
payload (see [Thermal Grid](#thermal-grid-binary) below).

| Characteristic | UUID | Example payload |
|----------------|------|-----------------|
| Distance | `abcd1234-ab12-ab12-ab12-abcdef123456` | `1234 mm` / `no reading` |
| Alert | `abcd5678-ab12-ab12-ab12-abcdef123456` | `CLOSE` (edge-triggered; no explicit clear) |
| Heading | `abcd9012-ab12-ab12-ab12-abcdef123456` | `169.0° S` |
| Accelerometer | `abcdef12-ab12-ab12-ab12-abcdef123456` | `X:1.8 Y:0.0 Z:9.8` |
| GPS | `abcd3456-ab12-ab12-ab12-abcdef123456` | `37.12345,-122.12345 12m 3.4km/h` |
| Thermal | `abcd6789-ab12-ab12-ab12-abcdef123456` | `Ctr:23.1 Min:22.6 Max:31.4C` |
| Climate | `abcdba98-ab12-ab12-ab12-abcdef123456` | `23.0C 41%RH 750ppm` |
| Environment | `abcdc0de-ab12-ab12-ab12-abcdef123456` | `1007.2hPa 84200ohm 22.8C 39%RH` |
| Thermal Grid | `abcd7890-ab12-ab12-ab12-abcdef123456` | 68-byte **binary** heat grid (see below) |

To receive notifications a central must write `ENABLE_NOTIFICATION` to each
characteristic's CCCD. **The CCCD UUID is the Bluetooth Base UUID
`00002902-0000-1000-8000-00805f9b34fb`** — note the `8000`, not `0000`; the one-char
typo makes `getDescriptor()` return null and silently kills every subscription.

### Thermal Grid (binary)

Added by **Metsuke** (`specs/zokyo/metsuke.md`) — the **first binary characteristic**
and the first field to touch this contract (every other characteristic is a UTF-8
string). It streams a downsampled MLX90640 heat image for the glasses to render as a
false-colour panel, at the camera's **~2 Hz**, **only while a central is subscribed**.

Payload — **68 bytes, little-endian**:

| Offset | Type | Field | Meaning |
|--------|------|-------|---------|
| 0 | `int16` | `min_dC` | min cell temperature ×10 (°C·10, signed) |
| 2 | `int16` | `max_dC` | max cell temperature ×10 (°C·10, signed) |
| 4 | `64 × uint8` | `cells` | row-major 8×8 grid, `cell = round((t − min)/(max − min) × 255)` |

- The 32×24 frame is block-averaged on-device to **8×8** (each cell = mean of its
  4×3 source block, NaN pixels skipped). `min`/`max` are the **grid's** range, so the
  cells span the full palette; the consumer auto-ranges the palette to `min_dC`/`max_dC`.
- **68 bytes exceeds the default 20-byte ATT payload**, so the central must negotiate a
  larger **MTU** (~185+; the apps request 247). The string characteristics never needed
  this; Thermal Grid does.
- **Not logged** — this grid is BLE-live-only; it is **not** in the CSV schema or the
  flash log. The summary `thermal_*` columns remain the logged thermal representation.
- CCCD gotcha still applies (the `8000` above).

A characteristic only notifies when its sensor is present and has data (e.g. Climate
warms up ~5 s and updates every ~5 s; Thermal needs the MLX90640 attached).

**Consumer coverage.** There are two Android consumers, and they cover the GATT table
differently by design (see `android/`):

- **Operator** (`com.saboteur.shintaioperator`, the phone field console) subscribes to
  the **eight string** characteristics, Environment included — the complete numeric readout —
  **plus Thermal Grid** (the Metsuke heat panel). Nine channels: the full-fidelity console.
- **Glass** (`com.saboteur.shintaiglass`, the RayNeo X3 Pro HUD) subscribes to **eight** —
  seven string channels plus **Thermal Grid**, and deliberately skips **Environment**
  (`abcdc0de`, BME688 pressure + gas): the glanceable overlay keeps its readout surface lean,
  and pressure/VOC belong on the full-fidelity phone.

Both apps render the Metsuke heat panel (`THERMAL_GRID`) — the one binary channel — from the
shared `:core` parse + ironbow palette; only the surrounding surface differs (waveguide HUD vs
phone console). Thermal Grid is kept out of `ShintaiGatt.ALL` (the string set), so each app
appends it explicitly.

Both apps share one mirror of this table — `ShintaiGatt` in the `:core` module — so the
subset each subscribes to is a per-app choice, never a second source of truth.
