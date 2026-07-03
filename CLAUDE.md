# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Shintai-OS is a wearable multi-sensor platform on an **Adafruit QT Py ESP32-S3**.
The project is three modules that meet at one seam — the data contract in
[`CONTRACT.md`](CONTRACT.md):

- `firmware/shintai-os/` — the ESP32-S3 sketch (Shintai-OS core): reads sensors and
  publishes them three ways (serial CSV, onboard FFat flash, BLE notify).
- `groundstation/` — the Python consumer: capture + analysis over serial/flash.
- `android/` — the BLE consumers: a Gradle project with a shared `:core` module (the
  one Kotlin mirror of the contract + BLE transport) and **two** Kotlin/Compose apps —
  `:glass` (RayNeo X3 Pro HUD) and `:operator` (phone field console).

The board produces; the two consumers only ever meet it at the contract. The
data flow is the architecture:

```
firmware/shintai-os ──USB serial (CSV+human)──▶ groundstation/shintai-logger.py ──▶ logs/*.csv
       │                                                                                  │
       ├── onboard FFat flash (untethered) ──groundstation/shintai-pull.py (dump)──▶ logs/*.csv
       │                                                                                  │
       │                                                  groundstation/{hud,analyze}.py ─┘ ──▶ analysis/
       └── BLE GATT (notify) ──▶ android :core ──┬─▶ :glass  app (RayNeo X3 Pro HUD)
                                                  └─▶ :operator app (phone field console)
```

## Per-module guidance

Each module has its own focused `CLAUDE.md` — commands, layout, and invariants live
there. Read the one for the seam you're working in:

- [`firmware/CLAUDE.md`](firmware/CLAUDE.md) — the ESP32-S3 sketch (Arduino/`verify.sh`).
- [`groundstation/CLAUDE.md`](groundstation/CLAUDE.md) — the Python capture + analysis pipeline.
- [`android/CLAUDE.md`](android/CLAUDE.md) — the two Kotlin/Compose apps + `:core` (see also `android/README.md`).

## The contract seam (cross-cutting — read before touching any module's schema)

- **`CONTRACT.md` is the source of truth.** The CSV column schema and the BLE GATT
  characteristics are defined there; the firmware `CSV_HEADER`, the Kotlin UUIDs in
  `android/core/…/ShintaiGatt.kt` (the shared `:core` module — both apps import it),
  and any hardcoded column names in `groundstation/` all mirror it. Change a field →
  edit `CONTRACT.md` first, then those three mirror sites.
- **BLE CCCD gotcha:** the notify-enable descriptor UUID is
  `00002902-0000-1000-8000-00805f9b34fb` — the `8000` (not `0000`) matters; the typo
  silently kills all notifications. See `CONTRACT.md`.

**Checks I can run without hardware** (wire these into pre-commit / CI):

```sh
python3 tools/check-contract.py   # assert CONTRACT.md == firmware CSV_HEADER == Kotlin UUIDs == groundstation cols
firmware/verify.sh                # firmware compiles
android/build.sh detekt lint      # Kotlin compiles + lints
```

`tools/check-contract.py` is stdlib-only and catches the drift the invariants warn
about — a renamed/reordered CSV column, a mistyped GATT UUID, and the CCCD
`8000`-vs-`0000` typo. Run it before editing any of the three mirror sites.

These are wired into a **scope-aware pre-commit hook** (`tools/hooks/pre-commit`):
the contract linter runs on every commit; `verify.sh` runs only when `firmware/`
changed; strict `detekt` (fails on issues not in each module's `detekt-baseline.xml`)
runs only when `android/` changed. Enable once per clone — it's local git config:

```sh
git config core.hooksPath tools/hooks   # enable the hook (already set on this machine)
git commit --no-verify                  # bypass it for a single commit
```
