# groundstation/ — Python consumer (capture + analysis)

Consumes the board three ways: **USB serial**, **dumped onboard flash**, and — since the
BLE central — **BLE** (wireless, untethered, via `shintai-ble.py`). The board's schema is
defined in [`../CONTRACT.md`](../CONTRACT.md); any hardcoded column names here mirror it.
See the root [`CLAUDE.md`](../CLAUDE.md) for the contract seam. The live/web tooling
(`console.py`, `storm_radar.py`, the boot ritual, the restyled HUD) follows the
cassette-futurism style guide in [`../docs/style.md`](../docs/style.md).

## Commands

No build/test/lint. Four zsh launchers here (each `source`s miniconda +
`conda activate shintai`, then runs the matching `.py` with `python`, **not**
`python3` — a pyenv shim shadows the conda env's pyserial). Shell aliases (`shintai`,
`shintailog`, `shintaipull`, `shintaicsv`) in `~/.zshrc` point at the original launchers;
`ble.sh` and the `.py` servers below are run directly. `logs/` and `analysis/` are resolved
`__file__`-relative, so they live under `groundstation/`.

```sh
groundstation/start.sh   # shintai     — live capture: serial → segmented panel + logs/*.csv
groundstation/ble.sh     # (no alias)  — wireless capture (bleak BLE): BLE → logs/shintai_ble_*.csv
groundstation/hud.sh     # shintailog  — build + open analysis/hud.html for newest log
groundstation/pull.sh    # shintaipull — dump onboard flash logs into logs/
python groundstation/console.py            # live web "Sulaco" console: tail newest log → 127.0.0.1:8138
python groundstation/storm_radar.py [--serve]   # Enrai stormscope → analysis/storm.html (--serve = live, :8137)
python groundstation/analyze.py            # stitch ALL sessions → combined.csv + pngs + route_map.html
```

## Layout

- `shintai-logger.py` — serial → CSV writer. In a TTY it opens with the boot roll-call
  ritual, then paints an in-place **segmented instrument console** (`dashboard()`, via
  `bootroll`); piped, it falls back to plain `[#N logged]` lines.
- `shintai-ble.py` (`ble.sh`) — **BLE central** (bleak), the wireless transport. Scans
  `ShintaiOS-<role>`, subscribes to the `:core` string chars, and reconstructs the
  firmware-schema CSV into `logs/shintai_ble_<time>.csv` — a **lossy per-sensor view** (no
  `sats`/`thermal_mean`), not the column-exact flash log; reconnects on drop.
- `shintai-pull.py` — dumps onboard flash over framed serial.
- `hud.py` — newest log → self-contained offline Plotly HUD (`analysis/hud.html`):
  GPS route on a base64-baked CartoDB DarkMatter basemap (via `contextily`) + every
  sensor on a synced time axis, restyled to `docs/style.md`. **Degrades gracefully** to a
  tileless route offline.
- `console.py` — live web **"Sulaco" console** (`docs/style.md` §8): a stdlib server tails the
  newest log and serves the dashboard — Enrai stormscope + segmented RANGE/CLIMATE/THERMAL/
  AIR (+ Kyūkaku smell)/NAV panels + a panel show/hide modal — at `127.0.0.1:8138`, updating
  live. Reads `shintai_log_*` / `shintai_ble_*` / `spidey_log_*`.
- `storm_radar.py` — the **Enrai stormscope**, the signature motion-tracker (`docs/style.md`
  §5.1), for the strike log (`lightning-*.csv`): a self-contained radar → `analysis/storm.html`;
  `--serve` makes it live (:8137). `console.py` embeds the same scope as one panel.
- `bootroll.py` — the shared **cassette-futurism style module** (`docs/style.md`): the ANSI
  palette + `seg_bar`/`meter`/`banner` + the boot roll-call ritual, imported by
  `shintai-logger.py`; `python bootroll.py` runs a standalone demo.
- `analyze.py` — stitches *all* sessions into one absolute timeline
  (`file_start + timestamp_ms`), reports gaps + headline stats, writes `combined.csv`,
  `timeseries.png`, `route.png`, `route_map.html`.
- `analysis/tour/` is a scratch pile of one-off exploration scripts — not the pipeline.

## Invariants (easy to break)

- **CSV framing.** The sketch emits a `timestamp_ms,...` header then numeric rows;
  key off `line.startswith("timestamp_ms")` (header) and `line[0].isdigit()` (data).
- **Serial modes** `h`/`c`/`b` (human/csv/both): the logger sends `b\n` on connect to
  get both streams + a fresh header. Flash control chars on the same line: `L` list,
  `P` dump, `E` erase.
- **`shintai-pull.py` parses framed serial**: `<<<BEGIN name size>>>` … `<<<END>>>`,
  `<<<DONE>>>`, `<<<NOFS>>>`, `<<<ERASED…>>>`. Pulled files become
  `shintai_log_<pulltime>_flashNNNN.csv` so the glob picks them up. (The docstring says
  LittleFS; the board actually uses FFat — the puller doesn't care.)
- **The `shintai_log_*.csv` glob is load-bearing.** `hud.py`/`analyze.py` glob it (plus
  legacy `spidey_log_*`) and skip files under `MIN_ROWS` (50) data rows; `console.py` also
  globs `shintai_ble_*` (wireless captures). The stormscope reads a separate `lightning-*.csv`
  strike log (written by `tools/lightning-logger.py`).
- **Close the Arduino IDE Serial Monitor before running any Python tool** — the
  serial port can't be shared (`Resource busy`).

## Notes

- Python deps (pyserial, **bleak** (BLE), pandas, numpy, plotly, matplotlib, folium,
  contextily, PIL) live in the conda env `shintai`; there is no requirements file. `console.py`
  and `storm_radar.py` serve with the **stdlib only** — self-contained HTML + `http.server`.
- `logs/` and `analysis/` are gitignored (data + generated output) and live here.
