"""
Stitch Shintai-OS logs into one timeline and surface insights.

- Loads every logs/shintai_log_*.csv with more than MIN_ROWS data rows (skips
  short test scraps), ordered by their wall-clock filename timestamp.
- Reconstructs absolute time per row:  file_start + timestamp_ms.
- Reports the gaps between sessions (time + distance moved).
- Writes analysis/combined.csv, analysis/timeseries.png, analysis/route_map.html.

Run:  conda activate shintai && python ~/shintai-os/groundstation/analyze.py
"""
import glob
import os
import re
import math

import pandas as pd
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.dates as mdates
import folium

import hokan  # Hokan (歩勘) dead-reckoning — base-side PDR path from the `steps` column
import kiatsu  # Kiatsu (気圧) — base-side floor/altitude from pressure_hpa (the z for Hokan's x,y)

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "logs")
OUT_DIR = os.path.join(HERE, "analysis")
MIN_ROWS = 50  # skip test scraps


def haversine_m(lat1, lon1, lat2, lon2):
    R = 6371000.0
    p1, p2 = math.radians(lat1), math.radians(lat2)
    dp = math.radians(lat2 - lat1)
    dl = math.radians(lon2 - lon1)
    a = math.sin(dp / 2) ** 2 + math.cos(p1) * math.cos(p2) * math.sin(dl / 2) ** 2
    return 2 * R * math.asin(math.sqrt(a))


def load_sessions():
    # Match new and pre-rename logs; order by the embedded timestamp (not the
    # filename) so the two prefixes still interleave chronologically.
    files = glob.glob(os.path.join(LOG_DIR, "shintai_log_*.csv")) + \
            glob.glob(os.path.join(LOG_DIR, "spidey_log_*.csv"))
    def _embedded_ts(path):
        m = re.search(r"(\d{8}_\d{6})", os.path.basename(path))
        return m.group(1) if m else ""
    files = sorted(files, key=_embedded_ts)
    sessions = []
    for f in files:
        try:
            df = pd.read_csv(f)
        except pd.errors.EmptyDataError:
            continue
        if len(df) < MIN_ROWS:
            continue
        m = re.search(r"(\d{8}_\d{6})", os.path.basename(f))
        start = pd.to_datetime(m.group(1), format="%Y%m%d_%H%M%S")
        df["time"] = start + pd.to_timedelta(df["timestamp_ms"], unit="ms")
        df["epoch_s"] = (df["time"] - pd.Timestamp("1970-01-01")) / pd.Timedelta(seconds=1)
        df["session"] = os.path.basename(f)
        sessions.append((f, start, df))
    return sessions


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    sessions = load_sessions()
    if not sessions:
        print("No sessions with >= %d rows found." % MIN_ROWS)
        return

    print("Sessions stitched:")
    for f, start, df in sessions:
        print(f"  {os.path.basename(f)}  start {start}  rows {len(df)}  "
              f"dur {df['timestamp_ms'].iloc[-1]/1000/60:.1f} min")

    combined = pd.concat([d for _, _, d in sessions], ignore_index=True)
    combined.to_csv(os.path.join(OUT_DIR, "combined.csv"), index=False)

    # ── Gaps between consecutive sessions ──
    print("\nGaps between sessions:")
    for (fa, sa, da), (fb, sb, db) in zip(sessions, sessions[1:]):
        end_a = da["time"].iloc[-1]
        gap_s = (db["time"].iloc[0] - end_a).total_seconds()
        line = f"  after {os.path.basename(fa)}: {gap_s/60:.1f} min unplugged"
        a_fix = da[da["gps_fix"] == 1]
        b_fix = db[db["gps_fix"] == 1]
        if len(a_fix) and len(b_fix):
            d = haversine_m(a_fix["lat"].iloc[-1], a_fix["lon"].iloc[-1],
                            b_fix["lat"].iloc[0], b_fix["lon"].iloc[0])
            line += f", moved {d:.0f} m between last/first fix"
        print(line)

    # ── Headline stats ──
    fix = combined[combined["gps_fix"] == 1].copy()
    print("\nHeadline numbers:")
    span = (combined["time"].max() - combined["time"].min()).total_seconds() / 60
    print(f"  Wall-clock span (incl. gaps): {span:.1f} min, {len(combined)} samples")
    if len(fix):
        # distance travelled within each session (don't bridge gaps)
        total = 0.0
        for _, _, df in sessions:
            g = df[df["gps_fix"] == 1]
            la, lo = g["lat"].to_numpy(), g["lon"].to_numpy()
            for i in range(1, len(g)):
                total += haversine_m(la[i-1], lo[i-1], la[i], lo[i])
        print(f"  Distance travelled (sum within sessions): {total/1000:.2f} km")
        print(f"  Speed: max {fix['speed_kmh'].max():.0f} km/h, "
              f"moving-median {fix.loc[fix['speed_kmh']>1,'speed_kmh'].median():.0f} km/h")
        print(f"  Altitude: {fix['alt_m'].min():.0f}–{fix['alt_m'].max():.0f} m "
              f"(climb {fix['alt_m'].max()-fix['alt_m'].min():.0f} m)")
    for col, unit in [("co2_ppm", "ppm"), ("air_temp_c", "°C"),
                      ("humidity_pct", "%RH"), ("thermal_max", "°C surface"),
                      ("hotspot_delta", "°C")]:
        s = pd.to_numeric(combined[col], errors="coerce").dropna()
        if len(s):
            print(f"  {col}: {s.min():.1f}–{s.max():.1f} {unit} (median {s.median():.1f})")

    # ── Hokan: steps walked (summed per session so a reboot's counter reset doesn't
    # bridge sessions), and the on-foot distance it implies. Degrades: sessions/logs
    # with no `steps` column contribute nothing.
    walked = sum(hokan.total_steps(pd.to_numeric(df["steps"], errors="coerce").tolist())
                 for _, _, df in sessions if "steps" in df.columns)
    if walked:
        print(f"  Steps (Hokan): {walked:,} "
              f"(~{walked * hokan.STEP_LEN_M / 1000:.2f} km on foot @ {hokan.STEP_LEN_M} m/step)")

    # ── Kiatsu: barometric floor/altitude reconstruction (base-side, KiD-4) — derive a
    # relative floor index + altitude (m) per session from pressure_hpa, temperature-
    # corrected via air_temp_c (KiD-2). The derived columns feed the timeseries Z-strip
    # and the PDR floor tags below. Degrades: sessions with <2 valid pressures skip.
    floor_label = {}
    for _, _, df in sessions:
        if "pressure_hpa" not in df.columns:
            continue
        p = pd.to_numeric(df["pressure_hpa"], errors="coerce").tolist()
        t = (pd.to_numeric(df["air_temp_c"], errors="coerce").tolist()
             if "air_temp_c" in df.columns else None)
        if sum(1 for v in p if kiatsu._is_num(v) and v > 0) < 2:
            continue
        df["baro_floor"] = kiatsu.floor_steps(p)
        df["baro_alt"] = kiatsu.altitude_profile(p, t)
        name = df["session"].iloc[0]
        floor_label[name] = f"floors {min(df['baro_floor'])}..{max(df['baro_floor'])}"
        line = kiatsu.summary(p, t)
        if line:
            print(f"  Kiatsu ({name}): {line}")

    # ── Time-series figure ──
    panels = [
        ("speed_kmh", "Speed (km/h)"),
        ("alt_m", "Altitude (m)"),
        ("co2_ppm", "CO2 (ppm)"),
        ("air_temp_c", "Air temp (°C)"),
        ("humidity_pct", "Humidity (%RH)"),
        ("hotspot_delta", "Thermal hotspot Δ (°C)"),
    ]
    # Hokan step count, only when logged (old logs predate the column) — the gait
    # timeline beside the environment panels.
    if "steps" in combined.columns and pd.to_numeric(combined["steps"], errors="coerce").notna().any():
        panels.append(("steps", "Steps (cumulative)"))
    # Kiatsu: pressure and the derived relative-altitude Z-strip (the z for Hokan's x,y),
    # only when a barometer logged. baro_alt is a per-session derived column (above).
    if "pressure_hpa" in combined.columns and pd.to_numeric(combined["pressure_hpa"], errors="coerce").notna().any():
        panels.append(("pressure_hpa", "Pressure (hPa)"))
    if any("baro_alt" in df.columns for _, _, df in sessions):
        panels.append(("baro_alt", "Baro altitude (m, rel)"))
    fig, axes = plt.subplots(len(panels), 1, figsize=(12, 13), sharex=True)
    for ax, (col, label) in zip(axes, panels):
        for _, _, df in sessions:
            if col not in df.columns:      # e.g. `steps` in newer logs only — skip old sessions
                continue
            y = pd.to_numeric(df[col], errors="coerce")
            ax.plot(df["time"], y, lw=0.9)
        ax.set_ylabel(label, fontsize=9)
        ax.grid(alpha=0.3)
    axes[-1].xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
    axes[0].set_title("Shintai-OS — stitched sessions (gaps show as flat breaks)")
    plt.tight_layout()
    fig.savefig(os.path.join(OUT_DIR, "timeseries.png"), dpi=110)
    print(f"\nWrote {os.path.join(OUT_DIR, 'timeseries.png')}")

    # ── Static route scatter (viewable PNG), colored by speed ──
    if len(fix):
        fig2, ax2 = plt.subplots(figsize=(9, 9))
        sc = ax2.scatter(fix["lon"], fix["lat"], c=fix["speed_kmh"],
                         cmap="viridis", s=6)
        ax2.plot(fix["lon"], fix["lat"], lw=0.3, color="gray", alpha=0.5)
        fig2.colorbar(sc, label="Speed (km/h)")
        ax2.set_xlabel("Longitude"); ax2.set_ylabel("Latitude")
        ax2.set_title("Route (colored by speed)")
        ax2.set_aspect("equal", adjustable="datalim")
        fig2.savefig(os.path.join(OUT_DIR, "route.png"), dpi=110)
        print(f"Wrote {os.path.join(OUT_DIR, 'route.png')}")

    # ── Hokan PDR: per-session dead-reckoned track (metres from steps + heading),
    # anchored at the session's first GPS fix (AC-4) or the origin when GPS-denied.
    def pdr_of(df):
        if "steps" not in df.columns:
            return None
        steps = pd.to_numeric(df["steps"], errors="coerce").tolist()
        if hokan.total_steps(steps) <= 0:
            return None
        hdg = pd.to_numeric(df["heading_deg"], errors="coerce").tolist()
        g = df[df["gps_fix"] == 1]
        lat0, lon0 = (g["lat"].iloc[0], g["lon"].iloc[0]) if len(g) else (0.0, 0.0)
        xs, ys = hokan.integrate(steps, hdg)
        return hokan.to_latlon(xs, ys, lat0, lon0)

    pdr_tracks = [(df["session"].iloc[0], pdr_of(df)) for _, _, df in sessions]
    pdr_tracks = [(name, t) for name, t in pdr_tracks if t]

    # ── Interactive map ── drawn when there's a GPS route OR a dead-reckoned track,
    # so a fully GPS-denied walk (the PDR use case) still renders.
    if len(fix) or pdr_tracks:
        if len(fix):
            center = [fix["lat"].mean(), fix["lon"].mean()]
        else:
            first = pdr_tracks[0][1]
            center = [first[0][0], first[1][0]]
        fmap = folium.Map(location=center, zoom_start=12, tiles="OpenStreetMap")
        colors = ["blue", "red", "green", "purple", "orange"]
        for i, (_, _, df) in enumerate(sessions):
            g = df[df["gps_fix"] == 1]
            pts = list(zip(g["lat"], g["lon"]))
            if pts:
                folium.PolyLine(pts, color=colors[i % len(colors)], weight=4,
                                opacity=0.8, tooltip=df["session"].iloc[0]).add_to(fmap)
                folium.Marker(pts[0], tooltip="start " + df["session"].iloc[0],
                              icon=folium.Icon(color="green")).add_to(fmap)
                folium.Marker(pts[-1], tooltip="end " + df["session"].iloc[0],
                              icon=folium.Icon(color="red")).add_to(fmap)
        # Dead-reckoned tracks in a deliberately distinct style (black dashed) so
        # they read apart from the solid coloured GPS routes.
        # Kiatsu composes z onto the track: where a barometer logged, the PDR tooltip
        # carries the floor range it crossed — the 2-D dead-reckoned path tagged 3-D.
        for name, (lats, lons) in pdr_tracks:
            tip = "Hokan PDR: " + name
            if name in floor_label:
                tip += " — Kiatsu " + floor_label[name]
            folium.PolyLine(list(zip(lats, lons)), color="black", weight=2.5,
                            opacity=0.75, dash_array="5,8", tooltip=tip).add_to(fmap)
        fmap.save(os.path.join(OUT_DIR, "route_map.html"))
        print(f"Wrote {os.path.join(OUT_DIR, 'route_map.html')}")


if __name__ == "__main__":
    main()
