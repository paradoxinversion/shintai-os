import serial
import serial.tools.list_ports
import csv
import os
import sys
import datetime

# ── Config ──────────────────────────────────────────────
BAUD = 115200
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
# ─────────────────────────────────────────────────────────


def find_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if any(x in p.description for x in ["QT Py", "ESP32", "USB Serial", "CP210", "CH340"]):
            return p.device
    print("Available ports:")
    for i, p in enumerate(ports):
        print(f"  {i}: {p.device} — {p.description}")
    choice = input("Enter port number: ")
    return ports[int(choice)].device


def _num(v, dec=1):
    """Format a CSV field as a fixed-decimal number, or None if blank/invalid."""
    try:
        return f"{float(v):.{dec}f}"
    except (TypeError, ValueError):
        return None


def dashboard(vals, samples, csv_name):
    """Build the live status panel (list of lines) from the latest CSV row."""
    g = vals.get
    W = 38
    out = [f"SHINTAI-OS — live".ljust(W - 8) + f"[#{samples}]", "─" * W]

    dl = f"{g('distance_l_mm')} mm" if g("distance_l_mm") else "no reading"
    dr = f"{g('distance_r_mm')} mm" if g("distance_r_mm") else "no reading"
    dist = f"L {dl}  R {dr}"        # rear dual-arc: left (ch0) / right (ch1)
    if g("alert") == "1":
        dist += "   ⚠ TOO CLOSE"
    out.append(f"DISTANCE   {dist}")

    hd = _num(g("heading_deg"), 0)
    out.append(f"HEADING    {hd}° {g('cardinal')}" if hd else "HEADING    —")

    ax, ay, az = _num(g("accel_x")), _num(g("accel_y")), _num(g("accel_z"))
    if ax:
        out.append(f"ACCEL      X {ax}  Y {ay}  Z {az} m/s²")

    ctr, tmin, tmax = _num(g("thermal_ctr")), _num(g("thermal_min")), _num(g("thermal_max"))
    if ctr:
        line = f"THERMAL    ctr {ctr}°C  scene {tmin}–{tmax}°C"
        hs = _num(g("hotspot_delta"))
        if hs and float(hs) >= 5:
            line += f"  (+{hs}°C hot)"
        out.append(line)

    air = _num(g("air_temp_c"))
    if air:
        out.append(f"CLIMATE    {air}°C  {_num(g('humidity_pct'), 0)}%RH  {g('co2_ppm')}ppm")
    else:
        out.append("CLIMATE    warming up…")

    press, gas = _num(g("pressure_hpa")), _num(g("gas_ohms"), 0)
    if press or gas:
        out.append(f"ENV        {press or '—'} hPa  {f'{float(gas)/1000:.1f} kΩ' if gas else '—'} gas")

    if g("gps_fix") == "1":
        out.append(f"GPS        {_num(g('lat'), 5)},{_num(g('lon'), 5)}  "
                   f"{_num(g('speed_kmh'))}km/h  {g('sats')} sats")
    else:
        out.append("GPS        no fix")

    out += ["─" * W, f"logging → {csv_name}   Ctrl+C to stop"]
    return out


def main():
    os.makedirs(LOG_DIR, exist_ok=True)

    port = find_port()
    print(f"Connecting to {port} at {BAUD} baud...")

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    csv_file = os.path.join(LOG_DIR, f"shintai_log_{timestamp}.csv")
    csv_name = os.path.basename(csv_file)
    print(f"Logging CSV to: {csv_file}\n")

    live = sys.stdout.isatty()   # refreshing panel in a real terminal; plain text when piped
    header, header_written, samples, dash_on = [], False, 0, False

    with serial.Serial(port, BAUD, timeout=2) as ser, \
         open(csv_file, "w", newline="") as f:

        writer = csv.writer(f)

        # Ask the sketch for both human + CSV output and a fresh CSV header,
        # so we capture data even when connecting mid-stream (no board reset).
        ser.write(b"b\n")

        while True:
            try:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if not line:
                    continue

                if line.startswith("timestamp_ms"):          # CSV header → file only
                    header = line.split(",")
                    writer.writerow(header)
                    f.flush()
                    header_written = True
                    continue

                if header_written and line[0].isdigit():      # CSV data row
                    row = line.split(",")
                    writer.writerow(row)
                    f.flush()
                    samples += 1
                    if live:
                        if not dash_on:
                            sys.stdout.write("\x1b[2J")        # clear once on first frame
                            dash_on = True
                        frame = dashboard(dict(zip(header, row)), samples, csv_name)
                        sys.stdout.write("\x1b[H" + "\n".join(l + "\x1b[K" for l in frame) + "\x1b[J")
                        sys.stdout.flush()
                    else:
                        print(f"  [#{samples} logged]")
                    continue

                if not dash_on:        # human/boot lines (suppressed once the panel is live)
                    print(line)

            except KeyboardInterrupt:
                if dash_on:
                    sys.stdout.write("\x1b[J")
                print(f"\nLogging stopped. {samples} samples saved to:\n  {csv_file}")
                sys.exit(0)
            except Exception as e:
                print(f"Error: {e}")
                continue


if __name__ == "__main__":
    main()
