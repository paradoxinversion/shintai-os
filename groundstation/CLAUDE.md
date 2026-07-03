# groundstation/ — Python consumer (capture + analysis)

Consumes the board over USB serial and dumped flash. The board's schema is defined in
[`../CONTRACT.md`](../CONTRACT.md); any hardcoded column names here mirror it. See the
root [`CLAUDE.md`](../CLAUDE.md) for the contract seam.

## Commands

No build/test/lint. Three zsh launchers here (each `source`s miniconda +
`conda activate shintai`, then runs the matching `.py` with `python`, **not**
`python3` — a pyenv shim shadows the conda env's pyserial). Shell aliases (`shintai`,
`shintailog`, `shintaipull`, `shintaicsv`) in `~/.zshrc` point at these launchers.
`logs/` and `analysis/` are resolved `__file__`-relative, so they live under
`groundstation/`.

```sh
groundstation/start.sh   # shintai     — live capture: serial → panel + logs/*.csv
groundstation/hud.sh     # shintailog  — build + open analysis/hud.html for newest log
groundstation/pull.sh    # shintaipull — dump onboard flash logs into logs/
python groundstation/analyze.py   # stitch ALL sessions → combined.csv + pngs + route_map.html
```

## Layout

- `shintai-logger.py` — serial → CSV writer. In a TTY it paints an in-place ANSI
  status panel (`dashboard()`); piped, it falls back to plain `[#N logged]` lines.
- `shintai-pull.py` — dumps onboard flash over framed serial.
- `hud.py` — newest log → self-contained offline Plotly HUD (`analysis/hud.html`):
  GPS route on a base64-baked CartoDB DarkMatter basemap (via `contextily`) + every
  sensor on a synced time axis. **Degrades gracefully** to a tileless route offline.
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
- **The `shintai_log_*.csv` glob is load-bearing.** `hud.py` and `analyze.py` glob it
  (plus legacy `spidey_log_*`) and skip files under `MIN_ROWS` (50) data rows.
- **Close the Arduino IDE Serial Monitor before running any Python tool** — the
  serial port can't be shared (`Resource busy`).

## Notes

- Python deps (pyserial, pandas, numpy, plotly, matplotlib, folium, contextily, PIL)
  live in the conda env `shintai`; there is no requirements file.
- `logs/` and `analysis/` are gitignored (data + generated output) and live here.
