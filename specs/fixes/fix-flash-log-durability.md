# Fix — reliable untethered flash logs

*Make field (untethered) flash logs actually listable, pullable, and power-cut-safe.*

**Status:** built + hardware-validated on `fix/flash-log-durability` · **Target:** `firmware/shintai-os/shintai-os.ino` · **Seam:** [CONTRACT.md](../../CONTRACT.md) (no change)

> Spec authored on `research-development-ichi`; implemented + tested on `fix/flash-log-durability`.

## What broke

A real field session — the rig on phone power, watched live over BLE on the glasses — produced
**no pullable log**. Over serial the FFat flash reported **`0 file(s), ~2.9 MB used / 3.8 MB
total`**: data allocated, **zero listable files**; `listLogs()` / `dumpAllLogs()` / `shintaipull`
all saw nothing. Investigation on the board revealed **two independent bugs** — one primary, one
secondary.

## Bug 1 (primary) — broken directory enumeration

`listLogs()`, `dumpAllLogs()`, and `eraseLogs()` enumerated the flash via `FFat.open("/")` +
`openNextFile()`. On this ESP32 core (3.3.10), **that root enumeration returns zero entries even for
well-formed, closed files.** Confirmed by an on-board diagnostic:

```
DIAG cur=/sht0026.csv exists=1      # file exists
DIAG openByName ok=1 size=4190      # opens by name, real data
0 file(s), ... bytes used           # yet openNextFile() enumerates nothing
```

So every flash file was invisible to the tooling whether or not it was cleanly written. **This is
why flash logs could never be pulled** — the data was fine; the listing was blind.

## Bug 2 (secondary) — durability on power cut

Independently, untethered logging opened the session file **once** at boot and only `flush()`ed per
row, **never `close()`ing** it (`openNewLog()` ~`:90`; the `loop()` append ~`:421`). On the ESP32
FAT layer, **`close()` (→ `f_close`) is what commits the directory entry** (existence, start
cluster, size); per-row `flush()` pushes data clusters out but does not reliably persist the
directory entry before a hard power cut. Older sessions that lost power mid-write therefore left
**orphaned clusters** (allocated, no directory entry) — ~2.6 MB of the chip. That orphaned data was
recovered by dumping the raw partition (`esptool read-flash 0x450000 0x3B0000`) and carving the
plaintext CSV — **27,989 rows** — into `logs/recovered/`.

## Fix 1 — enumerate by seq-name (the primary fix)

Since `exists()` / open-by-name work perfectly but `openNextFile()` is blind, **enumerate by the
deterministic filenames.** The firmware only ever creates `/shtNNNN.csv` named by the NVS boot
counter, so `listLogs`/`dumpAllLogs`/`eraseLogs` iterate `1..seq`, `FFat.exists()`-check each, and
operate on it — sidestepping the broken core enumeration entirely. Complete for this app (it creates
no other files) and immune to the core bug.

## Fix 2 — close on a cadence (durability hardening)

**Close the log file on a cadence** (and on host-connect), reopening in **append** mode. `close()`
forces the directory entry + FAT to flash, so at any moment the file on disk is complete and
listable; a power cut loses at most the rows written since the last commit.

Two behaviours:

1. **Commit every N rows.** After every `FLASH_COMMIT_ROWS` appended rows, `close()` the file. Reopen
   lazily on the next untethered row with `FILE_APPEND` — **never `FILE_WRITE`**, which is `"w"` and
   would *truncate* the file. Between commits the file is closed and fully persisted.
2. **Commit on host-connect.** When the host connects (`!Serial` → `Serial`, i.e. the untethered→
   tethered transition), `close()` immediately so the just-finished session is listable/pullable the
   moment you plug in and run `shintaipull`. This directly fixes the "watched live, then nothing to
   pull" workflow.

Worst-case loss is bounded to `FLASH_COMMIT_ROWS` rows (≈ `FLASH_COMMIT_ROWS × UPDATE_MS`). At the
default 1500 ms cadence, committing every **16 rows ≈ every 24 s** — negligible flash wear, ≤24 s of
data at risk.

### Code sketch

```cpp
const uint32_t FLASH_COMMIT_ROWS = 16;   // close+commit the dir entry every N rows
uint32_t rowsSinceCommit = 0;

// ── in loop(), replacing the current append block ──
bool untethered = !Serial;
if (fsReady && untethered) {
  if (!logFile) logFile = FFat.open(logPath, FILE_APPEND);   // (re)open at end — NOT FILE_WRITE
  if (logFile) {
    logFile.println(row);
    if (++rowsSinceCommit >= FLASH_COMMIT_ROWS) {
      logFile.close();                     // f_close → directory entry + FAT flushed to chip
      rowsSinceCommit = 0;                 // reopened lazily on the next untethered row
    } else {
      logFile.flush();                     // keep pushing data sectors between commits
    }
  }
} else if (fsReady && !untethered && logFile) {
  logFile.close();                         // host connected: commit + release so it's pullable now
  rowsSinceCommit = 0;
}
```

`openNewLog()` keeps `FILE_WRITE` for the initial create (header line). Only the *reopen* path uses
`FILE_APPEND`. `dumpAllLogs()`/`listLogs()`/`eraseLogs()` are unchanged — they now simply see files
that actually exist.

## Alternatives considered

- **Close after *every* row** — simplest guarantee, but a directory-entry + FAT write per row is slow
  and burns flash write cycles. Rejected: the N-row cadence gives the same durability guarantee with
  a fraction of the wear.
- **Rely on `flush()` / add an explicit `fsync`** — the current (broken) behaviour already flushes;
  whether the Arduino wrapper's `flush()` fsyncs is version-dependent and clearly didn't persist the
  directory entry here. `close()` is the documented guarantee, so use it. Rejected as unreliable.
- **A journaling / append-only record format** — overkill for a CSV field logger; doesn't fit the
  contract or the pull tooling.
- **Detect power-loss and close gracefully** — there's no warning when a USB bank is unplugged; a
  brown-out handler is fragile. The cadence approach needs no power-loss signal.

## Test plan (the crux — this is why we're doing fix + test)

Untethered logging is gated on `!Serial`, so it can't be exercised while tethered. The decisive test
**must** involve real untethered power + a hard cut:

1. **Flash** the fixed firmware (`arduino-cli compile --upload …`, per `CLAUDE.md`).
2. **Power from a battery / USB bank** (no CDC host → `!Serial` → flash logging active). Confirm the
   NeoPixel/telemetry shows it's running.
3. **Let it accumulate** several minutes of rows (well past several commit windows).
4. **Yank power** — do *not* shut down cleanly. This reproduces the field failure.
5. **Connect to the laptop**, send `L` (or run `shintaipull`).
6. **Expect:** the session file **is listed with non-zero size** and pulls cleanly, containing all
   rows **up to within the last ≤16-row commit window**. (Before the fix: 0 files.)
7. **Repeat** with the cut at different times to confirm the loss is always bounded to the last
   commit window, never the whole session.
8. **Host-connect path:** power untethered, log a bit, then plug into the laptop *without* cutting
   power; immediately `L`/`shintaipull` → the session is already closed and pullable (tests the
   transition-close).

### Regression checks

- Tethered live capture (`shintai`) still logs to serial normally; **no** double-write to flash while
  tethered (the `!Serial` gate still holds).
- CSV header/column order unchanged; `CONTRACT.md` untouched.
- `L` / `P` / `E` still behave; dumped files parse under `shintai-pull.py`'s `<<<BEGIN…>>>` framing.
- No new serial output on the telemetry stream; 1500 ms cadence unaffected.

## Reclaiming the already-orphaned space (one-time, after recovery)

The current chip still holds ~2.9 MB of orphaned clusters from the 29 recovered sessions — `FFat`
won't reclaim them automatically. **Only after confirming the recovered data is safely in `logs/`**,
send `E` (erase) — or reflash — to reformat the partition and reclaim the space. Do this deliberately;
it is destructive. (Recovery is already complete: see `logs/recovered/`.)

## Contract impact

**None.** This is an internal durability fix to how rows are persisted. CSV schema, BLE GATT, serial
control bytes, and the pull framing are all unchanged.

## Validated on hardware (fix/flash-log-durability)

Flashed to the QT Py ESP32-S3 and confirmed over serial:

- **Enumeration:** `L` now lists **8 files** (`/sht0020…0027`) where it previously showed `0 file(s)`.
- **Durability:** files written by the fixed firmware (`/sht0026`, `/sht0027`) carry valid directory
  entries with real sizes; the host-connect close commits the active session on plug-in.
- **End-to-end pull:** `shintaipull` recovered all 8 sessions cleanly into `logs/` (66–1453 rows
  each) — no raw-partition carving needed. The field session watched on the glasses (782 rows) came
  back as `…_flash0023.csv`.
- **Still pending — the hard power-cut test:** a genuine mid-session power yank (below) can only be
  done by hand. The commit mechanism is validated; the field power-loss case isn't yet reproduced.

## Acceptance criteria

1. After an untethered run **ended by a hard power cut**, the session file is listed (`L`) with
   non-zero size and pulls via `shintaipull` — never "0 files, N bytes used".
2. Data loss from a power cut is bounded to ≤ `FLASH_COMMIT_ROWS` rows.
3. Plugging in mid/after an untethered run makes the session immediately listable/pullable
   (transition-close works).
4. Reopen uses append: no session file is ever truncated by the commit cycle.
5. Tethered live logging, the CSV schema, and `L`/`P`/`E` are unregressed.

## Follow-ups (separate)

- **HUD can't zoom / no street labels** — the reason the exact session couldn't be visually
  confirmed. `hud.py` bakes a *static* `CartoDB.DarkMatterNoLabels` image, so it neither zooms nor
  labels streets. Options: switch to a labelled basemap (`DarkMatter` with labels), or emit an
  interactive/zoomable map (e.g. the folium `route_map.html` path `analyze.py` already uses). Worth a
  small separate spec.
- **mtime-vs-record-time selection** — hud/analyze pick "newest" by file mtime; flash pulls are
  stamped at pull time, and `timestamp_ms` is millis-since-boot with no wall clock. Newest-by-mtime
  can therefore point at the wrong session. Separate fix.
