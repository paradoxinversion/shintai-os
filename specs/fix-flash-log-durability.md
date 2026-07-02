# Fix — durable untethered flash logging

*Make field (untethered) flash logs survive a power cut, so they're always listable and pullable.*

**Status:** fix spec (unbuilt) · **Target:** `firmware/shintai-os/shintai-os.ino` · **Seam:** [CONTRACT.md](../CONTRACT.md) (no change)

> Specs live on `research-development-ichi`. Build from this file on a later branch.

## What broke

A real field session — the rig on phone power, watched live over BLE on the glasses — produced
**no pullable log**. Diagnosis over serial: the FFat flash reported **`0 file(s), 2912256 bytes
used / 3796992 total`** — ~2.9 MB of data allocated, but **zero listable files**. `listLogs()` /
`dumpAllLogs()` / `shintaipull` all saw nothing to pull.

The data wasn't gone — it was **orphaned**: allocated clusters with no directory entry pointing at
them. It was recovered by dumping the raw FFat partition (`esptool read-flash 0x450000 0x3B0000`)
and carving the plaintext CSV back out — **27,989 rows across 29 sessions** (an entire road-trip's
worth of untethered runs, all orphaned the same way). This fix stops the orphaning at the source.

## Root cause

Untethered logging opens the session file **once** at boot and appends to it, flushing per row, but
**never `close()`s** it — the session just ends when field power is cut.

- `openNewLog()` (`shintai-os.ino:90`) does `FFat.open(logPath, FILE_WRITE)` once.
- The `loop()` append (`shintai-os.ino:421`) does `logFile.println(row); logFile.flush();` while
  `!Serial`.

On the ESP32 Arduino FAT layer, **`close()` (→ FatFs `f_close`) is what writes the directory entry**
(the file's existence, start cluster, and size) and syncs the FAT to flash. Per-row `flush()` moves
data *clusters* out to flash as the file grows (which is why 2.9 MB was used), but it does **not**
reliably commit the *directory entry* before power loss. Never closing + a hard power cut = data
clusters on the chip, no directory entry → **lost chain, 0 files listed**. The incident is the proof:
2.9 MB used, every directory entry absent.

## The fix

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
