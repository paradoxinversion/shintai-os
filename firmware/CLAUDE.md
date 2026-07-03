# firmware/ — Shintai-OS core (ESP32-S3 sketch)

The producer. Reads sensors and publishes them three ways: serial CSV, onboard FFat
flash, and BLE notify. Everyone downstream meets it at [`../CONTRACT.md`](../CONTRACT.md)
— see the root [`CLAUDE.md`](../CLAUDE.md) for the contract seam and the hardware-free
checks.

## Commands

The `.ino` lives at `firmware/shintai-os/shintai-os.ino` — Arduino requires the sketch
file to sit in a folder of the same name, so the compile target is the **directory**
`firmware/shintai-os`. The board + all library versions are pinned in
`firmware/shintai-os/sketch.yaml` (profile `qtpy`), so use the profile rather than the
long flag incantation:

```sh
firmware/verify.sh            # compile-check, NO hardware needed (do this after every .ino edit)
firmware/verify.sh --upload   # compile + flash to the board on $SHINTAI_PORT (default /dev/cu.usbmodem101)
```

`firmware/i2c-scan/` is a standalone bus-scan sketch for bringing up new sensors — not
part of the shipped firmware.

## Layout (`firmware/shintai-os/shintai-os.ino`)

Sensors over I²C: VL53L4CX (distance), LSM6DSOX (accel) + LIS3MDL (mag/heading),
Adafruit GPS, MLX90640 (thermal cam, 768 px), SCD-40 (CO₂/air temp/humidity). Each
is presence-gated (`*Present` flags) and warns-and-continues if absent — a latent
per-sensor module boundary. Also exposes everything over **BLE** (one characteristic
per sensor group). `Preferences` holds a persistent boot counter naming each flash
file `/shtNNNN.csv`. `setup()` brings up sensors + BLE + FFat; `loop()` reads, prints
(per output mode), BLE-notifies, and — when untethered — appends a flushed flash row.
Flash helpers: `listLogs()`, `dumpAllLogs()`, `eraseLogs()`.

## Invariants (easy to break)

- **`CSV_HEADER` mirrors `../CONTRACT.md`.** The column schema is defined in the
  contract; this header is one of its three mirror sites. Change a field → edit
  `CONTRACT.md` first, then this header. Run `python3 tools/check-contract.py`.
- **CSV framing.** The sketch emits a `timestamp_ms,...` header then numeric rows;
  consumers key off `line.startswith("timestamp_ms")` and `line[0].isdigit()`.
- **Serial modes** `h`/`c`/`b` (human/csv/both): the logger sends `b\n` on connect
  to get both streams + a fresh header. Flash control chars on the same line:
  `L` list, `P` dump, `E` erase.
- **Untethered flash logging is gated on `!Serial`** (`CDCOnBoot=cdc` makes this
  work), so tethered live sessions don't double-write. The partition is **FFat**
  (stock tinyuf2 3.7 MB FAT) — *not* LittleFS, despite `shintai-pull.py`'s docstring.
- **The dump framing is a contract with `shintai-pull.py`**: `<<<BEGIN name size>>>`
  … `<<<END>>>`, `<<<DONE>>>`, `<<<NOFS>>>`, `<<<ERASED…>>>`. Don't change it without
  updating the puller.
