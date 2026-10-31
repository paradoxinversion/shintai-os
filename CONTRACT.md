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
| `pm1_0_ugm3` | µg/m³ (blank = none) | **Enmu** — PM1.0 mass concentration (PMSA003I, ambient/`_env` scale) |
| `pm2_5_ugm3` | µg/m³ (blank = none) | **Enmu** — PM2.5 mass concentration; the health-relevant fraction, and the one the glasses surface |
| `pm10_ugm3` | µg/m³ (blank = none) | **Enmu** — PM10 mass concentration |
| `nfc_uid` | hex string (blank = no tag) | **Fuda** — UID of the ISO14443A tag last read by the PN532 (e.g. `04A1B2C3`); held ~5 s after a read, then blank |
| `nfc_type` | string (blank = no tag) | **Fuda** — RF family of that tag (`ISO14443A`); blank alongside `nfc_uid` when no tag |
| `presence_detected` | 0 / 1 (blank = sensor absent) | **Hitokage** — 1 when the XM125 radar currently detects something in range, else 0 |
| `presence_distance_mm` | mm (blank = not detected / absent) | **Hitokage** — the NEAREST detected range (500–5000 mm in distance mode, 200–2000 mm in presence-mode fallback); blank when `presence_detected` is 0 |
| `light_lux` | lux (blank = none) | **Hizashi** — ambient illuminance computed by the TSL2591 from its visible + IR channels |
| `light_ir` | raw counts (blank = none) | **Hizashi** — the TSL2591's raw **infrared** channel; paired with `light_lux` it separates daylight from artificial light |
| `board` | `fwd` / `aft` / `gnd` | **Bunshin** — which producer (role) emitted this row; the terminal role discriminator. `fwd`/`aft` are the two **body** pods; `gnd` is a stationary **desk station** that is deliberately NOT merged with them (see below). Absent in pre-Bunshin logs |

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

`pm1_0_ugm3` / `pm2_5_ugm3` / `pm10_ugm3` are added by **Enmu (煙霧)**
(`specs/zokyo/enmu.md`) — a **PMSA003I** laser particulate counter direct on the main I²C bus
at `0x12` (no mux). Unlike Enrai these are ordinary periodic scalars: three mass
concentrations in µg/m³, sampled like any other air channel. They report the sensor's
**ambient** (`_env`) values rather than the standard-particle (`_standard`) set — the
atmospheric figures, which is what an on-body reading means. Like Enrai's, they are
**appended before `board`** (which stays the terminal Bunshin discriminator), so pre-Enmu
logs still parse and name-keyed consumers are unaffected. Blank when the PMSA003I is absent.

`nfc_uid` / `nfc_type` are added by **Fuda (札)** (`specs/zokyo/fuda.md`) — the **NFC / RFID
identity sense** over the PN532 ([Adafruit #364](REGISTRY.md#sensors), I²C `0x24` — the PN532's
**7-bit** address; `0x24 = 0x48 >> 1`, *not* `0x12`, which is a double-shift artifact, and `0x12`
is Enmu's address besides — on the plain bus, no mux). When an ISO14443A tag/card enters the
reader's RF field its UID is read and published as a **hex string** (`nfc_uid`, e.g. `04A1B2C3`)
with the tag's RF family (`nfc_type`, `ISO14443A`); the pair is **held ~5 s** after the last read
so a brief tap is logged across a couple of cycles, then both blank. Like the `lightning_*` and
`pm*` columns they are **appended before `board`**, so pre-Fuda logs still parse and name-keyed
consumers are unaffected. They are **string** columns — like `cardinal` and `board` — so the
numeric-row framing (`line[0].isdigit()`, which only tests `timestamp_ms`) is untouched. Fuda is
the platform's first *identity* sense: it reports *who/what*, not a physical scalar.

`presence_detected` / `presence_distance_mm` are added by **Hitokage (人影)**
(`specs/zokyo/hitokage.md`) — an **Acconeer XM125** pulsed coherent radar
([SparkFun SEN-24540](REGISTRY.md#sensors), I²C `0x52`, plain bus, no mux). Unlike every other
sense here, the XM125 does its own DSP on a co-packaged MCU running **one of two onboard
applications**; the firmware is a register-protocol I²C client to whichever is flashed, not a
raw-sensor driver. The **I2C Distance Detector** app is the real default (tried first at boot):
it reports up to 9 simultaneous reflection peaks (range + relative signal strength), so these two
columns are the **nearest** peak's presence/range — the full list streams live on the
[Radar Peaks](#ble-gatt) characteristic. The **I2C Presence Detector** app (one detected flag,
one range, no peak list) is a **fallback**, attempted only when distance-mode setup fails — a
module still flashed with the older presence firmware. Either way it cannot count or distinguish
multiple *people* — a peak is a reflector, not an identity (see
[Decisions](specs/zokyo/hitokage.md#decisions)). Like `nfc_uid`/`nfc_type` they are **appended
before `board`**, so pre-Hitokage logs still parse and name-keyed consumers are unaffected. Blank
when the XM125 is absent.

`light_lux` / `light_ir` are added by **Hizashi (日差し)** — the **ambient-light sense** over a
**TSL2591** high-dynamic-range light sensor. Unlike every other sensor in this contract the
TSL2591's I²C address `0x29` is **fixed and unstrappable**, and it is the *same* address as the
VL53L5CX rear field — the collision the PCA9546 mux already exists to resolve. Hizashi therefore
sits **behind the mux on ch1** (Zanshin holds ch0), and the firmware selects its channel before
every access, exactly as the rear field does. On a bench rig with no mux the firmware falls back
to the plain bus (`hizashiDirect`), which is safe only because the ToF is then necessarily absent.

`light_lux` is the sensor's **computed illuminance** from the visible and IR channels;
`light_ir` is the **raw IR channel** alongside it, because the visible/IR ratio — not the lux
figure — is what separates daylight from incandescent from LED. Like the `lightning_*`, `pm*`,
`nfc_*` and `presence_*` columns they are **appended before `board`** (which stays the terminal
Bunshin discriminator), so pre-Hizashi logs still parse and name-keyed consumers are unaffected.
Both are blank when the TSL2591 is absent. The TSL2591 saturates in direct sun at the default
gain/integration; a saturated reading is reported as-is rather than clamped or blanked, since a
blank already means "no sensor".

Serial output modes (toggle live by sending a byte): `h` human · `c` CSV · `b` both
(default; the logger requests `b`). Onboard-flash control bytes: `L` list · `P` dump · `E` erase.

**Unit report (`U`) — which sensors actually initialized.** The board answers with
`<<<UNITS>>>`, one `key=0|1` line per unit, then `<<<END UNITS>>>`. This is the
*authoritative* presence answer and is deliberately **not** the same question as the `I`
bus scan: a part behind the PCA9546 is invisible to a scan (only the mux answers), two
parts can share one address, and an address that ACKs may still fail `begin()` — a `0x77`
that ACKs while every register reads `0x00` is an address **collision**, not a working
BME688. Keys (`mux`, `rear`, `imu`, `mag`, `gps`, `thermal`, `air`, `nose`, `particulate`,
`nfc`, `radar`, `light`, `lightning`, `gesture`, `pane`, `andon`, plus the strap companions
`imu_alt` / `mag_alt`) are **append-only and never
renamed**, and a consumer must **ignore keys it does not know** so an older ground-station
still parses a newer board. A board that does not answer the framing at all is older
firmware without the command; `groundstation/bootroll.py` falls back to the `I` scan.

**Strap companions.** `imu_alt` and `mag_alt` are not units — they annotate `imu` and
`mag`, reporting `1` when that part answered on its **alternate** strap address (LSM6DSOX
`0x6b` rather than `0x6a`; LIS3MDL `0x1e` rather than `0x1c`). They carry the same
`key=0|1` shape as every other line so the framing stays one form. They are meaningless
when the part is absent. This is the only place the strap is observable: the `setup()`
`[OK]` banner also says it, but that line cannot be read on this board (native-USB
re-enumeration outlasts the serial wait).

## BLE GATT

Device name `ShintaiOS`. Service `12345678-1234-1234-1234-123456789abc`. Every
characteristic is `READ | NOTIFY`; **Lightning Control** additionally has `WRITE` — the
one **app→board** path (a central writes a tuning token; see [Lightning Control](#lightning-control-writable)).
All but two carry a **UTF-8 string** (no binary packing); the exceptions are **Thermal Grid**
and **Rear Depth Grid**, which carry packed **binary** payloads (see
[Thermal Grid](#thermal-grid-binary) and [Rear Depth Grid](#rear-depth-grid-binary) below).

| Characteristic | UUID | Example payload |
|----------------|------|-----------------|
| Distance | `abcd1234-ab12-ab12-ab12-abcdef123456` | `L:1234 R:1180 mm` / `L:-- R:1180 mm` (per-arc `--` = no target) |
| Alert | `abcd5678-ab12-ab12-ab12-abcdef123456` | `CLOSE` (edge-triggered; no explicit clear) |
| Heading | `abcd9012-ab12-ab12-ab12-abcdef123456` | `169.0° S` |
| Accelerometer | `abcdef12-ab12-ab12-ab12-abcdef123456` | `X:1.8 Y:0.0 Z:9.8` |
| GPS | `abcd3456-ab12-ab12-ab12-abcdef123456` | `37.12345,-122.12345 12m 3.4km/h` — or `no fix sats=0` |
| Thermal | `abcd6789-ab12-ab12-ab12-abcdef123456` | `Ctr:23.1 Min:22.6 Max:31.4C` |
| Climate | `abcdba98-ab12-ab12-ab12-abcdef123456` | `23.0C 41%RH 750ppm` |
| Environment | `abcdc0de-ab12-ab12-ab12-abcdef123456` | `1007.2hPa 84200ohm 22.8C 39%RH` |
| Hokan | `abcdf007-ab12-ab12-ab12-abcdef123456` | `1240 98.5 112` (cumulative `steps` · `heading_deg` · `cadence` steps/min) |
| Particulate | `abcdd057-ab12-ab12-ab12-abcdef123456` | `PM1.0:12 PM2.5:34 PM10:56 ug/m3` (**Enmu** — the three mass concentrations; `d057` = "dust") |
| Fuda | `abcdfada-ab12-ab12-ab12-abcdef123456` | `UID:04A1B2C3 ISO14443A` (**Fuda** — the NFC tag last read; **event-driven**, notifies only on a read) |
| Presence | `abcd0125-ab12-ab12-ab12-abcdef123456` | `present=1 dist=260mm` / `present=0 dist=--` (**Hitokage** — the XM125's detected/not-detected flag + nearest range; `0125` = XM125) |
| Radar Peaks | `abcdd1a6-ab12-ab12-ab12-abcdef123456` | `n=2 0:2894/-7029 1:4809/-3865` (**Hitokage** — the full multi-peak list, distance mode only; see below) |
| Hizashi | `abcd2591-ab12-ab12-ab12-abcdef123456` | `lux=412.5 ir=57` (**Hizashi** — ambient illuminance + raw IR; `2591` = TSL2591) |
| Pane Control | `abcd1306-ab12-ab12-ab12-abcdef123456` | **WRITE** a token → pages the OLED pane; notifies `pane=2/5 NAV` (see below; `1306` = SSD1306) |
| Lightning | `abcda535-ab12-ab12-ab12-abcdef123456` | `km=1 e=227467 n=8` (last strike distance km · raw energy · cumulative count) |
| Lightning Control | `abcda53c-ab12-ab12-ab12-abcdef123456` | **WRITE** a token → applies AS3935 tuning; notifies `gain=out spike=2 wdog=1 tune=0` (see below) |
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

### Radar Peaks (string)

Added by **Hitokage (人影)** (`specs/zokyo/hitokage.md`) — the XM125's full multi-peak list, the
live companion to the `presence_detected`/`presence_distance_mm` CSV columns' nearest-peak
summary (the same relationship Metsuke's Thermal Grid has to `thermal_*`, or Zanshin's Rear Depth
Grid has to `distance_l/r_mm`). Payload is the UTF-8 string **`n=<count> i:<mm>/<strength> ...`**
(e.g. `n=2 0:2894/-7029 1:4809/-3865`): the number of detected peaks, then each as
`index:range_mm/strength` — up to 9, in the detector's own index order (NOT sorted by range).
`strength` is the XM125's raw relative signal value, not a physical unit (dB or otherwise); more
positive / less negative reads as a stronger reflection, but it can't be compared across sensors
or profiles. Notifies **only while distance mode is active** — a module running the fallback
presence-detector firmware has no peak-list concept, so this characteristic simply stays silent
(the `Presence` characteristic above still works either way). Not logged — BLE-live-only, no CSV
column. Each peak is a **reflector**, not a person: two people at the same range read as one peak,
and a strong single reflector can produce more than one peak (multipath) — see
[Decisions](specs/zokyo/hitokage.md#decisions).

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

### Lightning Control (writable)

Added by **Enrai (遠雷)** (`specs/zokyo/enrai.md`) — the platform's **first writable
characteristic** (`READ | WRITE | NOTIFY`), the one **app→board** path in an otherwise
read-only contract. A central **writes a UTF-8 command token** to tune the AS3935 live (the
sensor's distance is a coarse storm-front energy estimate — see the spec); the firmware applies
it and **notifies the new config** back so the app can reflect it. The same tokens are on the
firmware's serial knobs (`o`/`s`/`S`/`w`/`W`/`y`/`Y`/`x`).

| Write token | Effect |
|-------------|--------|
| `gain` | toggle AFE gain INDOOR ↔ OUTDOOR (OUTDOOR = lower gain, spreads the distance estimate) |
| `spike+` / `spike-` | spike rejection 1–11 (stricter waveform validation) |
| `wdog+` / `wdog-` | watchdog threshold 1–10 (sensitivity vs disturber rejection) |
| `tune+` / `tune-` | antenna tuning cap 0–15 (empirical — no IRQ wired to measure the LCO) |
| `clear` | clear the storm statistics (reset the distance estimate) |

**Read / notify payload** — the current config as space-separated `key=value`:
`gain=out spike=2 wdog=1 tune=0` (`gain` ∈ `in`/`out`). Notified after every applied write **and
once per telemetry cycle**, like Pane Control — a change-only channel gives a central no way to
learn the initial config, since nothing notifies on connect and the Android client subscribes
without ever issuing a GATT read.

When **no AS3935** answered at `0x03` the payload is `enrai=none`, not a config: reporting the
defaults these globals happen to hold would light the console's tuning LED and offer knobs that
drive nothing. Consumers parse `enrai=none` as unknown and hide the tuning panel.

An unknown token is ignored, and the board re-asserts the true config afterwards — the BLE stack
stores written bytes into the characteristic, so without that a rejected token would be left there
and the next read would return the token instead of the config. CCCD gotcha still applies (the
`8000` above) for the notify half.

The **Hokan** characteristic is added by `specs/zokyo/hokan.md` — the **second BLE-half
addition** (after Metsuke's binary grid) and the only *string* characteristic beyond the
original set. It streams the live pedometer state — cumulative `steps`, current
`heading_deg`, and `cadence` (steps/min) — which both apps integrate over time
(`Δsteps × step_length @ heading`) into a **dead-reckoned breadcrumb mini-map**, the live
on-glasses twin of the base-side path `groundstation/analyze.py` draws from the CSV. It
notifies only while the IMU is present. (The CSV `steps` column remains the logged basis;
this characteristic is the live-only companion — the two halves of Hokan's output.)

The **Fuda** characteristic is added by `specs/zokyo/fuda.md` — a plain UTF-8 string carrying the
last-read NFC tag (`UID:<hex> <type>`, the BLE-live twin of the `nfc_uid` / `nfc_type` CSV columns).
Unlike the continuous senses it is **event-driven**: it notifies only when the PN532 actually reads
a tag, not every cycle, so a central sees a scan as it happens rather than a value re-sent through
the ~5 s hold. (Enrai's Lightning is the other event-driven channel; the two differ in that a
strike is a *world* event and a tag read is a *deliberate* one.) The Operator console shows the
full `UID + type`; the Glass HUD surfaces the **UID alone** — the lean-surface rule: the identity
is the heads-up, the RF family is console detail.

### Pane Control (writable)

The **Pane Control** characteristic is the **second app→board path** (after Lightning Control) and
the first that drives an *output* surface rather than tuning a sensor. It pages the tertiary OLED
pane (SSD1306 @ `0x3D`) — the same carousel the APDS9960's swipe gesture drives on-body.

A central **writes** one token:

| Token | Effect |
| --- | --- |
| `next` / `prev` | step the carousel one pane, wrapping |
| `home` · `nav` · `env` · `prox` · `therm` | jump straight to that pane |

It **notifies** the resulting state as `pane=<n>/<count> <NAME>` (e.g. `pane=2/5 NAV`) after every
change — including when the **gesture** pages the pane, not only on a write. The board is the single
source of truth for which pane is showing, so a console that only tracked its own writes would drift
out of sync the moment someone swiped.

It also notifies **once per telemetry cycle**, like the sensor channels. That is not redundancy: a
change-only channel gives a central no way to learn the *initial* state — nothing notifies on
connect, and the Android client subscribes without ever issuing a GATT read — so a console gating
its paging control on "have I seen a pane?" would wait forever for a value that needed the control
to exist first.

When **no panel** answered at `0x3D` the payload is `pane=none`, not a pane number: reporting `HOME`
for a screen that doesn't exist would repeat the mistake the GPS channel used to make. Consumers
parse `pane=none` as unknown and hide the paging control. The characteristic itself is always built,
and a write is a no-op without a panel — the same present-gating Lightning Control uses for a
missing AS3935.

An unknown token is ignored, but the board still re-asserts the true state afterwards: the BLE stack
stores written bytes into the characteristic, so without that a rejected token would be left sitting
there and the next read would return the token instead of the pane.

Note this channel is **per-pod**, not merged like a sensor: each pod drives its own panel, and a
central writing it addresses every pod it is connected to.

### Hizashi (string)

Added by **Hizashi (日差し)** — the TSL2591 ambient-light channel, the BLE-live twin of the
`light_lux` / `light_ir` CSV columns. Payload is the UTF-8 string **`lux=<illuminance> ir=<raw>`**
(e.g. `lux=412.5 ir=57`): the computed illuminance and the raw infrared count. It notifies
**periodically**, every telemetry cycle, like Particulate and Presence rather than event-driven
like Fuda and Lightning — ambient light is a continuous scalar. It notifies only while the
TSL2591 is present.

**Both apps consume it**, and they split it the way the lean-surface rule dictates: Operator shows
the illuminance **and** the raw IR channel (the visible/IR ratio is the daylight-vs-incandescent-vs-LED
comparison, which is console work), while Glass shows the **lux alone** — the same treatment PM2.5 and
the bare tag UID get. Unlike Particulate and Presence the payload is **parsed** in `:core`
(`HizashiState`) rather than held raw, because both apps want the lux as a number.

Operator also records `light_lux` / `light_ir` to its own capture CSV, blank when the sensor is
unknown — a recorded `0` lux would be a reading, not an absence.

A characteristic only notifies when its sensor is present and has data (e.g. Climate
warms up ~5 s and updates every ~5 s; Thermal needs the MLX90640 attached; Hokan needs the IMU;
Particulate needs the PMSA003I; Fuda notifies only when the PN532 reads a tag; Presence needs
the XM125; Radar Peaks needs the XM125 **and** distance mode specifically — it stays silent in
the presence-mode fallback).

**Consumer coverage.** There are two Android consumers, and they cover the GATT table
differently by design (see `android/`):

- **Operator** (`com.saboteur.shintaioperator`, the phone field console) subscribes to
  **every one of the seventeen string** characteristics — the whole of `ShintaiGatt.ALL`, Environment,
  Hokan, Lightning, Particulate, Fuda, Presence, Radar Peaks, Hizashi and Pane Control included — the
  complete readout — **plus Thermal Grid and Rear Depth Grid** (the Metsuke heat panel + the Zanshin
  rear depth panel). **Nineteen channels**: the full-fidelity console. It renders the full Enrai readout (distance · energy ·
  count) and flashes on each new strike, the full Enmu triad (PM1.0 · PM2.5 · PM10), Fuda's
  `UID + type` in a dedicated **Identity** panel — its own category, being the first channel that
  reports *who/what* rather than a physical scalar — and Hitokage's detected/nearest-range summary
  **plus** the full Radar Peaks list, rendered as an A-scope (range/strength, no angle — the sensor
  has none).
- **Glass** (`com.saboteur.shintaiglass`, the RayNeo X3 Pro HUD) subscribes to **fourteen string
  channels plus Thermal Grid and Rear Depth Grid** (sixteen). This is the first place the two apps'
  *subscription* sets genuinely differ rather than just their rendering. Glass omits three of
  `ShintaiGatt.ALL`: **Pane Control** (`abcd1306`), because paging the board's OLED is a console
  action and the HUD has no panel to drive; **Radar Peaks** (`abcdd1a6`) — a peak list has no
  HUD-appropriate rendering, unlike Thermal Grid/Rear Depth Grid's visual panels, so Glass keeps the
  lean `Presence` summary alone; and **Lightning Control** (`abcda53c`), the AS3935 tuning channel,
  which is an app→board control surface and belongs on the console with the other knobs.
  It also treats **Environment** (`abcdc0de`,
  BME688 pressure + gas) specially: it takes the channel **only to derive Kyūkaku's smell SPIKE
  badge** from `gas_ohms` (`:core` `Kyukaku.kt`), and does **not** render the raw pressure/VOC
  readout — that full clean/taint/foul readout stays on the phone. The overlay keeps its *displayed*
  surface lean; a transient chemical spike is a heads-up worth the waveguide, the numbers aren't.
  **Lightning** it surfaces lean too: the nearest-strike distance with a flash on the edge — a
  storm overhead is worth the waveguide. **Particulate** gets the same treatment: the HUD shows
  **PM2.5 alone** — the health-relevant fraction — while the full PM1.0/2.5/10 triad stays on
  the phone. **Fuda** likewise: the tag **UID alone**, without the RF family. **Hizashi** the same way — the
  **lux alone**, since the raw IR channel is only meaningful as a comparison against the visible one,
  and that is console work. **Presence**
  (Hitokage) it renders too — a single-line detected/range readout, the same payload as the
  console since there's no raw-vs-derived split to lean out of.

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

Three roles: **`fwd`** (forward / head-side), **`aft`** (pack-side), and **`gnd`** (a
stationary desk station). Each stores its role in NVS and advertises as
**`ShintaiOS-<role>`** (`ShintaiOS-fwd` / `ShintaiOS-aft` / `ShintaiOS-gnd`); the BLE
**service UUID is identical** on all — a central tells them apart by the **name**, never
by service. In the CSV / flash log each row carries its origin in the appended **`board`**
column. A role is set live over serial (`'R'`, which cycles `fwd → aft → gnd`) and applied
at the next boot. (`'G'` is *not* the ground command — that byte is the thermal-grid probe.)

**`gnd` is a producer, not a pod.** It runs the identical firmware and fills the same
channels, but it senses a **different place**: the room, not the wearer. Bunshin's merge
is only meaningful because `fwd` and `aft` observe the *same* environment from two mount
points — folding a desk station in would assert the room's CO₂, light or temperature as
the body's. So **`gnd` is excluded from the authority table and from every merged
channel**, including the GPS no-fix fallback and the packet heartbeat. It remains visible
as a connected producer (`perBoard` in the Kotlin merge) so a consumer can render it on
its own surface, and its rows are logged under `board=gnd` like any other. The Operator
console shows it in a dedicated **Ground Station** panel with no precedence controls; the
Glass HUD ignores it entirely.

Consumers must key the role off the three-letter **token** (`fwd`/`aft`/`gnd`), never a
Kotlin enum name — `Role.Ground.name.lowercase()` is `"ground"`, which is not the wire
value. `:core` exposes `Role.token` and `roleFromToken()` for exactly this.

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
| `pm*_ugm3` + Particulate (Enmu) | **aft** → fwd | Air chem rides the pack; the air you breathe is direction-agnostic |
| `nfc_uid` / `nfc_type` + Fuda | **fwd** → aft | You scan the tag you're facing — head-side |
| `presence_detected` / `presence_distance_mm` + Presence + Radar Peaks (Hitokage) | **fwd** → aft | Forward presence rides the head — you face what you're scanning for |
| `gps_fix` / `lat` / `lon` / `alt_m` / `speed_kmh` / `sats` | **fwd** → aft (fix-gated) | A pod with no fix supplies nothing — but it still *notifies* `no fix sats=N`, so consumers must gate on the payload, not on silence |
| `light_lux` / `light_ir` + Hizashi | **fwd** → aft | Light is *not* direction-agnostic the way air chem is: a pack sensor reads the wearer's own shadow, the head reads the light actually being looked at |
| `steps` (Hokan) | **aft** → fwd | Torso pedometer beats head-bob |

**Merge rule (one sentence):** for each **body** pod (`gnd` is filtered out first), filter to those currently supplying a
**valid** value (non-blank / GPS fix / non-null), then take the highest in that channel's
precedence order — so a preferred-but-absent pod never wins over a present one.

**GPS is the one channel where silence and "no value" differ.** The firmware notifies the
GPS characteristic whenever the *receiver* is present, fix or not, so:

| On the air | Means | Merge treats it as |
| --- | --- | --- |
| (nothing ever sent) | no receiver on this pod | not supplying |
| `no fix sats=N` | receiver present, no lock | not supplying (but rendered as "searching") |
| `<lat>,<lon> <alt>m <speed>km/h` | fix | supplying |

If no pod has a fix, the merged value falls back to any pod's `no fix` report, so the UI
can distinguish "still searching" from "no GPS hardware" — which a bare dash cannot.

**Runtime override (Operator).** The precedence order is a **default, not a fixed law**: the
Operator app exposes a per-channel control to pick which pod wins each *contested* channel,
persisted on the phone and applied live. The override is **Operator-local** in v1; the
glasses and the ground-station use the defaults above.

**Clocks.** `timestamp_ms` is each pod's own `millis()` since *its* boot — the two pods
share no clock. Consumers align on arrival, not on `timestamp_ms`: the phones on notify
receipt, the ground-station on **host wall-clock** at capture.
