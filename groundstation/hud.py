"""Shintai-OS HUD — the default view of the newest excursion.

One command: finds the newest non-trivial log, cleans it, and builds an
offline, cassette-futurism instrument HUD (docs/style.md — VOID/PHOSPHOR,
monochrome, subtle scanlines) that integrates the GPS route (on a real
DarkMatter street basemap, baked in) with every sensor on a synced time axis.
If the
basemap tiles can't be fetched (no network), it gracefully drops to a tileless
route so the view ALWAYS builds.

Run:  conda activate shintai && python ~/shintai-os/groundstation/hud.py
Out:  analysis/hud.html   (self-contained, offline once built)  -> opened
"""
import os
import io
import sys
import glob
import base64
import datetime
import subprocess

import numpy as np
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "logs")
OUT = os.path.join(HERE, "analysis", "hud.html")
MIN_ROWS = 50

# --- choose which log to build (default: newest with real data) -----------
# Usage:  python hud.py            -> newest selectable log (default)
#         python hud.py -l         -> list selectable logs and exit
#         python hud.py <index>    -> pick by the index shown by -l
#         python hud.py <substring>-> newest log whose filename contains <substring>
def _rows(path):
    with open(path) as fh:
        return sum(1 for _ in fh) - 1

def candidate_logs():
    """Selectable logs (>= MIN_ROWS data rows), newest first."""
    paths = glob.glob(os.path.join(LOG_DIR, "shintai_log_*.csv")) + \
            glob.glob(os.path.join(LOG_DIR, "spidey_log_*.csv"))   # back-compat: pre-rename
    paths = [p for p in paths if _rows(p) >= MIN_ROWS]
    return sorted(paths, key=os.path.getmtime, reverse=True)

def print_log_list(paths):
    print("Selectable logs (newest first) — pick with:  shintailog <index|substring>\n")
    for i, p in enumerate(paths):
        when = datetime.datetime.fromtimestamp(os.path.getmtime(p)).strftime("%Y-%m-%d %H:%M")
        print("  [%2d] %-48s %6d rows  %s" % (i, os.path.basename(p), _rows(p), when))

def choose_log(arg, paths):
    if arg is None:
        return paths[0]                                  # default: newest
    if arg.isdigit() and int(arg) < len(paths):          # a small number -> list index
        return paths[int(arg)]
    matches = [p for p in paths if arg.lower() in os.path.basename(p).lower()]
    if not matches:
        raise SystemExit("No log matches '%s'. Run with -l to list." % arg)
    return matches[0]                                    # newest filename match

arg = sys.argv[1] if len(sys.argv) > 1 else None
logs = candidate_logs()
if not logs:
    raise SystemExit("No log with >= %d rows found in %s" % (MIN_ROWS, LOG_DIR))
if arg in ("-l", "--list", "ls"):
    print_log_list(logs)
    sys.exit(0)
logpath = choose_log(arg, logs)
print("log:", os.path.basename(logpath))

df = pd.read_csv(logpath)
df["timestamp_s"] = (df["timestamp_ms"] - df["timestamp_ms"].iloc[0]) / 1000.0
df["t_min"] = df["timestamp_s"] / 60.0
df["accel_mag"] = (df.accel_x**2 + df.accel_y**2 + df.accel_z**2) ** 0.5
if "gas_ohms" in df.columns:
    df["gas_kohm"] = df["gas_ohms"] / 1000.0   # ohms → kΩ for a readable trace
gps = df.dropna(subset=["lat", "lon"])

# --- palette: cassette-futurism instrument language (docs/style.md, web posture) --
# One emissive color per surface; green = nominal, amber = caution, red = alarm.
# NO rainbow — that's the cyberpunk tell the guide rejects. Data reads in PHOSPHOR;
# labels/chrome in BONE; structure in GRID.
VOID, PANEL, GRID = "#05080A", "#0C1410", "#1C4028"
PHOSPHOR, PHOSPHOR_DIM = "#58F07A", "#2E7A45"
AMBER, ALERT = "#F2A93B", "#FF4438"
BONE, BONE_DIM = "#C9CDBC", "#6B6F62"
# CO₂ over the route carries STATE, not decoration: in-range green → climbing amber → over-limit red.
STATE_SCALE = [[0.0, PHOSPHOR_DIM], [0.45, PHOSPHOR], [0.72, AMBER], [1.0, ALERT]]
MONO = "'IBM Plex Mono', 'Space Mono', 'SFMono-Regular', Menlo, monospace"
SENSORS = [("distance_l_mm", "REAR-L mm"), ("distance_r_mm", "REAR-R mm"),
           ("co2_ppm", "CO₂ ppm"), ("accel_mag", "ACCEL |g|"),
           ("hotspot_delta", "THERMAL Δ"), ("humidity_pct", "HUMID %"),
           ("air_temp_c", "TEMP °C"), ("pressure_hpa", "PRESS hPa"),
           ("gas_kohm", "GAS kΩ")]
SENSORS = [s for s in SENSORS if s[0] in df.columns]   # drop fields a log lacks (pre-BME688, or pre-dual-arc distance_mm)

# --- try to bake a DarkMatter basemap; fall back to tileless if offline ----
R = 6378137.0
basemap = None
if len(gps):
    mx = np.radians(gps.lon) * R
    my = np.log(np.tan(np.pi / 4 + np.radians(gps.lat) / 2)) * R
    try:
        import contextily as cx
        from PIL import Image
        img, ext = cx.bounds2img(
            gps.lon.min() - 0.01, gps.lat.min() - 0.01,
            gps.lon.max() + 0.01, gps.lat.max() + 0.01, ll=True,
            source=cx.providers.CartoDB.DarkMatterNoLabels)
        buf = io.BytesIO(); Image.fromarray(img[:, :, :3]).save(buf, format="PNG")
        basemap = ("data:image/png;base64," + base64.b64encode(buf.getvalue()).decode(), ext)
        print("basemap: DarkMatter tiles baked in")
    except Exception as exc:
        print("basemap: offline/failed (%s) -> tileless route" % type(exc).__name__)

# --- figure ---------------------------------------------------------------
rows = 1 + len(SENSORS)
title = "// GPS TRACK" + (" on DarkMatter" if basemap else "") + " — colored by CO₂"
fig = make_subplots(rows=rows, cols=1,
                    row_heights=[0.46] + [0.54 / len(SENSORS)] * len(SENSORS),
                    vertical_spacing=0.012,
                    subplot_titles=[title] + [s[1].upper() for s in SENSORS])

if len(gps):
    if basemap:
        src, (left, right, bottom, top) = basemap
        fig.add_layout_image(dict(source=src, xref="x", yref="y", x=left, y=top,
                                  sizex=right - left, sizey=top - bottom,
                                  sizing="stretch", layer="below",
                                  xanchor="left", yanchor="top"), row=1, col=1)
        rx, ry = mx, my
        fig.update_xaxes(range=[left, right], row=1, col=1, visible=False)
        fig.update_yaxes(range=[bottom, top], scaleanchor="x", scaleratio=1,
                         row=1, col=1, visible=False)
    else:
        rx, ry = gps.lon, gps.lat
        fig.update_yaxes(scaleanchor="x",
                         scaleratio=1 / np.cos(np.radians(gps.lat.mean())),
                         row=1, col=1)
    fig.add_trace(go.Scatter(x=rx, y=ry, mode="lines",
                  line=dict(color="rgba(46,122,69,0.35)", width=8),   # PHOSPHOR_DIM track
                  hoverinfo="skip"), row=1, col=1)
    fig.add_trace(go.Scatter(
        x=rx, y=ry, mode="markers",
        marker=dict(size=5, color=gps.co2_ppm, colorscale=STATE_SCALE, showscale=True,
                    colorbar=dict(title=dict(text="CO₂", font=dict(color=BONE, family=MONO)),
                                  len=0.4, y=0.8, thickness=10, outlinewidth=0,
                                  tickfont=dict(color=BONE_DIM, family=MONO))),
        customdata=gps[["t_min", "speed_kmh"]],
        hovertemplate="t %{customdata[0]:.1f}min  %{customdata[1]:.1f}km/h<extra></extra>"),
        row=1, col=1)

for i, (col, _) in enumerate(SENSORS):
    r = i + 2
    # Monochrome discipline (style.md §2): every channel reads in PHOSPHOR — its own
    # panel + BONE label distinguish it, not a decorative hue.
    fig.add_trace(go.Scatter(
        x=df.t_min, y=df[col], mode="lines",
        line=dict(color=PHOSPHOR, width=1.4), fill="tozeroy",
        fillcolor="rgba(88,240,122,0.10)",
        hovertemplate=f"{col} %{{y:.1f}} @ %{{x:.1f}}min<extra></extra>"), row=r, col=1)
    fig.update_xaxes(matches="x2", row=r, col=1)
fig.update_xaxes(title_text="MINUTES SINCE BOOT", row=rows, col=1)

fig.update_layout(
    template="plotly_dark", height=1240, paper_bgcolor=VOID, plot_bgcolor=PANEL,
    font=dict(family=MONO, color=BONE, size=11),
    title=dict(text="▸ SHINTAI-OS  //  " + os.path.basename(logpath).upper(),
               font=dict(family=MONO, size=17, color=PHOSPHOR)),
    margin=dict(l=60, r=30, t=70, b=40), showlegend=False, hovermode="x unified")
fig.update_xaxes(gridcolor=GRID, zeroline=False, linecolor=GRID, tickfont=dict(color=BONE_DIM))
fig.update_yaxes(gridcolor=GRID, zeroline=False, linecolor=GRID, tickfont=dict(color=BONE_DIM))
for a in fig.layout.annotations:                          # panel titles are BONE chrome
    a.update(font=dict(color=BONE, size=11, family=MONO), xanchor="left")

fig.write_html(OUT, include_plotlyjs=True)
# CRT flourish (style.md §7 / web posture): VOID ground, IBM Plex Mono, subtle scanlines —
# injected into the self-contained HTML. The webfont @import upgrades when online and
# falls back to the mono stack offline, so the HUD still builds + reads with no network.
CRT_CSS = (
    "<style>\n"
    "@import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Mono:wght@400;500&display=swap');\n"
    "body{background:#05080A!important;}\n"
    "body::after{content:'';position:fixed;inset:0;pointer-events:none;z-index:9999;"
    "background:repeating-linear-gradient(to bottom,rgba(0,0,0,0) 0 2px,rgba(0,0,0,0.16) 2px 3px);"
    "mix-blend-mode:multiply;}\n"
    "</style>\n"
)
with open(OUT, "r") as _fh:
    _html = _fh.read()
with open(OUT, "w") as _fh:
    _fh.write(_html.replace("</head>", CRT_CSS + "</head>", 1))
print("wrote", OUT)
subprocess.run(["open", OUT], check=False)
