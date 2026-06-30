"""Shintai-OS HUD — the default view of the newest excursion.

One command: finds the newest non-trivial log, cleans it, and builds an
offline, cyberpunk HUD that integrates the GPS route (on a real DarkMatter
street basemap, baked in) with every sensor on a synced time axis. If the
basemap tiles can't be fetched (no network), it gracefully drops to a tileless
route so the view ALWAYS builds.

Run:  conda activate shintai && python ~/shintai-os/groundstation/hud.py
Out:  analysis/hud.html   (self-contained, offline once built)  -> opened
"""
import os
import io
import glob
import base64
import subprocess

import numpy as np
import pandas as pd
import plotly.graph_objects as go
from plotly.subplots import make_subplots

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "logs")
OUT = os.path.join(HERE, "analysis", "hud.html")
MIN_ROWS = 50

# --- pick the newest log with real data -----------------------------------
logs = sorted(glob.glob(os.path.join(LOG_DIR, "shintai_log_*.csv")) +
              glob.glob(os.path.join(LOG_DIR, "spidey_log_*.csv")),  # back-compat: pre-rename logs
              key=os.path.getmtime, reverse=True)
newest = next((p for p in logs if sum(1 for _ in open(p)) - 1 >= MIN_ROWS), None)
if newest is None:
    raise SystemExit("No log with >= %d rows found in %s" % (MIN_ROWS, LOG_DIR))
print("newest log:", os.path.basename(newest))

df = pd.read_csv(newest)
df["timestamp_s"] = (df["timestamp_ms"] - df["timestamp_ms"].iloc[0]) / 1000.0
df["t_min"] = df["timestamp_s"] / 60.0
df["accel_mag"] = (df.accel_x**2 + df.accel_y**2 + df.accel_z**2) ** 0.5
gps = df.dropna(subset=["lat", "lon"])

# --- palette --------------------------------------------------------------
BG, PANEL, GRID = "#05060a", "#0b0e1a", "#16203a"
NEON = ["#00f0ff", "#ff2bd6", "#b14bff", "#39ff14", "#ffd300"]
NEON_RGB = [(0, 240, 255), (255, 43, 214), (177, 75, 255), (57, 255, 20), (255, 211, 0)]
NEON_SCALE = [[0, "#0d1b4c"], [0.4, "#b14bff"], [0.7, "#ff2bd6"], [1, "#00f0ff"]]
SENSORS = [("co2_ppm", "CO₂ ppm"), ("accel_mag", "ACCEL |g|"),
           ("hotspot_delta", "THERMAL Δ"), ("humidity_pct", "HUMID %"),
           ("air_temp_c", "TEMP °C")]

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
                    subplot_titles=[title] + [s[1] for s in SENSORS])

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
                  line=dict(color="rgba(0,240,255,0.20)", width=9),
                  hoverinfo="skip"), row=1, col=1)
    fig.add_trace(go.Scatter(
        x=rx, y=ry, mode="markers",
        marker=dict(size=5, color=gps.co2_ppm, colorscale=NEON_SCALE, showscale=True,
                    colorbar=dict(title="CO₂", len=0.4, y=0.8, thickness=10, outlinewidth=0)),
        customdata=gps[["t_min", "speed_kmh"]],
        hovertemplate="t %{customdata[0]:.1f}min  %{customdata[1]:.1f}km/h<extra></extra>"),
        row=1, col=1)

for i, (col, _) in enumerate(SENSORS):
    r = i + 2
    fig.add_trace(go.Scatter(
        x=df.t_min, y=df[col], mode="lines",
        line=dict(color=NEON[i % len(NEON)], width=1.6), fill="tozeroy",
        fillcolor="rgba(%d,%d,%d,0.13)" % NEON_RGB[i % len(NEON_RGB)],
        hovertemplate=f"{col} %{{y:.1f}} @ %{{x:.1f}}min<extra></extra>"), row=r, col=1)
    fig.update_xaxes(matches="x2", row=r, col=1)
fig.update_xaxes(title_text="MINUTES SINCE BOOT", row=rows, col=1)

fig.update_layout(
    template="plotly_dark", height=1240, paper_bgcolor=BG, plot_bgcolor=PANEL,
    font=dict(family="SFMono-Regular, Menlo, monospace", color="#9fefff", size=11),
    title=dict(text="▸ SHINTAI-OS // " + os.path.basename(newest),
               font=dict(size=18, color="#00f0ff")),
    margin=dict(l=60, r=30, t=70, b=40), showlegend=False, hovermode="x unified")
fig.update_xaxes(gridcolor=GRID, zeroline=False, linecolor=GRID)
fig.update_yaxes(gridcolor=GRID, zeroline=False, linecolor=GRID)
for a in fig.layout.annotations:
    a.update(font=dict(color="#ff2bd6", size=11, family="Menlo, monospace"), xanchor="left")

fig.write_html(OUT, include_plotlyjs=True)
print("wrote", OUT)
subprocess.run(["open", OUT], check=False)
