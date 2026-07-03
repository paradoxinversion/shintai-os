# Shintai-OS — Android consumers

Two Kotlin/Compose apps that consume the Shintai-OS board (`shintai-os.ino`) over
BLE, plus a shared library that holds the one Kotlin mirror of the data contract.

```
android/
  core/       :core     — ShintaiGatt (UUIDs) · ShintaiBleClient (transport) · Units · Readings+fold. No UI.
  glass/      :glass     — RayNeo X3 Pro HUD          (com.saboteur.shintaiglass)
  operator/   :operator  — phone field console        (com.saboteur.shintaioperator)
```

Both apps depend on `:core`, so the GATT contract can never drift between them —
change a UUID once in `ShintaiGatt` and both consumers move together. Which subset
of characteristics an app subscribes to is a per-app choice (see below), never a
second source of truth. `:core` is also where the [`CONTRACT.md`](../CONTRACT.md)
GATT table is mirrored and checked by `tools/check-contract.py`.

## The two apps

|                    | **Glass** (`:glass`)                          | **Operator** (`:operator`)                              |
| ------------------ | --------------------------------------------- | ------------------------------------------------------- |
| Role               | Glanceable AR HUD on the RayNeo X3 Pro        | Stable full-fidelity phone console + standalone fallback |
| Find the board     | **Hardcoded MAC** (the glasses' radio starves a scan) | **Scans & pairs** (the phone's radio is dependable) |
| Channels           | Seven (skips **Environment** to stay lean)    | **All eight**, Environment included — the complete readout |
| Extras             | Stereo/mono split, volume-key IPD tuning      | CSV recording, rolling-history sparklines, motion-tracker gauge |
| Look               | Phosphor on **pure black** (waveguide) — strokes only, no fills | Phosphor on **VOID** — panels/charts/log per [`docs/style.md`](../docs/style.md) |
| Permissions        | `BLUETOOTH_CONNECT`                           | `BLUETOOTH_CONNECT` + `BLUETOOTH_SCAN` (`neverForLocation`) |

They are **complementary**: run both and the glasses give you the heads-up
glance while the phone is the control-and-record surface; run only the phone and
it stands alone. The phone was originally a debugging surface for the glasses —
this split makes it a first-class app.

### Glass — set the MAC (required)

The glasses never scan, so set `DEVICE_ADDRESS` in
[`ShintaiViewModel.kt`](glass/src/main/java/com/saboteur/shintaiglass/ShintaiViewModel.kt)
to your board's MAC. Find the address by looking for the device advertising as
**`ShintaiOS`** (nRF Connect on a phone, or `bluetoothctl` → `scan on`). ESP32 BLE
MACs are static, so you set this once. (The Operator app scans instead, so it needs
no hardcoded MAC — but it remembers the last board for a one-tap reconnect.)

### Operator — recordings

The Operator records the live BLE stream to a CSV in its app-specific external
files dir (no storage permission needed), named `shintai_ble_<timestamp>.csv`.
Note this captures the **BLE channels** (a phone wall-clock plus each channel's raw
payload), *not* the firmware CSV schema in `CONTRACT.md` — BLE is a lossy per-sensor
summary, so a column-exact firmware CSV can't be reconstructed from it. The header
is honest about that: `wall_ms,distance,alert,heading,accel,gps,climate,thermal,environment`.

### Fonts (shared from :core)

The four SIL Open Font License (OFL 1.1) faces `docs/style.md §3` calls for are
bundled **once, in `:core`** (`core/src/main/res/font/`) and referenced by both
apps via `com.saboteur.shintai.core.R.font.*`, so there's no duplication or
license drift. The license texts + an attribution `NOTICE.txt` ship in the APK
under `core/src/main/assets/licenses/`. All are bundled unmodified with their
Reserved Font Names intact.

- **Operator** uses all four: Michroma (titles), IBM Plex Mono (workhorse), DSEG7
  Classic (the 7-seg range hero), VT323 (console log).
- **Glass** uses only Michroma + IBM Plex Mono + DSEG — the waveguide caveat
  (§3) bars pixel fonts (VT323 turns to mush on the optics).

Eurostile Bold Extended — style.md's *paid* ideal title face — is deliberately
**not** bundled (proprietary); Michroma fills that role.

## Why no scanning on the glasses

The X3 Pro's own BLE stack keeps the radio busy enough to starve a normal
`startScan()` — the QT Py advertisement often never surfaces. So `:glass`
**never scans**: it connects straight to the hardcoded MAC with
`getRemoteDevice(MAC).connectGatt(autoConnect = false, …)` — a fast, deterministic
DIRECT connect (on this radio `autoConnect = true` often never reports
`STATE_CONNECTED`). A direct connect won't retry on its own, so
`ShintaiBleClient.reconnectSoon` re-fires it after a disconnect. The phone has no
such problem, so `:operator` scans normally.

## Build & install

The Gradle wrapper is checked in (Gradle 8.9); the build needs JDK 17+ — Android
Studio's bundled JBR works. `build.sh` points `JAVA_HOME` at it so Gradle runs
headless (override with `SHINTAI_JAVA_HOME`).

```sh
android/build.sh                        # assembleDebug + lint — builds BOTH apps + :core
android/build.sh :glass:assembleDebug   # just the glasses app
android/build.sh :operator:assembleDebug # just the phone app
android/build.sh detekt                 # Kotlin static analysis (config: android/detekt.yml)

# install a specific app
adb install -r glass/build/outputs/apk/debug/glass-debug.apk
adb install -r operator/build/outputs/apk/debug/operator-debug.apk
```

Or open `android/` in Android Studio (Ladybug+), pick the `glass` or `operator`
run configuration, and Run.

## Layout

- **`:core`** — `ShintaiGatt.kt` (UUIDs + `ALL` characteristic list),
  `ShintaiBleClient.kt` (connect-by-MAC, MTU 247, serialized CCCD subscribes,
  cache refresh), `Readings.kt` (`ShintaiReadings` + the shared `fold` parser +
  `NEAR_MM`), `Units.kt` (metric→imperial at display time), plus the shared
  brand fonts + licenses (`res/font/`, `assets/licenses/`).
- **`:glass`** — `MainActivity.kt` (aspect-ratio stereo/mono split, volume-key IPD,
  the waveguide HUD), `GlassTheme.kt` (pure-black phosphor tokens + fonts),
  `ShintaiViewModel.kt` (**edit the MAC here**; subscribes to seven channels).
- **`:operator`** — `MainActivity.kt` (scan+connect permissions),
  `OperatorViewModel.kt` (scan, all-eight subscribe, recording, history),
  `ShintaiScanner.kt`, `TelemetryRecorder.kt`, `OperatorScreen.kt` +
  `ui/` (the phosphor instrument components + motion-tracker gauge),
  `OperatorTheme.kt` (style.md tokens).
