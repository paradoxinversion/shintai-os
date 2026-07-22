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
| `distance_l_mm` | mm (blank = none) | Rear **left-half** nearest range — nearest valid zone in the VL53L5CX field's left columns (Zanshin) |
| `distance_r_mm` | mm (blank = none) | Rear **right-half** nearest range — nearest valid zone in the field's right columns (Zanshin) |
| `alert` | 0 / 1 | 1 when the **nearer** half is within `NEAR_MM` (200 mm) |
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
| `lightning_km` | km (0 = none yet) | **Enrai** — most recent lightning strike's estimated distance (AS3935); `1` = overhead, `63` = detected-but-out-of-range, blank when the sensor is absent |
| `lightning_energy` | raw (blank = none) | **Enrai** — most recent strike's raw energy value (AS3935 lightning-energy register; relative, not a physical unit) |
| `lightning_strikes` | count (cumulative) | **Enrai** — cumulative validated strikes since boot (blank when the sensor is absent) |
| `board` | `fwd` / `aft` | **Bunshin** — which pod (role) produced this row; the terminal role discriminator for two-host federation. Absent in pre-Bunshin logs |

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

`board` is added by **Bunshin** (`specs/zokyo/bunshin.md`) — the **first multi-host contract
change**. When two pods run the identical firmware and split the sensor set, each row carries
the role of the pod that produced it (`fwd`/`aft`). Like `steps` it is **appended at the end**
of the schema, so consumers that key on column *names* are unaffected and pre-Bunshin logs
(without the column) still parse. It is the CSV-half discriminator the ground-station uses to
tag and merge the two streams; the full multi-producer model — identity, the per-channel
authority table, and the merge rule — is in [Multi-producer model (Bunshin)](#multi-producer-model-bunshin).

`distance_l_mm` / `distance_r_mm` are the **rear depth field** — **Zanshin (残心)**
(`specs/zokyo/zanshin.md`), superseding Kōei's (`specs/zokyo/koei.md`) two-arc pair. One
**VL53L5CX** 8×8 multizone ToF (at `0x29`, behind the same PCA9546 mux on **ch0**) replaces
the two VL53L4CX arcs: the nearest valid zone in the field's **left columns** fills
`distance_l_mm`, the **right columns** fill `distance_r_mm` (which physical side is which is a
mount detail, validated on-wrist). Each is blank when that half sees no valid target or the
field is absent. `alert` and the on-body Kehai reflex key off the **nearer** half, so the
wearer is warned by whichever side sees the closest object — the **CSV shape is unchanged
from Kōei**, only the source moved from two point beams to one field. The full field is also
streamed live over the **Rear Depth Grid** characteristic (below).

`lightning_km` / `lightning_energy` / `lightning_strikes` are added by **Enrai (遠雷)**
(`specs/zokyo/enrai.md`) — an **AS3935** "Franklin" lightning detector direct on the main I²C
bus at `0x03`. Lightning is **event-based** (strikes are intermittent) while the CSV samples
every tick, so the three columns are a **last-strike snapshot** (`lightning_km`,
`lightning_energy`) plus a **monotonic count** (`lightning_strikes`) — each row reports the most
recent strike and the running total, not an instantaneous reading. They are **appended before
`board`** (which stays the terminal Bunshin discriminator), so consumers that key on column
*names* are unaffected and pre-Enrai logs still parse. The firmware polls the sensor's
interrupt-source register every loop (no IRQ pin wired); a validated strike also notifies the
live **Lightning** characteristic (below). Blank when the AS3935 is absent.

Serial output modes (toggle live by sending a byte): `h` human · `c` CSV · `b` both
(default; the logger requests `b`). Onboard-flash control bytes: `L` list · `P` dump · `E` erase.

## BLE GATT

Device name `ShintaiOS`. Service `12345678-1234-1234-1234-123456789abc`. Every
characteristic is `READ | NOTIFY`. All but two carry a **UTF-8 string** (no binary
packing); the exceptions are **Thermal Grid** and **Rear Depth Grid**, which carry packed
**binary** payloads (see [Thermal Grid](#thermal-grid-binary) and
[Rear Depth Grid](#rear-depth-grid-binary) below).

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
| Lightning | `abcda535-ab12-ab12-ab12-abcdef123456` | `km=1 e=227467 n=8` (last strike distance km · raw energy · cumulative count) |
| Thermal Grid | `abcd7890-ab12-ab12-ab12-abcdef123456` | chunked **binary** 32×24 heat grid (see below) |
| Rear Depth Grid | `abcd5c88-ab12-ab12-ab12-abcdef123456` | 128-byte **binary** 8×8 rear depth field (see below) |

To receive notifications a central must write `ENABLE_NOTIFICATION` to each
characteristic's CCCD. **The CCCD UUID is the Bluetooth Base UUID
`00002902-0000-1000-8000-00805f9b34fb`** — note the `8000`, not `0000`; the one-char
typo makes `getDescriptor()` return null and silently kills every subscription.

### Thermal Grid (binary, chunked)

Added by **Metsuke** (`specs/zokyo/metsuke.md`) — the **first binary characteristic**
and the first field to touch this contract (every other characteristic is a UTF-8
string). It streams the MLX90640 heat image for the glasses/phone to render as a
false-colour panel, at the camera's **~2 Hz**, **only while a central is subscribed**.

The image is the sensor's **full native 32×24 (768 cells)**. 768 bytes + a header
exceeds a single notification even at the largest negotiable MTU, so each frame is sent
as **4 chunks** the consumer reassembles. (This superseded the original single-packet
**16×12** grid — 4× the resolution; the three mirror sites moved together.)

Each notification is one **chunk — 199 bytes, little-endian**:

| Offset | Type | Field | Meaning |
|--------|------|-------|---------|
| 0 | `uint8` | `frame_seq` | frame counter (wraps 0–255); increments once per full 32×24 frame |
| 1 | `uint8` | `chunk_index` | 0 … `chunk_count`−1 |
| 2 | `uint8` | `chunk_count` | chunks per frame (**4**) |
| 3 | `int16` | `min_dC` | frame min cell temperature ×10 (°C·10, signed) — repeated in every chunk |
| 5 | `int16` | `max_dC` | frame max cell temperature ×10 (°C·10, signed) — repeated in every chunk |
| 7 | `192 × uint8` | `cells` | this chunk's slice: rows `[chunk_index·6, +6)` of the row-major 32×24 grid, `cell = round((t − min)/(max − min) × 255)` |

- **Reassembly.** The consumer buffers chunks by `frame_seq`: a chunk whose `frame_seq`
  differs from the buffer starts a fresh frame; when all `chunk_count` chunks of one
  `frame_seq` have arrived, the **32×24** grid is complete (chunk `i` fills rows
  `[i·6, i·6+6)`). An incomplete frame (a dropped chunk) is discarded when the next
  `frame_seq` begins — at ~2 Hz a missed frame is invisible. `min`/`max` are the
  **frame's** range, so the cells span the full palette; the consumer auto-ranges the
  palette to `min_dC`/`max_dC` and **bilinear-upscales** the 32×24 grid for a smooth panel.
- **MTU.** Each 199-byte chunk fits the apps' negotiated **MTU 247** (payload = MTU − 3
  = 244) — the *same* MTU the single-packet grid used. Chunking, not a bigger MTU, is
  what makes full resolution fit.
- **Not logged** — this grid is BLE-live-only; it is **not** in the CSV schema or the
  flash log. The summary `thermal_*` columns remain the logged thermal representation.
- CCCD gotcha still applies (the `8000` above).

### Rear Depth Grid (binary)

Added by **Zanshin (残心)** (`specs/zokyo/zanshin.md`) — the **second binary characteristic**
(after Metsuke's thermal grid) and the sensor behind the rear `distance_l/r_mm` + `alert`. It
streams the VL53L5CX's **8×8 (64-zone)** rear depth field for the glasses/phone to render as a
rear depth panel, at the sensor's **~15 Hz**, **only while a central is subscribed**.

Payload — **128 bytes, little-endian**, one notification (fits MTU 247, no chunking):

| Offset | Type | Field | Meaning |
|--------|------|-------|---------|
| `2·z` | `uint16` | `zone[z]` | row-major 8×8 zone distance in **mm** (`z = row·8 + col`); **0 = no valid target** |

- A zone packs its range only when the VL53L5CX `target_status` is a **valid code (5 or 9)**;
  otherwise it packs **0** (a real ToF range is never 0). The consumer maps near→warm,
  far→cool, `0`→blank, and may bilinear-upscale the 8×8 for a smooth panel.
- The same field derives `distance_l_mm` / `distance_r_mm` on-device (nearest valid zone in the
  left / right columns) and the `alert` / Kehai reflex (nearest zone overall) — so the string
  `Distance` / `Alert` characteristics and the CSV are **unchanged**; this grid is the live
  full-resolution companion, like Metsuke's thermal grid beside the `thermal_*` columns.
- **Not logged** — BLE-live-only; not in the CSV schema or the flash log.
- CCCD gotcha still applies (the `8000` above).

### Lightning (string)

Added by **Enrai (遠雷)** (`specs/zokyo/enrai.md`) — the AS3935 lightning detector's live
channel. Payload is the UTF-8 string **`km=<d> e=<energy> n=<count>`** (e.g. `km=1 e=227467
n=8`): the most recent strike's distance (km; `1` = overhead, `63` = out of range), its raw
energy, and the cumulative strike count since boot — the BLE mirror of the
`lightning_km`/`lightning_energy`/`lightning_strikes` CSV columns. Unlike the periodic string
channels it notifies **event-driven** — once per validated strike, the instant the firmware's
poll catches it — so a consumer can flash on the edge. It notifies only while the AS3935 is
present. (No IRQ pin is wired; the firmware polls the sensor's interrupt-source register every
loop.)

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
  the **ten string** characteristics, Environment, Hokan and Lightning included — the complete numeric readout —
  **plus Thermal Grid and Rear Depth Grid** (the Metsuke heat panel + the Zanshin rear depth panel).
  Twelve channels: the full-fidelity console. It renders the full Enrai readout (distance · energy ·
  count) and flashes on each new strike.
- **Glass** (`com.saboteur.shintaiglass`, the RayNeo X3 Pro HUD) subscribes to **all ten string
  channels plus Thermal Grid and Rear Depth Grid** (twelve, same as Operator), but treats **Environment** (`abcdc0de`,
  BME688 pressure + gas) specially: it takes the channel **only to derive Kyūkaku's smell SPIKE
  badge** from `gas_ohms` (`:core` `Kyukaku.kt`), and does **not** render the raw pressure/VOC
  readout — that full clean/taint/foul readout stays on the phone. The overlay keeps its *displayed*
  surface lean; a transient chemical spike is a heads-up worth the waveguide, the numbers aren't.
  **Lightning** it surfaces lean too: the nearest-strike distance with a flash on the edge — a
  storm overhead is worth the waveguide.

Both apps render the Metsuke heat panel (`THERMAL_GRID`) — the one binary channel — from the
shared `:core` parse + ironbow palette; only the surrounding surface differs (waveguide HUD vs
phone console). Thermal Grid is kept out of `ShintaiGatt.ALL` (the string set), so each app
appends it explicitly.

Both apps share one mirror of this table — `ShintaiGatt` in the `:core` module — so the
subset each subscribes to is a per-app choice, never a second source of truth.

## Multi-producer model (Bunshin)

Until **Bunshin** (`specs/zokyo/bunshin.md`) there was one board — "*the* board produces."
Bunshin runs the **identical firmware on two hosts** that split the sensor set by what's
physically plugged into each, and the consumers **federate the two streams into one**.
Nothing above changes shape: both pods expose the *same* GATT service and the *same* CSV
schema, and each only fills the channels its sensors supply (the presence-driven blanking
already specified above). The additions are **identity** and a **merge rule**.

### Pods & identity

Two roles: **`fwd`** (forward / head-side) and **`aft`** (pack-side). Each pod stores its
role in NVS and advertises as **`ShintaiOS-<role>`** (`ShintaiOS-fwd` / `ShintaiOS-aft`);
the BLE **service UUID is identical** on both — a central tells the pods apart by the
**name**, never by service. In the CSV / flash log each row carries its origin in the
appended **`board`** column. A pod's role is set live over serial (`'R'`) and applied at
its next boot.

### Authority table (default precedence)

When both pods supply the **same** channel, the consumer resolves it by a **per-channel
precedence order**. This table is the **default**, shared by every consumer (the Android
`:core` merge, the glasses, and the ground-station); Operator can **override** any row live
(see below).

| Channel(s) | Default precedence | Rationale |
|---|---|---|
| `distance_l_mm` / `distance_r_mm` / `alert` + Rear Depth Grid | **aft** → fwd | Rear field (Zanshin) lives on the pack |
| `heading_deg` / `cardinal` | **fwd** → aft | HUD wants head orientation |
| `accel_x` / `accel_y` / `accel_z` | **fwd** → aft | Head IMU |
| `thermal_*` + Thermal Grid | **fwd** → aft | Forward-looking thermal |
| `co2_ppm` / `air_temp_c` / `humidity_pct` / `pressure_hpa` / `gas_ohms` | **aft** → fwd | Air chem rides the pack |
| `lightning_*` + Lightning (Enrai) | **aft** → fwd | Ambient storm sense; rides the pack with air chem (direction-agnostic) |
| `gps_fix` / `lat` / `lon` / `alt_m` / `speed_kmh` / `sats` | **fwd** → aft (fix-gated) | A pod with no fix supplies nothing |
| `steps` (Hokan) | **aft** → fwd | Torso pedometer beats head-bob |

**Merge rule (one sentence):** for each channel, filter to the pods currently supplying a
**valid** value (non-blank / GPS fix / non-null), then take the highest in that channel's
precedence order — so a preferred-but-absent pod never wins over a present one.

**Runtime override (Operator).** The precedence order is a **default, not a fixed law**: the
Operator app exposes a per-channel control to pick which pod wins each *contested* channel,
persisted on the phone and applied live. The override is **Operator-local** in v1; the
glasses and the ground-station use the defaults above.

**Clocks.** `timestamp_ms` is each pod's own `millis()` since *its* boot — the two pods
share no clock. Consumers align on arrival, not on `timestamp_ms`: the phones on notify
receipt, the ground-station on **host wall-clock** at capture.
