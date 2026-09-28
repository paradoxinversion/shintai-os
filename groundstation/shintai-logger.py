import serial
import serial.tools.list_ports
import csv
import os
import sys
import time
import datetime
import argparse
import threading

import bootroll  # shared cassette-futurism style (docs/style.md): palette + meters + boot ritual

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
    """Build the live cassette-futurism console (docs/style.md): a header, alert banners on
    breach, pulse-rifle segmented meters for the thresholded channels (range / CO₂ / thermal /
    lightning), then dotted-leader readout rows — one list of ANSI lines, redrawn in place."""
    b = bootroll
    g = vals.get
    W = b.WIDTH

    pod = g("board")                                   # Bunshin: which pod this stream is
    title = "SHINTAI-OS — LIVE" + (f" [{pod}]" if pod else "")
    out = ["  " + b.c(b.PHOSPHOR, title.ljust(W - 9)) + b.c(b.BONE_DIM, f"[#{samples}]"),
           "  " + b.c(b.GRID, "─" * W)]

    # ── banners: only when a channel is actually breached (§5.7) ──
    co2 = _num(g("co2_ppm"), 0)
    if g("alert") == "1":
        out.append(b.banner("OBJECT INSIDE 0.2 M — HOLD", "near"))
    if co2 and float(co2) >= 1500:
        out.append(b.banner(f"CO2 {int(float(co2))} PPM — VENTILATE", "mid"))

    # ── segmented meters (fuller / redder = more urgent) ──
    near, far = 200.0, 3000.0
    dists = [float(x) for x in (_num(g("distance_l_mm"), 0), _num(g("distance_r_mm"), 0)) if x]
    if dists:
        d = min(dists)
        frac = (far - d) / (far - near)                 # closer -> fuller
        band = "near" if d <= near else ("mid" if d <= 1000 else "far")
        out.append(b.meter("RANGE", frac, f"{d/1000:.2f} M", band))
    else:
        out.append(b.meter("RANGE", 0, "no reading", "far"))

    if co2:
        v = float(co2)
        band = "near" if v >= 1500 else ("mid" if v >= 1000 else "far")
        out.append(b.meter("CO2", (v - 400) / 1600.0, f"{int(v)} PPM", band))

    hs = _num(g("hotspot_delta"))
    if hs is not None and g("thermal_ctr"):
        v = float(hs)
        band = "near" if v >= 10 else ("mid" if v >= 5 else "far")
        out.append(b.meter("THERMAL", v / 20.0, f"{'+' if v >= 0 else ''}{v:.1f}°C", band))

    ls = g("lightning_strikes")                          # Enrai: present only with the AS3935
    if ls not in (None, ""):
        n = int(ls) if ls.isdigit() else 0
        km = g("lightning_km") or "0"
        near_txt, band = {"1": ("⚡ OVERHEAD", "near"), "0": ("— clear", "far"),
                          "": ("— clear", "far"), "63": ("out of range", "far")}.get(
                              km, (f"⚡ {km} KM", "mid"))
        out.append(b.meter("STORM", min(1.0, n / 50.0), f"{near_txt} ×{n}", band))

    out.append("")

    # ── dotted-leader readout rows (§5.3) ──
    pres = g("presence_detected")    # Hitokage forward radar — blank when the XM125 is absent
    if pres not in (None, ""):       # (a peak is a reflector, not a person — never "N people")
        pdist = _num(g("presence_distance_mm"), 0)
        out.append(b._row("PRESENCE",
                          f"{pdist} mm" if pres == "1" and pdist else "clear",
                          b.PHOSPHOR if pres == "1" else b.PHOSPHOR_DIM))
    hd = _num(g("heading_deg"), 0)
    out.append(b._row("HEADING", f"{hd}° {g('cardinal')}" if hd else "—", b.PHOSPHOR))
    ax = _num(g("accel_x"), 2)
    if ax:
        out.append(b._row("ACCEL", f"X{ax} Y{_num(g('accel_y'), 2)} Z{_num(g('accel_z'), 2)}", b.PHOSPHOR))
    air = _num(g("air_temp_c"))
    out.append(b._row("CLIMATE", f"{air}°C  {_num(g('humidity_pct'), 0)}%RH" if air else "warming up…",
                      b.PHOSPHOR if air else b.AMBER))
    press, gas = _num(g("pressure_hpa")), _num(g("gas_ohms"), 0)
    if press or gas:
        out.append(b._row("ENV", f"{press or '—'}hPa  {f'{float(gas)/1000:.0f}kΩ' if gas else '—'}", b.PHOSPHOR))
    pm25 = _num(g("pm2_5_ugm3"), 0)      # Enmu particulate — PM2.5 the headline, PM1.0/PM10 alongside
    if pm25:
        out.append(b._row("AQI", f"PM2.5 {pm25}  PM10 {_num(g('pm10_ugm3'), 0)}  "
                                 f"PM1.0 {_num(g('pm1_0_ugm3'), 0)} µg/m³", b.PHOSPHOR))
    lux = _num(g("light_lux"))       # Hizashi — illuminance the headline, raw IR alongside it
    if lux is not None:              # (the visible/IR ratio is what tells daylight from LED)
        out.append(b._row("LIGHT", f"{lux} lux  IR {g('light_ir') or '—'}", b.PHOSPHOR))
    uid = g("nfc_uid")               # Fuda — the held tag (blank once the ~5 s hold lapses)
    if uid:
        out.append(b._row("NFC", f"{uid}  {g('nfc_type')}", b.PHOSPHOR))
    if g("gps_fix") == "1":
        out.append(b._row("GPS", f"{_num(g('lat'), 4)},{_num(g('lon'), 4)} {_num(g('speed_kmh'))}km/h", b.PHOSPHOR))
    else:
        out.append(b._row("GPS", "no fix", b.AMBER))
    steps = g("steps")
    if steps not in (None, ""):
        out.append(b._row("STEPS", steps, b.PHOSPHOR))

    out += ["  " + b.c(b.GRID, "─" * W), "  " + b.c(b.BONE_DIM, f"logging → {csv_name}   Ctrl+C to stop")]
    return out


# Which CSV headers have already gone down the pipe. Two pods emit the SAME header, and
# a consumer wants the schema once, not once per pod (nor again each time a board is
# asked to re-print it mid-capture). Guarded by the same lock as the stdout writes.
_header_piped: dict[str, bool] = {}


def capture_port(port, csv_path, baud, live_panel, stop_event, lock, status, emit_data=False):
    """Capture one serial stream to its own CSV until [stop_event] is set.

    Each written row gets a trailing `host_ts` column — the host wall-clock at receipt,
    in epoch milliseconds. The two pods share no clock (`timestamp_ms` is each board's
    own millis() since ITS boot), so `host_ts` is the common timeline analyze.py aligns
    on. The board's own `board` column (fwd/aft) tags each row with its producing pod.

    STREAM SPLIT (so `shintai | …` composes). Everything a human reads — the boot
    roll-call, the live panel, per-port progress, the board's own human lines, errors —
    goes to **stderr**. **stdout** carries only the board's CSV verbatim (header + rows,
    no host_ts, no label prefix), and only when [emit_data] is set. That keeps the panel
    on the terminal while the data goes down the pipe. Rows stay self-identifying
    across pods via the contract's own `board` column, so no prefix is needed.
    """
    label = port_label(port)
    csv_name = os.path.basename(csv_path)
    header, header_written, samples, dash_on = [], False, 0, False

    try:
        ser = serial.Serial(port, baud, timeout=2)
    except serial.SerialException as e:
        with lock:
            print(f"[{label}] cannot open {port}: {e}", file=sys.stderr)
        return

    with ser, open(csv_path, "w", newline="") as f:
        writer = csv.writer(f)
        # Boot roll-call ritual (docs/style.md §5.8) — single-pod TTY only. Probe the
        # board's live 'I' scan and type out each module's presence before the capture
        # dashboard takes over. Non-fatal: a missing module or an unresponsive scan just
        # skips the ritual (the two-pod path stays a plain log).
        if live_panel:
            try:
                bootroll.play(bootroll.states_from_addresses(bootroll.probe_addresses(ser)),
                              out=sys.stderr)
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

                if line.startswith("timestamp_ms"):          # CSV header → file (+ pipe)
                    header = line.split(",") + ["host_ts"]
                    writer.writerow(header)
                    f.flush()
                    header_written = True
                    # A pipe consumer needs the schema before the rows, and needs it
                    # only once even with two pods sending their own identical header.
                    if emit_data:
                        with lock:
                            if not _header_piped.get(line):
                                _header_piped[line] = True
                                sys.stdout.write(line + "\n")
                                sys.stdout.flush()
                    continue

                if header_written and line[0].isdigit():      # CSV data row
                    row = line.split(",") + [str(int(time.time() * 1000))]
                    writer.writerow(row)
                    f.flush()
                    samples += 1
                    status[label] = samples
                    if emit_data:                              # the composable stream
                        with lock:
                            sys.stdout.write(line + "\n")
                            sys.stdout.flush()
                    if live_panel:
                        if not dash_on:
                            sys.stderr.write("\x1b[2J")        # clear once on first frame
                            dash_on = True
                        frame = dashboard(dict(zip(header, row)), samples, csv_name)
                        sys.stderr.write("\x1b[H" + "\n".join(l + "\x1b[K" for l in frame) + "\x1b[J")
                        sys.stderr.flush()
                    elif not emit_data:
                        with lock:
                            print(f"  [{label} #{samples} logged]", file=sys.stderr)
                    continue

                if live_panel and not dash_on:     # human/boot lines (single-port panel)
                    print(line, file=sys.stderr)
                elif not live_panel:
                    with lock:
                        print(f"  [{label}] {line}", file=sys.stderr)

            except KeyboardInterrupt:
                stop_event.set()
                break
            except Exception as e:                 # noqa: BLE001 — keep the capture alive
                with lock:
                    print(f"[{label}] error: {e}", file=sys.stderr)
                continue

    if live_panel and dash_on:
        sys.stderr.write("\x1b[J")


def main():
    ap = argparse.ArgumentParser(description="Shintai-OS serial capture (Bunshin: multi-pod).")
    ap.add_argument("--port", action="append", default=[],
                    help="serial port to capture; repeatable. Omit to auto-detect all matching devices.")
    ap.add_argument("--baud", type=int, default=BAUD, help=f"baud rate (default {BAUD})")
    ap.add_argument("--no-panel", action="store_true",
                    help="suppress the live ANSI panel; stream the board's CSV to stdout instead")
    args = ap.parse_args()

    # Stream split (see capture_port): the panel is a STDERR surface, so stdout is free
    # to be a pipe. Panel when stderr is a terminal and it wasn't waived; data on stdout
    # whenever stdout is redirected, or whenever there's no panel — so silencing one
    # channel never leaves both silent.
    panel = sys.stderr.isatty() and not args.no_panel
    emit_data = (not sys.stdout.isatty()) or not panel

    os.makedirs(LOG_DIR, exist_ok=True)

    ports = find_ports(args.port)
    if not ports:
        print("No serial ports found.", file=sys.stderr)
        sys.exit(1)

    timestamp = datetime.datetime.now().strftime("%Y%m%d_%H%M%S")
    jobs = []
    for port in ports:
        suffix = f"_{port_label(port)}" if len(ports) > 1 else ""
        csv_path = os.path.join(LOG_DIR, f"shintai_log_{timestamp}{suffix}.csv")
        jobs.append((port, csv_path))

    print(f"Capturing {len(ports)} port(s) at {args.baud} baud:", file=sys.stderr)
    for port, csv_path in jobs:
        print(f"  {port} → {os.path.basename(csv_path)}", file=sys.stderr)
    print(file=sys.stderr)

    stop_event = threading.Event()
    lock = threading.Lock()
    status = {}

    if len(jobs) == 1:
        # Single pod: keep the rich in-place ANSI status panel, in the main thread.
        port, csv_path = jobs[0]
        try:
            capture_port(port, csv_path, args.baud, panel, stop_event, lock, status, emit_data)
        except KeyboardInterrupt:
            stop_event.set()
    else:
        # Two pods (or more): one reader thread each (readline blocks), plain per-port
        # status lines. Host-time stamping keeps the streams alignable despite the reads
        # racing. Ctrl+C on the main thread signals every reader to stop.
        threads = [
            threading.Thread(target=capture_port,
                             args=(port, csv_path, args.baud, False, stop_event, lock,
                                   status, emit_data),
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
    print(f"\nLogging stopped. {total} samples across {len(jobs)} port(s) → {LOG_DIR}",
          file=sys.stderr)
    sys.exit(0)


if __name__ == "__main__":
    main()
