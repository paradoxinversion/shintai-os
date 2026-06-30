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
| `co2_ppm` | ppm (blank = none) | SCD-40 CO₂ |
| `air_temp_c` | °C | SCD-40 ambient **air** temperature |
| `humidity_pct` | %RH | SCD-40 relative humidity |

`thermal_*` are *surface* temps (the IR camera); `air_temp_c`/`humidity_pct`/`co2_ppm`
are *air* (the SCD-40, updates ~every 5 s, blank for the first few seconds after boot).

Serial output modes (toggle live by sending a byte): `h` human · `c` CSV · `b` both
(default; the logger requests `b`). Onboard-flash control bytes: `L` list · `P` dump · `E` erase.

## BLE GATT

Device name `ShintaiOS`. Service `12345678-1234-1234-1234-123456789abc`. Every
characteristic is `READ | NOTIFY` and carries a **UTF-8 string** (no binary packing).

| Characteristic | UUID | Example payload |
|----------------|------|-----------------|
| Distance | `abcd1234-ab12-ab12-ab12-abcdef123456` | `1234 mm` / `no reading` |
| Alert | `abcd5678-ab12-ab12-ab12-abcdef123456` | `CLOSE` (edge-triggered; no explicit clear) |
| Heading | `abcd9012-ab12-ab12-ab12-abcdef123456` | `169.0° S` |
| Accelerometer | `abcdef12-ab12-ab12-ab12-abcdef123456` | `X:1.8 Y:0.0 Z:9.8` |
| GPS | `abcd3456-ab12-ab12-ab12-abcdef123456` | `37.12345,-122.12345 12m 3.4km/h` |
| Thermal | `abcd6789-ab12-ab12-ab12-abcdef123456` | `Ctr:23.1 Min:22.6 Max:31.4C` |
| Climate | `abcdba98-ab12-ab12-ab12-abcdef123456` | `23.0C 41%RH 750ppm` |

To receive notifications a central must write `ENABLE_NOTIFICATION` to each
characteristic's CCCD. **The CCCD UUID is the Bluetooth Base UUID
`00002902-0000-1000-8000-00805f9b34fb`** — note the `8000`, not `0000`; the one-char
typo makes `getDescriptor()` return null and silently kills every subscription.

A characteristic only notifies when its sensor is present and has data (e.g. Climate
warms up ~5 s and updates every ~5 s; Thermal needs the MLX90640 attached).
