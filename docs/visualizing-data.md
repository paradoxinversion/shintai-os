# Visualizing Shintai-OS data

Ways to turn the CSV logs in `../logs/` into visual insight, from zero-setup
quick looks to full exploratory analysis. Tagged ⭐ = my top pick in each group.

Your data has two natural shapes:
- **Time series** — every numeric column over `timestamp_ms` (distance, accel,
  heading, the thermal stats, CO₂/temp/humidity).
- **A GPS track** — `lat`/`lon` (+ `alt_m`, `speed_kmh`), best seen on a map.

Most tools below handle one or both. See **Caveats** at the bottom first — they
save headaches.

---

## 1. Fastest, no install

| Tool | Good for | Notes |
|---|---|---|
| **Numbers** (built-in) | quick line charts | `shintaicsv` opens the newest raw log in Numbers. Clunky past a handful of columns. (`shintailog` now builds the offline HUD instead — see `hud.py`.) |
| **csvplot.com** | instant time-series | drop the CSV in a browser, pick X = `timestamp_ms`. ⚠ uploads data. |
| **RAWGraphs** (rawgraphs.io) | pretty one-off charts | drag-drop, many chart types. ⚠ uploads data. |
| **VisiData** (`pip install visidata`, then `vd file.csv`) | terminal power-browsing | sort/filter/frequency/quick plots without leaving the shell. Local. |

## 2. Desktop apps (local, more power)

| Tool | Good for | Notes |
|---|---|---|
| ⭐ **PlotJuggler** (no brew formula — build from source, see below) | multi-channel sensor logs | *built* for exactly this. Drag many signals onto synced, zoomable time plots. Fully local. |
| **Tableau Public** (free) | drag-drop dashboards | strong at time series **and** maps; shareable. Saves to Tableau's public cloud. |
| **DB Browser for SQLite** | SQL exploration | load the CSV, query/filter; pairs well with the SQLite path in §4. |
| **QGIS** (free) | serious geospatial | overkill for a track, but unmatched if you get into mapping. |

> **PlotJuggler on macOS** — there is *no* Homebrew formula or cask (`brew install
> plotjuggler` fails). Build from source against Qt5 (verified working 2026-06-23):
> ```sh
> brew install cmake qt@5
> git clone --depth 1 https://github.com/facontidavide/PlotJuggler.git
> cd PlotJuggler
> cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt@5)" -DCMAKE_BUILD_TYPE=Release
> cmake --build build -j$(sysctl -n hw.ncpu)
> ./build/bin/plotjuggler -d /path/to/log.csv   # then pick timestamp_s as the time axis
> ```
> Note it needs Qt**5** specifically — the project's CMake hardcodes a Qt5 lookup, so
> a Qt6-only system won't configure. The build skips a few optional plugins (QtAV
> video, Zcm, Arrow); none matter for CSV time-series.

## 3. Python (you already have conda + the `shintai` env)

Best long-term option — scriptable, repeatable, zero uploads.

- ⭐ **Jupyter + pandas + Plotly** — interactive charts, re-run each excursion.
  `pip install jupyterlab pandas plotly`
- **Plotly Express** — one-liner interactive charts: `px.line(df, x='timestamp_ms', y='co2_ppm')`.
- **hvPlot / HoloViews + Panel** — `df.hvplot.line(...)`; turns a DataFrame into an
  interactive dashboard with almost no code. Great for browsing.
- **Altair** — clean declarative charts; nice for faceting many signals.
- **seaborn** — statistical views: correlation heatmaps, pairplots, distributions.
- **D-Tale** (`pip install dtale`) — opens a spreadsheet-like **web UI** on a
  pandas DataFrame; sort/filter/chart/correlate by clicking. Local.
- **folium** — `df` with lat/lon → an interactive Leaflet map saved as HTML.

> Offer still stands: I can drop a `plot.py` / notebook in the project that
> auto-loads the newest log, fixes the timestamp, and pops the sensor traces +
> a route map in one command — fully local.

## 4. "Let the tool find patterns" (auto-EDA)

Point these at the CSV and they generate a full report — distributions,
correlations, missing-data, outliers — without you asking specific questions.
Best fit for *"I'm excited to see what surfaces."*

- ⭐ **ydata-profiling** (`pip install ydata-profiling`) — one call →
  a rich HTML report. Surfaces correlations (e.g. does CO₂ track standing still?
  does humidity move with temp?) automatically.
- **Sweetviz** (`pip install sweetviz`) — similar, very visual, great comparisons.
- **Datasette** (`pip install datasette sqlite-utils`) — turn the CSV into an
  explorable web app with SQL: `sqlite-utils insert log.db readings *.csv --csv`
  then `datasette log.db`. Add **datasette-cluster-map** to plot GPS points and
  **datasette-vega** for charts — exploration + maps + SQL in one local UI.

## 5. The GPS track specifically

| Tool | Good for | Notes |
|---|---|---|
| ⭐ **kepler.gl** | animated route maps | drop CSV with `lat`/`lon` (+ `timestamp_ms`); color the path by speed/CO₂/temp. Processes locally in-browser. |
| **GPS Visualizer** (gpsvisualizer.com) | map + speed/elevation profiles | takes CSV directly. Free. |
| **Google My Maps** | simple pins/path | easiest share. |
| **gpx.studio** | inspect/edit a route | convert CSV→GPX first. |
| **Google Earth** (KML) | 3D flyover | convert CSV→KML; fun with `alt_m`. |

## 6. Fun cross-sensor angles to try

Things worth plotting once you're in a tool — where the interesting stuff hides:

- **Map colored by environment** — your route tinted by `co2_ppm`, `air_temp_c`,
  or `humidity_pct`. Effectively an environmental map of where you walked.
- **Activity vs. environment** — compute accel magnitude
  `sqrt(accel_x² + accel_y² + accel_z²)` and overlay on CO₂: does CO₂ climb when
  you stop moving or enter enclosed spaces?
- **CO₂ as a "crowd / indoors" detector** — spikes often mean enclosed or busy spaces.
- **`hotspot_delta` timeline** — find the moments a warm object (person, vehicle,
  sun-baked surface) entered the thermal camera's view.
- **Humidity vs. temperature** — they often move together; deviations are interesting.
- **Heading vs. GPS bearing** — compare the compass to direction-of-travel from
  successive lat/lon points; mismatches show drift or sensor offset.

## Caveats

1. **`timestamp_ms` = milliseconds since the board booted**, not clock time.
   For a clean axis divide by 1000 (s) or 60000 (min). It resets to 0 on every
   power-up, so each log starts at ~0.
2. **Privacy:** `lat`/`lon` is your real location history. Prefer local tools
   (PlotJuggler, kepler.gl, Python, Datasette) over web uploaders for GPS logs.
3. **Blank fields are normal:** GPS columns stay empty until a fix (usually
   nothing indoors); SCD-40 columns are blank for the first few seconds after boot.
4. **Thermal is aggregated** (`min/ctr/max/mean/hotspot_delta`), not the full
   32×24 image — so think trends/timelines, not heatmap pictures. (Logging the
   full frame for heatmap video would be a firmware change.)
