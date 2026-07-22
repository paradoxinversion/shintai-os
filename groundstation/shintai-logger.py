import serial
import serial.tools.list_ports
import csv
import os
import sys
import time
import datetime
import argparse
import threading

# ── Config ──────────────────────────────────────────────
BAUD = 115200
LOG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "logs")
PORT_HINTS = ["QT Py", "ESP32", "USB Serial", "CP210", "CH340"]
# ─────────────────────────────────────────────────────────


def find_ports(explicit):
    """Resolve the serial ports to capture.

    Bunshin: the rig can be TWO pods (fwd/aft), so this returns a LIST. Explicit
    --port flags win; otherwise auto-detect *all* matching devices (a two-board rig
    surfaces both). Falls back to the interactive single-pick only when nothing matches.
    """
    if explicit:
        return explicit
    ports = serial.tools.list_ports.comports()
    matched = [p.device for p in ports if any(x in p.description for x in PORT_HINTS)]
    if matched:
        return matched
    if not ports:
        return []
    print("Available ports:")
    for i, p in enumerate(ports):
        print(f"  {i}: {p.device} — {p.description}")
    choice = input("Enter port number: ")
    return [ports[int(choice)].device]


def port_label(port):
    """A short filename/status tag for a port (e.g. /dev/cu.usbmodem101 -> usbmodem101)."""
    return os.path.basename(port).replace("cu.", "").replace("tty.", "")


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
    pod = g("board")                          # Bunshin: which pod this stream is
    title = "SHINTAI-OS — live" + (f" [{pod}]" if pod else "")
    out = [title.ljust(W - 8) + f"[#{samples}]", "─" * W]

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


def capture_port(port, csv_path, baud, live_panel, stop_event, lock, status):
    """Capture one serial stream to its own CSV until [stop_event] is set.

    Each written row gets a trailing `host_ts` column — the host wall-clock at receipt,
    in epoch milliseconds. The two pods share no clock (`timestamp_ms` is each board's
    own millis() since ITS boot), so `host_ts` is the common timeline analyze.py aligns
    on. The board's own `board` column (fwd/aft) tags each row with its producing pod.
    """
    label = port_label(port)
    csv_name = os.path.basename(csv_path)
    header, header_written, samples, dash_on = [], False, 0, False

    try:
        ser = serial.Serial(port, baud, timeout=2)
    except serial.SerialException as e:
        with lock:
            print(f"[{label}] cannot open {port}: {e}")
        return

    with ser, open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        # Boot roll-call ritual (docs/style.md §5.8) — single-pod TTY only. Probe the
        # board's live 'I' scan and type out each module's presence before the capture
        # dashboard takes over. Non-fatal: a missing module or an unresponsive scan just
        # skips the ritual (the two-pod path stays a plain log).
        if live_panel:
            try:
                import bootroll
                bootroll.play(bootroll.states_from_addresses(bootroll.probe_addresses(ser)))
            except Exception:  # noqa: BLE001 — never block capture on the flourish
                pass
        # Ask the sketch for both human + CSV output and a fresh CSV header, so we
        # capture data even when connecting mid-stream (no board reset).
        ser.write(b"b\n")

        while not stop_event.is_set():
            try:
                line = ser.readline().decode("utf-8", errors="replace").strip()
                if not line:
                    continue

                if line.startswith("timestamp_ms"):          # CSV header → file only
                    header = line.split(",") + ["host_ts"]
                    writer.writerow(header)
                    f.flush()
                    header_written = True
                    continue

                if header_written and line[0].isdigit():      # CSV data row
                    row = line.split(",") + [str(int(time.time() * 1000))]
                    writer.writerow(row)
                    f.flush()
                    samples += 1
                    status[label] = samples
                    if live_panel:
                        if not dash_on:
                            sys.stdout.write("\x1b[2J")        # clear once on first frame
                            dash_on = True
                        frame = dashboard(dict(zip(header, row)), samples, csv_name)
                        sys.stdout.write("\x1b[H" + "\n".join(l + "\x1b[K" for l in frame) + "\x1b[J")
                        sys.stdout.flush()
                    else:
                        with lock:
                            print(f"  [{label} #{samples} logged]")
                    continue

                if live_panel and not dash_on:     # human/boot lines (single-port panel)
                    print(line)
                elif not live_panel:
                    with lock:
                        print(f"  [{label}] {line}")

            except KeyboardInterrupt:
                stop_event.set()
                break
            except Exception as e:                 # noqa: BLE001 — keep the capture alive
                with lock:
                    print(f"[{label}] error: {e}")
                continue

    if live_panel and dash_on:
        sys.stdout.write("\x1b[J")


def main():
    ap = argparse.ArgumentParser(description="Shintai-OS serial capture (Bunshin: multi-pod).")
    ap.add_argument("--port", action="append", default=[],
                    help="serial port to capture; repeatable. Omit to auto-detect all matching devices.")
    ap.add_argument("--baud", type=int, default=BAUD, help=f"baud rate (default {BAUD})")
    args = ap.parse_args()

    os.makedirs(LOG_DIR, exist_ok=True)

    ports = find_ports(args.port)
    if not ports:
        print("No serial ports found.")
        sys.exit(1)

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    jobs = []
    for port in ports:
        suffix = f"_{port_label(port)}" if len(ports) > 1 else ""
        csv_path = os.path.join(LOG_DIR, f"shintai_log_{timestamp}{suffix}.csv")
        jobs.append((port, csv_path))

    print(f"Capturing {len(ports)} port(s) at {args.baud} baud:")
    for port, csv_path in jobs:
        print(f"  {port} → {os.path.basename(csv_path)}")
    print()

    stop_event = threading.Event()
    lock = threading.Lock()
    status = {}

    if len(jobs) == 1:
        # Single pod: keep the rich in-place ANSI status panel, in the main thread.
        port, csv_path = jobs[0]
        try:
            capture_port(port, csv_path, args.baud, sys.stdout.isatty(), stop_event, lock, status)
        except KeyboardInterrupt:
            stop_event.set()
    else:
        # Two pods (or more): one reader thread each (readline blocks), plain per-port
        # status lines. Host-time stamping keeps the streams alignable despite the reads
        # racing. Ctrl+C on the main thread signals every reader to stop.
        threads = [
            threading.Thread(target=capture_port,
                             args=(port, csv_path, args.baud, False, stop_event, lock, status),
                             daemon=True)
            for port, csv_path in jobs
        ]
        for t in threads:
            t.start()
        try:
            while any(t.is_alive() for t in threads):
                for t in threads:
                    t.join(timeout=0.3)
        except KeyboardInterrupt:
            stop_event.set()
            for t in threads:
                t.join(timeout=2)

    total = sum(status.values())
    print(f"\nLogging stopped. {total} samples across {len(jobs)} port(s) → {LOG_DIR}")
    sys.exit(0)


if __name__ == "__main__":
    main()
