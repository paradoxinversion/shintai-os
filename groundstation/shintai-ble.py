"""Shintai-OS BLE central — untethered wireless capture on the laptop (bleak).

The groundstation's THIRD transport, beside USB serial (shintai-logger.py) and the onboard
flash dump (shintai-pull.py): a GATT central that does what the phone apps do — scan for
`ShintaiOS-<role>`, subscribe to the characteristics :core `ShintaiGatt.kt` exposes, and
reconstruct the telemetry from the per-sensor notifications into
`logs/shintai_ble_<time>.csv`. No wire — `console.py` / `hud.py` read that CSV, so the whole
console runs wireless. Reconnects if the link drops.

BLE is a LOSSY per-sensor summary (the chars carry `L:1234 R:1180 mm`, `23.0C 41%RH 750ppm`,
`km=1 e=227467 n=8` — not the exact firmware row), so this reconstructs what the string chars
carry, not the full 30-column CSV (`sats` / `thermal_mean` aren't on the air; `hotspot_delta`
is re-derived). The flash log stays the column-exact archival record. Range is ~one room; for
past-the-room reach the board would serve over WiFi — a separate firmware piece.

Run:  conda activate shintai && python groundstation/shintai-ble.py [--name ShintaiOS-fwd] [--seconds N]
"""
import asyncio
import csv
import os
import re
import sys
import time
import argparse
from datetime import datetime

from bleak import BleakScanner, BleakClient

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "logs")
NEAR_MM = 200

# GATT — mirror CONTRACT.md / :core ShintaiGatt.kt. String characteristics only (the two
# binary grids — Thermal Grid, Rear Depth Grid — are image channels for a later pass).
SERVICE = "12345678-1234-1234-1234-123456789abc"
CH = {
    "distance":    "abcd1234-ab12-ab12-ab12-abcdef123456",
    "alert":       "abcd5678-ab12-ab12-ab12-abcdef123456",
    "heading":     "abcd9012-ab12-ab12-ab12-abcdef123456",
    "accel":       "abcdef12-ab12-ab12-ab12-abcdef123456",
    "gps":         "abcd3456-ab12-ab12-ab12-abcdef123456",
    "climate":     "abcdba98-ab12-ab12-ab12-abcdef123456",
    "thermal":     "abcd6789-ab12-ab12-ab12-abcdef123456",
    "environment": "abcdc0de-ab12-ab12-ab12-abcdef123456",
    "hokan":       "abcdf007-ab12-ab12-ab12-abcdef123456",
    "lightning":   "abcda535-ab12-ab12-ab12-abcdef123456",
}

# Reconstructed CSV columns — the firmware schema, so console.py/hud.py read it unchanged.
COLS = ("timestamp_ms,distance_l_mm,distance_r_mm,alert,heading_deg,cardinal,accel_x,accel_y,"
        "accel_z,gps_fix,lat,lon,alt_m,speed_kmh,sats,thermal_min,thermal_ctr,thermal_max,"
        "thermal_mean,hotspot_delta,co2_ppm,air_temp_c,humidity_pct,pressure_hpa,gas_ohms,"
        "steps,lightning_km,lightning_energy,lightning_strikes,board").split(",")


class State:
    """Live merged telemetry, reconstructed from the per-sensor BLE notifications."""
    def __init__(self, role="fwd"):
        self.d = {c: "" for c in COLS}
        self.d.update({"alert": "0", "gps_fix": "0", "board": role})
        self.bme_seen = False   # BME688 (Environment) takes precedence over SCD-40 for air T/RH
        self.pkts = 0

    def feed(self, key, text):
        self.pkts += 1
        d = self.d
        if key == "distance":
            m = re.search(r"L:(--|\d+)\s+R:(--|\d+)", text)
            if m:
                lv = None if m.group(1) == "--" else int(m.group(1))
                rv = None if m.group(2) == "--" else int(m.group(2))
                d["distance_l_mm"] = "" if lv is None else lv
                d["distance_r_mm"] = "" if rv is None else rv
                near = min([x for x in (lv, rv) if x is not None], default=None)
                if near is not None and near > NEAR_MM:
                    d["alert"] = "0"            # clear the latch (mirror :core / firmware)
        elif key == "alert":
            d["alert"] = "1"                    # edge-triggered "CLOSE"; distance beyond NEAR clears it
        elif key == "heading":
            m = re.search(r"([\d.]+)\s*[°\xb0]\s*(\w+)", text)
            if m:
                d["heading_deg"], d["cardinal"] = m.group(1), m.group(2)
        elif key == "accel":
            m = re.search(r"X:(-?[\d.]+)\s+Y:(-?[\d.]+)\s+Z:(-?[\d.]+)", text)
            if m:
                d["accel_x"], d["accel_y"], d["accel_z"] = m.group(1), m.group(2), m.group(3)
        elif key == "gps":
            m = re.search(r"(-?[\d.]+),(-?[\d.]+)\s+(-?[\d.]+)m\s+([\d.]+)km/h", text)
            if m:
                d["gps_fix"] = "1"
                d["lat"], d["lon"], d["alt_m"], d["speed_kmh"] = m.group(1), m.group(2), m.group(3), m.group(4)
        elif key == "climate":
            m = re.search(r"([\d.]+)C\s+([\d.]+)%RH(?:\s+(\d+)ppm)?", text)
            if m:
                if not self.bme_seen:           # SCD-40 fills air T/RH only when the BME is absent
                    d["air_temp_c"], d["humidity_pct"] = m.group(1), m.group(2)
                if m.group(3):
                    d["co2_ppm"] = m.group(3)
        elif key == "environment":
            m = re.search(r"([\d.]+)hPa\s+(\d+)ohm\s+([\d.]+)C\s+([\d.]+)%RH", text)
            if m:
                self.bme_seen = True
                d["pressure_hpa"], d["gas_ohms"] = m.group(1), m.group(2)
                d["air_temp_c"], d["humidity_pct"] = m.group(3), m.group(4)
        elif key == "thermal":
            m = re.search(r"Ctr:([\d.-]+)\s+Min:([\d.-]+)\s+Max:([\d.-]+)", text)
            if m:
                d["thermal_ctr"], d["thermal_min"], d["thermal_max"] = m.group(1), m.group(2), m.group(3)
                if d["air_temp_c"]:             # hotspot_delta isn't on the air — re-derive it
                    try:
                        d["hotspot_delta"] = round(float(m.group(3)) - float(d["air_temp_c"]), 1)
                    except ValueError:
                        pass
        elif key == "hokan":
            m = re.search(r"(\d+)\s+[\d.]+\s+\d+", text)
            if m:
                d["steps"] = m.group(1)
        elif key == "lightning":
            m = re.search(r"km=(-?\d+)\s+e=(\d+)\s+n=(\d+)", text)
            if m:
                d["lightning_km"], d["lightning_energy"], d["lightning_strikes"] = m.group(1), m.group(2), m.group(3)


async def find_board(name):
    want = name or "ShintaiOS"
    return await BleakScanner.find_device_by_filter(
        lambda dev, adv: (dev.name or "").startswith(want), timeout=12.0)


async def capture(dev, state, writer, fh, stop_after):
    """One connection: subscribe + write a row/sec until disconnect or timeout.
    Returns (rows_written, user_requested_stop)."""
    if "-" in (dev.name or ""):
        state.d["board"] = dev.name.rsplit("-", 1)[-1]
    dropped = asyncio.Event()
    async with BleakClient(dev, disconnected_callback=lambda _c: dropped.set()) as client:
        print("● connected %s — subscribing" % dev.name)
        for key, uuid in CH.items():
            def cb(_sender, data, key=key):
                try:
                    state.feed(key, bytes(data).decode("utf-8", "replace"))
                except Exception:  # noqa: BLE001 — one bad packet must not kill the stream
                    pass
            try:
                await client.start_notify(uuid, cb)
            except Exception as e:  # noqa: BLE001
                print("  (skip %s: %s)" % (key, e))
        t0 = time.time()
        rows = 0
        try:
            while not dropped.is_set() and (stop_after <= 0 or time.time() - t0 < stop_after):
                await asyncio.sleep(1.0)
                state.d["timestamp_ms"] = int(time.time() * 1000)   # host wall-clock ms
                writer.writerow([state.d[c] for c in COLS])
                fh.flush()
                rows += 1
                sys.stdout.write("\r[%s] #%d rows · %d notifies · co2 %s · strikes %s   "
                                 % (state.d["board"], rows, state.pkts,
                                    state.d["co2_ppm"] or "—", state.d["lightning_strikes"] or "—"))
                sys.stdout.flush()
        except asyncio.CancelledError:
            return rows, True
    return rows, stop_after > 0 and time.time() - t0 >= stop_after


async def main(args):
    os.makedirs(LOG_DIR, exist_ok=True)
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    path = os.path.join(LOG_DIR, "shintai_ble_%s.csv" % stamp)
    state = State()
    with open(path, "w", newline="") as fh:
        writer = csv.writer(fh)
        writer.writerow(COLS)
        fh.flush()
        print("logging → %s   (Ctrl+C to stop)" % os.path.basename(path))
        while True:
            print("scanning for %s…" % (args.name or "ShintaiOS-<role>"))
            dev = await find_board(args.name)
            if not dev:
                if args.once:
                    print("no ShintaiOS board found in range (is BLE on / Bluetooth permission granted?).")
                    return
                print("  none in range; retrying…")
                await asyncio.sleep(3)
                continue
            try:
                rows, done = await capture(dev, state, writer, fh, args.seconds)
            except Exception as e:  # noqa: BLE001 — connection errors are expected; reconnect
                print("\n  link error: %s" % e)
                rows, done = 0, False
            print("\n  %d rows this session." % rows)
            if done or args.once or args.seconds > 0:
                break
            print("  link lost — reconnecting…")
            await asyncio.sleep(2)
    print("stopped → %s" % path)


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description="Shintai-OS BLE central — wireless telemetry capture.")
    ap.add_argument("--name", help="device-name prefix to connect to (e.g. ShintaiOS-fwd)")
    ap.add_argument("--seconds", type=int, default=0, help="capture for N seconds then stop (0 = until Ctrl+C)")
    ap.add_argument("--once", action="store_true", help="don't reconnect after a drop / no device")
    a = ap.parse_args()
    try:
        asyncio.run(main(a))
    except KeyboardInterrupt:
        print("\nstopped.")
