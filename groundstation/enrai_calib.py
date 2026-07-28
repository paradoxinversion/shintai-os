"""Enrai calibration sweep — measure the AS3935 against a LIVE storm, not a datasheet.

The AS3935's antenna trim can only be measured properly with the IRQ pin frequency-counted
against the LCO, and this rig is Qwiic-only (no IRQ). So tuning here is EMPIRICAL: run each
candidate config against real strikes and keep the one that discriminates best.

The trap this tool exists to avoid: a storm MOVES. Measure config A for five minutes, then
config B for five minutes, and the difference you "found" is mostly the storm getting closer.
So configs are interleaved in short alternating segments (A B A B A B ...) and each config's
segments are pooled — storm drift then hits every config roughly equally instead of loading
onto whichever one ran last.

What's scored, in priority order:
  1. RANGE SPREAD — the point of the sensor. A config that reports every strike at 1 km is
     saturated and useless even if it "detects more". Scored as distinct distance bins seen.
  2. STRIKE YIELD — strikes actually validated per minute.
  3. DISTURBER RATIO — disturbers per strike; high means man-made noise is leaking through
     as events and some "strikes" are probably junk.

Strikes are detected by watching lightning_strikes INCREMENT in the CSV stream (the contract
carries a last-strike snapshot + monotonic count, not an event feed — see specs/zokyo/enrai.md),
and the km/energy read at that edge is the strike that caused it.

Run:  conda activate shintai && python groundstation/enrai_calib.py [--segment SEC] [--rounds N]
"""
import os
import sys
import glob
import time
import argparse
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "logs")

# Each candidate: a label plus the serial keystrokes that reach it FROM BOOT DEFAULTS
# (gain=in spike=2 wdog=1 tune=0). Serial exposes 'o' gain-toggle, 's'/'S' spike -/+,
# 'y'/'Y' tune-cap -/+, 'x' clear-stats, 'z' RCO re-trim. Watchdog is deliberately NOT
# swept here: it's BLE-only (no serial key), so it stays at the boot value throughout.
CANDIDATES = [
    ("indoor/spike2",  ""),       # boot default — maximum sensitivity, the current shipped config
    ("outdoor/spike2", "o"),      # lower AFE gain: the spec's remedy for a saturated strong storm
    ("outdoor/spike4", "oSS"),    # lower gain + stricter waveform validation
]


def find_port():
    ports = glob.glob("/dev/cu.usbmodem*")
    return ports[0] if ports else None


def open_serial():
    import serial
    port = find_port()
    if not port:
        raise SystemExit("no board on USB — is it plugged in?")
    s = serial.Serial(port, 115200, timeout=1)
    time.sleep(2.0)
    s.reset_input_buffer()
    return s, port


def drain_status(ser, timeout=4.0):
    """Send 'K' and return (shadow_line, chip_line)."""
    ser.write(b"K")
    ser.flush()
    shadow = chip = ""
    end = time.time() + timeout
    while time.time() < end and not (shadow and chip):
        ln = ser.readline().decode("utf-8", "replace").strip()
        if ln.startswith("<<<ENRAI-CHIP"):
            chip = ln
        elif ln.startswith("<<<ENRAI "):
            shadow = ln
    return shadow, chip


def parse_kv(line):
    """'<<<ENRAI a=1 b=2>>>' -> {'a': '1', 'b': '2'}"""
    body = line.strip().lstrip("<").rstrip(">")
    out = {}
    for tok in body.split():
        if "=" in tok:
            k, v = tok.split("=", 1)
            out[k] = v
    return out


def apply_config(ser, keys):
    """Reset Enrai to boot defaults, then apply this candidate's keystrokes.

    There is no 'reset to defaults' serial key, so the baseline is reached by walking the
    knobs back to their boot values explicitly -- gain toggles, so it's re-read rather than
    assumed. Then 'z' re-trims the RCO: the trim follows the antenna, so it is stale after
    any change and a config judged on a stale timebase isn't the config you think it is.
    """
    _, chip = drain_status(ser)
    c = parse_kv(chip)
    # gain: 0x12 = INDOOR (boot default). Toggle back if we're sitting on OUTDOOR.
    if c.get("gain", "0x12").lower() != "0x12":
        ser.write(b"o"); ser.flush(); time.sleep(0.2)
    # spike: walk down to the boot value of 2.
    for _ in range(int(c.get("spike", 2)) - 2):
        ser.write(b"s"); ser.flush(); time.sleep(0.2)
    for _ in range(2 - int(c.get("spike", 2))):
        ser.write(b"S"); ser.flush(); time.sleep(0.2)

    for k in keys:
        ser.write(k.encode()); ser.flush(); time.sleep(0.25)
    ser.write(b"z"); ser.flush(); time.sleep(0.6)   # re-trim RCO for the new antenna config
    ser.write(b"x"); ser.flush(); time.sleep(0.3)   # clear storm statistics
    return drain_status(ser)


def csv_index(header_line):
    cols = header_line.strip().split(",")
    return {n: i for i, n in enumerate(cols)}


def run_segment(ser, seconds, idx_holder, state):
    """Watch the CSV stream for `seconds`, returning strikes seen as [(km, energy), ...].

    Detects a strike by the lightning_strikes counter INCREMENTING, which is the only
    edge available -- the CSV carries a snapshot, so a second strike at the same distance
    is invisible without the counter.
    """
    strikes = []
    end = time.time() + seconds
    while time.time() < end:
        raw = ser.readline().decode("utf-8", "replace").strip()
        if not raw:
            continue
        if raw.startswith("timestamp_ms"):
            idx_holder.clear()
            idx_holder.update(csv_index(raw))
            continue
        if not raw[:1].isdigit() or not idx_holder:
            continue
        cells = raw.split(",")
        def cell(name):
            i = idx_holder.get(name, -1)
            return cells[i].strip() if 0 <= i < len(cells) else ""
        try:
            n = int(cell("lightning_strikes") or 0)
            km = int(cell("lightning_km") or 0)
            e = int(cell("lightning_energy") or 0)
        except ValueError:
            continue
        if state["last_n"] is None:
            state["last_n"] = n
            continue
        if n > state["last_n"]:
            # The counter can jump by >1 between 1.5 s CSV rows; we only ever see the most
            # recent strike's km/energy, so extra strikes are counted but not ranged. Recording
            # that honestly beats inventing distances for them.
            for _ in range(n - state["last_n"] - 1):
                strikes.append((None, None))
            strikes.append((km, e))
            print("    strike n=%d  km=%s  e=%s" % (n, km, e))
        state["last_n"] = n
    return strikes


def bin_label(km):
    if km is None:
        return "unranged"
    if km <= 1:
        return "overhead"
    if km == 63:
        return "out_of_range"
    if km <= 5:
        return "2-5km"
    if km <= 12:
        return "6-12km"
    if km <= 25:
        return "13-25km"
    return "26km+"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--segment", type=int, default=120, help="seconds per segment (default 120)")
    ap.add_argument("--rounds", type=int, default=3, help="interleaved passes over all configs")
    args = ap.parse_args()

    ser, port = open_serial()
    print("connected on %s" % port)
    ser.write(b"b\n"); ser.flush()   # both human + CSV streams, fresh header
    time.sleep(1.0)

    results = {name: {"strikes": [], "dist": 0, "noise": 0, "secs": 0.0} for name, _ in CANDIDATES}
    idx_holder, state = {}, {"last_n": None}

    total = args.rounds * len(CANDIDATES) * args.segment
    print("sweep: %d rounds x %d configs x %ds = %.1f min total\n"
          % (args.rounds, len(CANDIDATES), args.segment, total / 60.0))

    for rnd in range(args.rounds):
        for name, keys in CANDIDATES:
            print("[round %d/%d] %s" % (rnd + 1, args.rounds, name))
            shadow, chip = apply_config(ser, keys)
            print("    %s" % chip)
            state["last_n"] = None
            t0 = time.time()
            got = run_segment(ser, args.segment, idx_holder, state)
            shadow, _ = drain_status(ser)
            k = parse_kv(shadow)
            r = results[name]
            r["strikes"].extend(got)
            r["dist"] += int(k.get("disturbers", 0))
            r["noise"] += int(k.get("noise", 0))
            r["secs"] += time.time() - t0
            print("    -> %d strikes, %s disturbers this segment\n" % (len(got), k.get("disturbers", 0)))

    # ── Report ────────────────────────────────────────────────────────────────
    print("\n" + "=" * 68)
    print("ENRAI CALIBRATION SWEEP  %s" % datetime.now().strftime("%Y-%m-%d %H:%M"))
    print("=" * 68)
    order = ["overhead", "2-5km", "6-12km", "13-25km", "26km+", "out_of_range", "unranged"]
    for name, _ in CANDIDATES:
        r = results[name]
        n = len(r["strikes"])
        mins = r["secs"] / 60.0
        bins = {}
        for km, _e in r["strikes"]:
            bins[bin_label(km)] = bins.get(bin_label(km), 0) + 1
        ranged = [b for b in order[:-1] if bins.get(b)]
        print("\n%-16s  %5.1f strikes/min   %d disturbers   %d noise"
              % (name, n / mins if mins else 0, r["dist"], r["noise"]))
        print("  range spread: %d distinct bins" % len(ranged))
        for b in order:
            if bins.get(b):
                print("    %-13s %s (%d)" % (b, "#" * bins[b], bins[b]))
        if n:
            print("  disturbers/strike: %.2f" % (r["dist"] / float(n)))

    print("\nPick by: range spread first (a config that pins everything at 'overhead' is")
    print("saturated), then strikes/min, then the lowest disturbers/strike.")

    out = os.path.join(LOG_DIR, "enrai-calib-%s.csv" % datetime.now().strftime("%Y%m%d-%H%M"))
    with open(out, "w") as fh:
        fh.write("config,km,energy\n")
        for name, _ in CANDIDATES:
            for km, e in results[name]["strikes"]:
                fh.write("%s,%s,%s\n" % (name, "" if km is None else km, "" if e is None else e))
    print("\nraw strikes -> %s" % out)
    ser.close()


if __name__ == "__main__":
    main()
