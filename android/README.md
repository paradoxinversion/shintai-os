# Shintai Glass — RayNeo X3 Pro BLE central

A Kotlin/Compose Android app that connects to the Shintai-OS board
(`shintai-os.ino`) as a BLE **central** and renders its live readout on the
RayNeo X3 Pro glasses.

## Why no scanning

The X3 Pro's own BLE stack keeps the radio busy enough to starve a normal
`startScan()` — the QT Py advertisement often never surfaces. So this app
**never scans**. It connects straight to a hardcoded MAC with
`getRemoteDevice(MAC).connectGatt(autoConnect = false, …)` — a fast, deterministic
DIRECT connect (on this radio `autoConnect = true` often never reports
`STATE_CONNECTED`). A direct connect won't retry on its own, so the client
re-fires it after a disconnect (`ShintaiBleClient.reconnectSoon`). That also means
it needs only `BLUETOOTH_CONNECT` — no `BLUETOOTH_SCAN`, no location permission.

## Set the MAC (required)

Set `DEVICE_ADDRESS` in
[`ShintaiViewModel.kt`](app/src/main/java/com/saboteur/shintaiglass/ShintaiViewModel.kt)
to your board's MAC. Find the address by looking for the device advertising as
**`ShintaiOS`** (a scanner app like nRF Connect on a phone, or `bluetoothctl` →
`scan on` on a Linux box). ESP32 BLE MACs are static, so you set this once.

## What it subscribes to

Seven `READ | NOTIFY` characteristics under service
`12345678-1234-1234-1234-123456789abc`, each a UTF-8 string:

| Channel | UUID (`…-ab12-ab12-ab12-abcdef123456`) | Example payload |
|---------|-----------------------------------------|-----------------|
| Distance | `abcd1234` | `1234 mm` / `no reading` |
| Alert | `abcd5678` | `CLOSE` (edge-triggered) |
| Heading | `abcd9012` | `169.0° S` |
| Accelerometer | `abcdef12` | `X:1.8 Y:0.0 Z:9.8` |
| GPS | `abcd3456` | `37.12345,-122.12345 12m 3.4km/h` |
| Climate | `abcdba98` | `23.0C 41%RH 750ppm` |
| Thermal | `abcd6789` | `Ctr:23.1 Min:22.6 Max:31.4C` |

The contract also defines an eighth **Environment** characteristic (`abcdc0de`,
BME688 pressure + gas) that this app does not yet subscribe to — see
[`CONTRACT.md`](../CONTRACT.md).

Notifications are enabled one CCCD-write at a time (Android allows only one GATT
op in flight); see `ShintaiBleClient.subscribeNext`.

The launch screen shows distance large (red `⚠ TOO CLOSE` when ≤ 20 cm, mirroring
the firmware's `NEAR_MM`) plus a mini readout of heading, accel, GPS, climate, and
thermal, with a live connection-state indicator.

## Build & install

The Gradle wrapper is checked in, so no separate Gradle install is needed. The
build needs JDK 17+ — Android Studio's bundled JBR 21 works; point `JAVA_HOME` at
it if your system default is older (`java -version`).

```sh
cd android
./gradlew assembleDebug                # wrapper fetches Gradle 8.9 on first run
adb connect <glasses-ip>               # or USB; RayNeo X3 Pro runs Android
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

Or open `android/` in Android Studio (Ladybug+), let it sync, and Run.

## Layout

- `ShintaiGatt.kt` — UUIDs + the `ShintaiReadings` snapshot model.
- `ShintaiBleClient.kt` — connect-by-MAC, service discovery, serialized
  notification subscriptions, value routing.
- `ShintaiViewModel.kt` — holds the MAC, owns the client, parses payloads into
  state. **Edit the MAC here.**
- `MainActivity.kt` — requests `BLUETOOTH_CONNECT`, connects on launch, draws the
  Compose HUD.
