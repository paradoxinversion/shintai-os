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
| `distance_l_mm` | mm (blank = none) | Rear-**left** VL53L4CX time-of-flight distance (mux ch0) |
| `distance_r_mm` | mm (blank = none) | Rear-**right** VL53L4CX time-of-flight distance (mux ch1) |
| `alert` | 0 / 1 | 1 when the **nearer** arc is within `NEAR_MM` (200 mm) |
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
| `steps` | count (cumulative) | Hokan pedometer — cumulative step count since boot, detected live from the LSM6DSOX (0 when the IMU is absent) |

`thermal_*` are *surface* temps (the IR camera); `air_temp_c`/`humidity_pct`/`co2_ppm`
are *air*. `air_temp_c` and `humidity_pct` are **shared semantic slots**: whichever
climate sensor is present fills them, the **BME688 taking precedence over the SCD-40**
(it responds faster and lacks the SCD-40's photoacoustic self-heating offset).
`pressure_hpa`/`gas_ohms` are BME688-only; `co2_ppm` is SCD-40-only. A field is blank
when no present sensor supplies it — consumers key on the *column*, never on which chip
produced it, so a SCD-40 ↔ BME688 swap never moves a field. (SCD-40 warms up ~5 s and
updates ~every 5 s, blank until then.)

`steps` is added by **Hokan** (`specs/zokyo/hokan.md`) — the **first CSV-half contract
change** (Metsuke changed the BLE half). It's a cumulative pedometer count detected live
on-device from the IMU, **appended at the end** of the schema so consumers that key on
column *names* (and the `line[0].isdigit()` framing) are unaffected; old logs without the
column still parse. It's the logged basis for the ground-station's GPS-denied
dead-reckoned path (`Δsteps × step_length @ heading_deg`). CSV-only — there is no `steps`
GATT characteristic in v1.

`distance_l_mm` / `distance_r_mm` are the **dual rear arc** — **Kōei (後衛)**
(`specs/zokyo/koei.md`) — two VL53L4CX behind a PCA9546 I²C mux (both report at `0x29`;
the mux isolates them onto separate channels,
**ch0 = left, ch1 = right**, recorded in [`REGISTRY.md`](REGISTRY.md)). They replace the
former single `distance_mm` column. Each arc is independent and blank when its channel
has no target or its sensor is absent (non-fatal per channel — a missing arc never
blanks the other). `alert` and the on-body Kehai reflex both key off the **nearer** of
the two arcs, so the wearer is warned by whichever beam sees the closest object.

Serial output modes (toggle live by sending a byte): `h` human · `c` CSV · `b` both
(default; the logger requests `b`). Onboard-flash control bytes: `L` list · `P` dump · `E` erase.

## BLE GATT

Device name `ShintaiOS`. Service `12345678-1234-1234-1234-123456789abc`. Every
characteristic is `READ | NOTIFY`. All but one carry a **UTF-8 string** (no binary
packing); the exception is **Thermal Grid**, which carries a packed **binary**
payload (see [Thermal Grid](#thermal-grid-binary) below).

| Characteristic | UUID | Example payload |
|----------------|------|-----------------|
| Distance | `abcd1234-ab12-ab12-ab12-abcdef123456` | `L:1234 R:1180 mm` / `L:-- R:1180 mm` (per-arc `--` = no target) |
| Alert | `abcd5678-ab12-ab12-ab12-abcdef123456` | `CLOSE` (edge-triggered; no explicit clear) |
| Heading | `abcd9012-ab12-ab12-ab12-abcdef123456` | `169.0° S` |
| Accelerometer | `abcdef12-ab12-ab12-ab12-abcdef123456` | `X:1.8 Y:0.0 Z:9.8` |
| GPS | `abcd3456-ab12-ab12-ab12-abcdef123456` | `37.12345,-122.12345 12m 3.4km/h` |
| Thermal | `abcd6789-ab12-ab12-ab12-abcdef123456` | `Ctr:23.1 Min:22.6 Max:31.4C` |
| Climate | `abcdba98-ab12-ab12-ab12-abcdef123456` | `23.0C 41%RH 750ppm` |
| Environment | `abcdc0de-ab12-ab12-ab12-abcdef123456` | `1007.2hPa 84200ohm 22.8C 39%RH` |
| Hokan | `abcdf007-ab12-ab12-ab12-abcdef123456` | `1240 98.5 112` (cumulative `steps` · `heading_deg` · `cadence` steps/min) |
| Thermal Grid | `abcd7890-ab12-ab12-ab12-abcdef123456` | 196-byte **binary** heat grid (see below) |

To receive notifications a central must write `ENABLE_NOTIFICATION` to each
characteristic's CCCD. **The CCCD UUID is the Bluetooth Base UUID
`00002902-0000-1000-8000-00805f9b34fb`** — note the `8000`, not `0000`; the one-char
typo makes `getDescriptor()` return null and silently kills every subscription.

### Thermal Grid (binary)

Added by **Metsuke** (`specs/zokyo/metsuke.md`) — the **first binary characteristic**
and the first field to touch this contract (every other characteristic is a UTF-8
string). It streams a downsampled MLX90640 heat image for the glasses to render as a
false-colour panel, at the camera's **~2 Hz**, **only while a central is subscribed**.

Payload — **196 bytes, little-endian**:

| Offset | Type | Field | Meaning |
|--------|------|-------|---------|
| 0 | `int16` | `min_dC` | min cell temperature ×10 (°C·10, signed) |
| 2 | `int16` | `max_dC` | max cell temperature ×10 (°C·10, signed) |
| 4 | `192 × uint8` | `cells` | row-major 16×12 grid, `cell = round((t − min)/(max − min) × 255)` |

- The 32×24 frame is block-averaged on-device to **16×12** (each cell = mean of its
  2×2 source block, NaN pixels skipped) — the sensor's native 4:3 aspect. `min`/`max`
  are the **grid's** range, so the cells span the full palette; the consumer auto-ranges
  the palette to `min_dC`/`max_dC` and **bilinear-upscales** the grid for a smooth panel.
- **196 bytes far exceeds the default 20-byte ATT payload**, so the central must
  negotiate a larger **MTU** — it needs at least ~199, and the apps request **247**
  (payload = MTU − 3 = 244, so the whole grid lands in one notification). The string
  characteristics never needed this; Thermal Grid does.
- **Not logged** — this grid is BLE-live-only; it is **not** in the CSV schema or the
  flash log. The summary `thermal_*` columns remain the logged thermal representation.
- CCCD gotcha still applies (the `8000` above).

The **Hokan** characteristic is added by `specs/zokyo/hokan.md` — the **second BLE-half
addition** (after Metsuke's binary grid) and the only *string* characteristic beyond the
original set. It streams the live pedometer state — cumulative `steps`, current
`heading_deg`, and `cadence` (steps/min) — which both apps integrate over time
(`Δsteps × step_length @ heading`) into a **dead-reckoned breadcrumb mini-map**, the live
on-glasses twin of the base-side path `groundstation/analyze.py` draws from the CSV. It
notifies only while the IMU is present. (The CSV `steps` column remains the logged basis;
this characteristic is the live-only companion — the two halves of Hokan's output.)

A characteristic only notifies when its sensor is present and has data (e.g. Climate
warms up ~5 s and updates every ~5 s; Thermal needs the MLX90640 attached; Hokan needs the IMU).

**Consumer coverage.** There are two Android consumers, and they cover the GATT table
differently by design (see `android/`):

- **Operator** (`com.saboteur.shintaioperator`, the phone field console) subscribes to
  the **nine string** characteristics, Environment and Hokan included — the complete numeric readout —
  **plus Thermal Grid** (the Metsuke heat panel). Ten channels: the full-fidelity console.
- **Glass** (`com.saboteur.shintaiglass`, the RayNeo X3 Pro HUD) subscribes to **all nine string
  channels plus Thermal Grid** (ten, same as Operator), but treats **Environment** (`abcdc0de`,
  BME688 pressure + gas) specially: it takes the channel **only to derive Kyūkaku's smell SPIKE
  badge** from `gas_ohms` (`:core` `Kyukaku.kt`), and does **not** render the raw pressure/VOC
  readout — that full clean/taint/foul readout stays on the phone. The overlay keeps its *displayed*
  surface lean; a transient chemical spike is a heads-up worth the waveguide, the numbers aren't.

Both apps render the Metsuke heat panel (`THERMAL_GRID`) — the one binary channel — from the
shared `:core` parse + ironbow palette; only the surrounding surface differs (waveguide HUD vs
phone console). Thermal Grid is kept out of `ShintaiGatt.ALL` (the string set), so each app
appends it explicitly.

Both apps share one mirror of this table — `ShintaiGatt` in the `:core` module — so the
subset each subscribes to is a per-app choice, never a second source of truth.
