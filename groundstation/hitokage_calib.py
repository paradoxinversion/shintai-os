"""Hitokage calibration — measure what a radar+heading map is ALLOWED to claim.

The XM125 has no azimuth (specs/zokyo/hitokage.md, HK non-goals). Fusing its range
returns with `heading_deg` gives a direction for each return, but NOT an angular
resolution: a single sample paints an ARC whose width is the sensor's effective
detection spread, and the arc sits wherever the magnetometer says it does. So before
any map gets drawn, two numbers have to come off the bench:

  1. HOW WIDE is the arc a single detection may be painted at?   -> `arc`
  2. HOW WRONG is the heading that positions it?                 -> `tilt`, `spin`

Both are measured against the SHIPPED rig, not a datasheet. The antenna's nominal
beamwidth is not the quantity the display needs — what it needs is the angular span
over which THIS rig actually reports THAT target, which folds in target size, range,
CFAR threshold and mounting. This tool measures that and calls it the "effective
detection arc" rather than "beamwidth", because that is what the procedure returns.

Every mode is rate-limited by the firmware's telemetry cycle (UPDATE_MS = 1500 ms in
firmware/shintai-os/shintai-os.ino), so a 1.5 s gap sits between samples and every
angular figure is quantized by (rotation rate x 1.5 s). The tool reports each arc as a
BRACKET [inner, outer] spanning that quantization instead of a single confident number,
and warns when the rotation was fast enough that the bracket is uselessly wide.

Modes:
  probe   live readout — place the target, read its range, confirm distance mode
  arc     effective detection arc: rotate slowly through a lone reflector, N passes
  tilt    heading error vs. tilt, with a built-in azimuth-hold validity check
  spin    heading linearity + closure over >=2 revolutions, with a distortion test

Run:  conda activate shintai && python groundstation/hitokage_calib.py arc --target-mm 2000
      python groundstation/hitokage_calib.py --analyze logs/hitokage-calib-arc-....csv
"""
import os
import sys
import csv
import glob
import math
import time
import argparse
from datetime import datetime

HERE = os.path.dirname(os.path.abspath(__file__))
LOG_DIR = os.path.join(HERE, "logs")
sys.path.insert(0, HERE)

try:
    from bootroll import c, PHOSPHOR, AMBER, ALERT, BONE, BONE_DIM
except Exception:                                    # style is a nicety, not a dependency
    PHOSPHOR = AMBER = ALERT = BONE = BONE_DIM = None
    def c(_rgb, s):
        return s

# The firmware telemetry cycle. Every angular measurement here is quantized by this.
UPDATE_MS = 1500
CYCLE_S = UPDATE_MS / 1000.0

# Columns pulled from the CSV stream (CONTRACT.md).
COLS = ["timestamp_ms", "heading_deg", "presence_detected", "presence_distance_mm",
        "accel_x", "accel_y", "accel_z"]


# ── serial ────────────────────────────────────────────────────────────────────

def find_port():
    ports = glob.glob("/dev/cu.usbmodem*")
    return ports[0] if ports else None


def open_serial():
    import serial
    port = find_port()
    if not port:
        raise SystemExit("no board on USB — is it plugged in? (and close the IDE Serial Monitor)")
    s = serial.Serial(port, 115200, timeout=2)
    time.sleep(2.0)
    s.reset_input_buffer()
    s.write(b"b\n")                                  # both human + CSV streams, fresh header
    s.flush()
    time.sleep(1.0)
    return s, port


def read_rows(ser, seconds, sink=None):
    """Collect parsed CSV rows for `seconds`, calling sink(row) live as each arrives."""
    idx = {}
    out = []
    end = time.time() + seconds
    while time.time() < end:
        raw = ser.readline().decode("utf-8", "replace").strip()
        if not raw:
            continue
        if raw.startswith("timestamp_ms"):
            idx = {n: i for i, n in enumerate(raw.split(","))}
            continue
        if not raw[:1].isdigit() or not idx:
            continue
        cells = raw.split(",")
        row = {}
        for name in COLS:
            i = idx.get(name, -1)
            row[name] = cells[i].strip() if 0 <= i < len(cells) else ""
        row["t"] = time.time()
        out.append(row)
        if sink:
            sink(row)
    return out


def fnum(row, name):
    try:
        return float(row[name])
    except (ValueError, KeyError, TypeError):
        return None


# ── angle helpers ─────────────────────────────────────────────────────────────

def ang_diff(a, b):
    """Signed a-b wrapped to [-180, 180]."""
    return (a - b + 180.0) % 360.0 - 180.0


def unwrap(headings):
    """0/360 wrap-free continuous sequence. None entries pass through as None."""
    out, prev, offset = [], None, 0.0
    for h in headings:
        if h is None:
            out.append(None)
            continue
        if prev is not None:
            d = h - prev
            if d > 180.0:
                offset -= 360.0
            elif d < -180.0:
                offset += 360.0
        out.append(h + offset)
        prev = h
    return out


def vec_angle(a, b):
    """Angle in degrees between two 3-vectors."""
    na = math.sqrt(sum(x * x for x in a))
    nb = math.sqrt(sum(x * x for x in b))
    if na == 0 or nb == 0:
        return None
    d = sum(x * y for x, y in zip(a, b)) / (na * nb)
    return math.degrees(math.acos(max(-1.0, min(1.0, d))))


def median(xs):
    s = sorted(x for x in xs if x is not None)
    if not s:
        return None
    n = len(s)
    return s[n // 2] if n % 2 else 0.5 * (s[n // 2 - 1] + s[n // 2])


def pearson(xs, ys):
    pairs = [(x, y) for x, y in zip(xs, ys) if x is not None and y is not None]
    if len(pairs) < 3:
        return None
    n = len(pairs)
    mx = sum(p[0] for p in pairs) / n
    my = sum(p[1] for p in pairs) / n
    sxy = sum((p[0] - mx) * (p[1] - my) for p in pairs)
    sxx = sum((p[0] - mx) ** 2 for p in pairs)
    syy = sum((p[1] - my) ** 2 for p in pairs)
    if sxx <= 0 or syy <= 0:
        return None
    return sxy / math.sqrt(sxx * syy)


# ── capture log ───────────────────────────────────────────────────────────────

def log_path(mode):
    return os.path.join(LOG_DIR, "hitokage-calib-%s-%s.csv"
                        % (mode, datetime.now().strftime("%Y%m%d-%H%M")))


def write_log(path, mode, meta, rows):
    """Raw rows + a `#` meta line so --analyze can re-score without re-capturing.

    Named `hitokage-calib-*`, deliberately outside the load-bearing `shintai_log_*`
    glob (groundstation/CLAUDE.md) — hud.py/analyze.py must not pick these up.
    """
    os.makedirs(LOG_DIR, exist_ok=True)
    with open(path, "w", newline="") as f:
        f.write("# mode=%s %s\n" % (mode, " ".join("%s=%s" % kv for kv in sorted(meta.items()))))
        w = csv.writer(f)
        w.writerow(["segment", "t_s"] + COLS)
        t0 = rows[0]["t"] if rows else 0
        for r in rows:
            w.writerow([r.get("segment", ""), "%.2f" % (r["t"] - t0)] + [r[k] for k in COLS])
    return path


def read_log(path):
    meta, rows = {}, []
    with open(path) as f:
        first = f.readline()
        if first.startswith("#"):
            for tok in first.lstrip("#").split():
                if "=" in tok:
                    k, v = tok.split("=", 1)
                    meta[k] = v
        else:
            f.seek(0)
        for r in csv.DictReader(f):
            r["t"] = float(r["t_s"])
            rows.append(r)
    return meta, rows


# ── live readout ──────────────────────────────────────────────────────────────

def live_line(row, extra=""):
    h = fnum(row, "heading_deg")
    det = row.get("presence_detected", "")
    d = fnum(row, "presence_distance_mm")
    hs = "  ---" if h is None else "%5.1f" % h
    if det == "1" and d is not None:
        cont = c(PHOSPHOR, "CONTACT %5.0f mm" % d)
    elif det == "0":
        cont = c(BONE_DIM, "clear          ")
    else:
        cont = c(ALERT, "no radar       ")
    sys.stdout.write("\r  " + c(BONE, "hdg ") + c(AMBER, hs + "°") + "   " + cont + "  " + extra + "   ")
    sys.stdout.flush()


def countdown(label, secs):
    for i in range(secs, 0, -1):
        sys.stdout.write("\r  %s in %d... " % (label, i))
        sys.stdout.flush()
        time.sleep(1.0)
    sys.stdout.write("\r" + " " * 40 + "\r")
    sys.stdout.flush()


# ── mode: probe ───────────────────────────────────────────────────────────────

def mode_probe(args):
    ser, port = open_serial()
    print("connected on %s\n" % port)
    print("  Live readout. Place the target, read its range, confirm the radar is alive.")
    print("  A steady CONTACT at the target's range and nothing else = a clean scene for `arc`.")
    print("  Ctrl-C to stop.\n")
    try:
        read_rows(ser, args.secs, sink=lambda r: live_line(r))
    except KeyboardInterrupt:
        pass
    print("\n")


# ── mode: arc ─────────────────────────────────────────────────────────────────

ARC_BRIEF = """
  EFFECTIVE DETECTION ARC
  -----------------------
  Scene:  ONE strong reflector at a known range on an otherwise clear boresight.
          Nothing nearer than the target — `presence_distance_mm` is the NEAREST
          peak, so a closer object hides the target completely.
  Motion: start aimed well OFF the target, rotate SLOWLY and steadily straight
          through it, and finish well off the far side. Do not pause on it.
  Rate:   aim for <= %d deg/s. Samples land every %.1f s, so a fast sweep quantizes
          the arc into uselessness. Slower is strictly better here.
"""


def mode_arc(args):
    if args.target_mm is None:
        raise SystemExit("arc needs --target-mm (the target's range; read it off `probe`)")
    ser, port = open_serial()
    print("connected on %s" % port)
    print(ARC_BRIEF % (args.max_rate, CYCLE_S))
    print("  target %d mm  +/-%d mm tolerance   %d passes x %d s\n"
          % (args.target_mm, args.tol_mm, args.passes, args.pass_secs))

    rows = []
    for p in range(1, args.passes + 1):
        print(c(AMBER, "  PASS %d/%d" % (p, args.passes)) + "  — aim OFF the target, then sweep through")
        countdown("sweep begins", 5)
        got = read_rows(ser, args.pass_secs, sink=lambda r: live_line(r, "pass %d" % p))
        for r in got:
            r["segment"] = "pass%d" % p
        rows.extend(got)
        print("\n    %d samples\n" % len(got))

    # Meta keys avoid a `_mm`/`_deg` suffix on purpose: tools/check-contract.py flags
    # quoted column-shaped literals in groundstation/*.py as contract drift, and these
    # are run parameters, not CSV columns. Both are in mm.
    meta = {"target": args.target_mm, "tolerance": args.tol_mm}
    path = write_log(log_path("arc"), "arc", meta, rows)
    analyze_arc(meta, rows)
    print("\n  raw: %s" % path)


def analyze_arc(meta, rows):
    target = float(meta.get("target", 0))
    tol = float(meta.get("tolerance", 400))

    segs = {}
    for r in rows:
        segs.setdefault(r.get("segment", "pass1"), []).append(r)

    print("\n" + "=" * 72)
    print("EFFECTIVE DETECTION ARC   target %.0f mm +/-%.0f   %s"
          % (target, tol, datetime.now().strftime("%Y-%m-%d %H:%M")))
    print("=" * 72)

    inners, outers, centers, rates, offtarget = [], [], [], [], 0
    for name in sorted(segs):
        seg = segs[name]
        hs = unwrap([fnum(r, "heading_deg") for r in seg])
        on = []
        for i, r in enumerate(seg):
            if hs[i] is None:
                on.append(False)
                continue
            d = fnum(r, "presence_distance_mm")
            hit = r.get("presence_detected") == "1" and d is not None and abs(d - target) <= tol
            if r.get("presence_detected") == "1" and d is not None and not hit:
                offtarget += 1
            on.append(hit)

        idxs = [i for i, v in enumerate(on) if v]
        # Sweep rate from the pass as a whole — the quantization scale for this pass.
        valid = [(seg[i]["t"], hs[i]) for i in range(len(seg)) if hs[i] is not None]
        rate = None
        if len(valid) >= 2 and valid[-1][0] > valid[0][0]:
            rate = abs(valid[-1][1] - valid[0][1]) / (valid[-1][0] - valid[0][0])
            rates.append(rate)

        if not idxs:
            print("  %-7s  no on-target detection — target missed, out of range, or occluded" % name)
            continue

        lo, hi = min(idxs), max(idxs)
        # INNER: first-to-last confirmed detection — the arc is at least this wide.
        inner = abs(hs[hi] - hs[lo])
        # OUTER: out to the bracketing non-detections — the arc is at most this wide.
        # Detection could have begun anywhere in the 1.5 s gap on either side.
        prev_off = next((i for i in range(lo - 1, -1, -1) if hs[i] is not None), None)
        next_off = next((i for i in range(hi + 1, len(seg)) if hs[i] is not None), None)
        o_lo = hs[prev_off] if prev_off is not None else hs[lo]
        o_hi = hs[next_off] if next_off is not None else hs[hi]
        outer = abs(o_hi - o_lo)
        center = 0.5 * (hs[lo] + hs[hi]) % 360.0

        gaps = sum(1 for i in range(lo, hi + 1) if not on[i])
        inners.append(inner)
        outers.append(outer)
        centers.append(center)
        flag = ""
        if prev_off is None or next_off is None:
            flag = c(ALERT, "  [UNBRACKETED — sweep did not clear the target on both sides]")
        elif gaps:
            flag = c(AMBER, "  [%d dropout(s) inside the arc]" % gaps)
        print("  %-7s  arc %5.1f - %5.1f deg   center %5.1f deg   %d samples on target   %s deg/s%s"
              % (name, inner, outer, center, len(idxs),
                 "--" if rate is None else "%.1f" % rate, flag))

    if not inners:
        print("\n  NO ARC MEASURED. Nothing on target in any pass — re-run `probe` and")
        print("  confirm a steady CONTACT at the target's range before sweeping.")
        return

    mi, mo = median(inners), median(outers)
    print("\n  " + "-" * 68)
    print("  MEDIAN ARC:  %.1f deg (lower bound)  to  %.1f deg (upper bound)" % (mi, mo))
    print("  across %d pass(es); spread of the lower bound %.1f-%.1f deg"
          % (len(inners), min(inners), max(inners)))
    if centers:
        cs = [ang_diff(x, centers[0]) for x in centers]
        print("  arc center repeatability: %.1f deg spread (pass-to-pass)" % (max(cs) - min(cs)))
    if offtarget:
        print(c(AMBER, "  %d off-target detections seen — the scene was not clean; the bracket"
                       "\n  may include a second reflector rather than the target's own spread."
                       % offtarget))

    mr = median(rates)
    if mr:
        q = mr * CYCLE_S
        print("  quantization: %.1f deg/s x %.1f s = %.1f deg per sample" % (mr, CYCLE_S, q))
        if mo - mi > 0.6 * mo:
            print(c(ALERT, "  BRACKET TOO WIDE to be useful — the sweep outran the 1.5 s cycle."))
            print(c(ALERT, "  Re-run slower (target <= %d deg/s) before trusting this number." % 3))

    print("\n  WHAT THIS LICENSES: a single detection may be painted as an arc no")
    print("  narrower than %.0f deg. Drawing it tighter invents azimuth resolution" % mi)
    print("  the XM125 does not have. This figure is for THIS target at THIS range —")
    print("  it is an effective detection arc, not the antenna's beamwidth.")


# ── mode: tilt ────────────────────────────────────────────────────────────────

TILT_BRIEF = """
  HEADING ERROR vs. TILT
  ----------------------
  heading_deg is a raw atan2(mag.y, mag.x) with NO tilt compensation
  (firmware/shintai-os/shintai-os.ino:1543), so off-level attitude bleeds straight
  into azimuth. On-body the rig is rarely level, and the map has no way to know.

  Hold ONE fixed azimuth throughout — aim at a distant mark and keep aiming at it.
  Any heading change is error, because the azimuth never changed.

  1. LEVEL dwell   (%ds) — hold level and still. This is the reference.
  2. TILT sweep    (%ds) — pitch and roll through the range the rig sees on-body.
                           Keep pointing at the mark. Do not yaw.
  3. LEVEL again   (%ds) — the validity check. If heading does not come back, you
                           yawed, and the run is contaminated rather than damning.
"""


def mode_tilt(args):
    ser, port = open_serial()
    print("connected on %s" % port)
    print(TILT_BRIEF % (args.level_secs, args.tilt_secs, args.level_secs))

    rows = []
    for seg, secs, prompt in (("level_a", args.level_secs, "hold LEVEL and still, aimed at the mark"),
                              ("tilt", args.tilt_secs, "PITCH and ROLL — keep aiming at the mark"),
                              ("level_b", args.level_secs, "back to LEVEL, same mark")):
        print(c(AMBER, "  %s" % seg.upper()) + "  — %s" % prompt)
        countdown("starts", 5)
        got = read_rows(ser, secs, sink=lambda r: live_line(r, seg))
        for r in got:
            r["segment"] = seg
        rows.extend(got)
        print("\n    %d samples\n" % len(got))

    path = write_log(log_path("tilt"), "tilt", {}, rows)
    analyze_tilt({}, rows)
    print("\n  raw: %s" % path)


def analyze_tilt(_meta, rows):
    segs = {}
    for r in rows:
        segs.setdefault(r.get("segment", "tilt"), []).append(r)

    print("\n" + "=" * 72)
    print("HEADING ERROR vs. TILT   %s" % datetime.now().strftime("%Y-%m-%d %H:%M"))
    print("=" * 72)

    ref = segs.get("level_a", [])
    if not ref:
        print("  no level_a reference segment — cannot score.")
        return
    ref_vec = [median([fnum(r, ax) for r in ref]) for ax in ("accel_x", "accel_y", "accel_z")]
    ref_hdgs = [fnum(r, "heading_deg") for r in ref]
    ref_hdg = median(unwrap(ref_hdgs))
    if ref_hdg is None or None in ref_vec:
        print("  reference segment has no heading/accel — is the LIS3MDL present?")
        return
    ref_hdg %= 360.0
    ref_noise = max(abs(ang_diff(h, ref_hdg)) for h in ref_hdgs if h is not None)
    print("  reference heading %.1f deg (still-noise +/-%.1f deg over the level dwell)"
          % (ref_hdg, ref_noise))

    # Validity check FIRST — a contaminated run must not be read as a sensor result.
    lb = segs.get("level_b", [])
    hold = None
    if lb:
        lb_hdg = median([fnum(r, "heading_deg") for r in lb])
        if lb_hdg is not None:
            hold = abs(ang_diff(lb_hdg, ref_hdg))
            verdict = "OK" if hold <= max(3.0, ref_noise) else "CONTAMINATED"
            col = PHOSPHOR if verdict == "OK" else ALERT
            print("  azimuth-hold check: returned to %.1f deg, %.1f deg from reference — %s"
                  % (lb_hdg, hold, c(col, verdict)))

    sweep = segs.get("tilt", [])
    pts = []
    for r in sweep:
        h = fnum(r, "heading_deg")
        v = [fnum(r, ax) for ax in ("accel_x", "accel_y", "accel_z")]
        if h is None or None in v:
            continue
        t = vec_angle(v, ref_vec)
        if t is not None:
            pts.append((t, abs(ang_diff(h, ref_hdg))))
    if not pts:
        print("  no usable tilt samples.")
        return

    print("\n  tilt from level      samples   median |err|   max |err|")
    edges = [(0, 5), (5, 10), (10, 20), (20, 30), (30, 45), (45, 90), (90, 181)]
    first_bad = None
    for lo, hi in edges:
        b = [e for t, e in pts if lo <= t < hi]
        if not b:
            continue
        med, mx = median(b), max(b)
        print("  %3d-%3d deg          %5d    %8.1f     %7.1f" % (lo, hi, len(b), med, mx))
        if first_bad is None and med > 10.0:
            first_bad = (lo, hi)

    worst = max(e for _t, e in pts)
    print("\n  " + "-" * 68)
    print("  MAX HEADING SWING at a FIXED azimuth: %.1f deg" % worst)
    if first_bad:
        print("  median error passes 10 deg once tilt exceeds ~%d deg" % first_bad[0])
    if hold is not None and hold > max(3.0, ref_noise):
        print(c(ALERT, "  DO NOT READ THE ABOVE AS SENSOR ERROR — the azimuth-hold check failed,"))
        print(c(ALERT, "  so operator yaw and tilt error are mixed together here. Re-run."))
    else:
        print("  WHAT THIS COSTS THE MAP: an arc is placed this far from where the")
        print("  return actually came from, and nothing downstream can detect it. If")
        print("  this exceeds the arc width, tilt — not the radar — sets the resolution.")


# ── mode: spin ────────────────────────────────────────────────────────────────

SPIN_BRIEF = """
  HEADING LINEARITY + CLOSURE
  ---------------------------
  Mark a spot on the floor and a bearing to aim at. Rotate SLOWLY, STEADILY and
  in ONE direction through %d full revolutions, finishing aimed at the same mark.

  Two things come out. CLOSURE (did %d turns read as %d x 360 deg?) needs no steady
  rate to be valid. LINEARITY (was the reading proportional to the turn?) does —
  and a human is never truly steady, so the residual alone cannot separate hard/soft
  iron distortion from operator wobble.

  Repeatability by azimuth is a first cut at separating them, but it is NOT
  sufficient on its own: a wobble whose period divides the revolution repeats by
  sector just as cleanly as iron does. The test that actually discriminates is a
  DIRECTION FLIP — run this once cw and once ccw and `--compare` them. Iron is
  locked to the compass and survives the flip; turn-coupled wobble does not.
"""


def mode_spin(args):
    if args.revs < 2:
        raise SystemExit("spin needs --revs >= 2 — one revolution cannot separate distortion from wobble")
    ser, port = open_serial()
    print("connected on %s" % port)
    print(SPIN_BRIEF % (args.revs, args.revs, args.revs))
    print("  %d revolutions %s at ~%d s each = %d s. Ctrl-C early only ruins closure.\n"
          % (args.revs, args.direction.upper(), args.rev_secs, args.revs * args.rev_secs))

    countdown("rotation begins", 5)
    rows = read_rows(ser, args.revs * args.rev_secs, sink=lambda r: live_line(r, "spin"))
    for r in rows:
        r["segment"] = "spin"
    print("\n    %d samples\n" % len(rows))

    meta = {"revs": args.revs, "direction": args.direction}
    path = write_log(log_path("spin-" + args.direction), "spin", meta, rows)
    analyze_spin(meta, rows)
    print("\n  raw: %s" % path)


NB = 12                                              # 30-degree sectors


def spin_core(rows, revs):
    """Shared spin math: fit, residual, and per-revolution + pooled sector profiles.

    Returns None when there is too little to score.
    """
    ts = [r["t"] for r in rows]
    hs = unwrap([fnum(r, "heading_deg") for r in rows])
    pts = [(t, h) for t, h in zip(ts, hs) if h is not None]
    if len(pts) < 12:
        return None

    total = pts[-1][1] - pts[0][1]
    expected = 360.0 * revs * (1 if total >= 0 else -1)

    n = len(pts)
    mt = sum(p[0] for p in pts) / n
    mh = sum(p[1] for p in pts) / n
    stt = sum((p[0] - mt) ** 2 for p in pts)
    if stt <= 0:
        return None
    slope = sum((p[0] - mt) * (p[1] - mh) for p in pts) / stt
    resid = [(p[0], p[1], p[1] - (mh + slope * (p[0] - mt))) for p in pts]

    def profile(sub):
        bins = [[] for _ in range(NB)]
        for _t, h, e in sub:
            bins[int((h % 360.0) / (360.0 / NB)) % NB].append(e)
        return [median(x) if x else None for x in bins]

    profiles = []
    for k in range(revs):
        lo = pts[0][1] + expected * k / revs
        hi = pts[0][1] + expected * (k + 1) / revs
        a, b = (lo, hi) if lo <= hi else (hi, lo)
        profiles.append(profile([r for r in resid if a <= r[1] < b]))

    return {
        "pts": pts, "total": total, "expected": expected, "closure": total - expected,
        "slope": slope, "resid": resid,
        "rms": math.sqrt(sum(r[2] ** 2 for r in resid) / n),
        "max": max(abs(r[2]) for r in resid),
        "profiles": profiles, "pooled": profile(resid),
        "secs": pts[-1][0] - pts[0][0],
    }


def analyze_spin(meta, rows):
    revs = int(meta.get("revs", 2))
    direction = meta.get("direction", "?")

    print("\n" + "=" * 72)
    print("HEADING LINEARITY + CLOSURE   %d revolutions %s   %s"
          % (revs, direction.upper(), datetime.now().strftime("%Y-%m-%d %H:%M")))
    print("=" * 72)

    s = spin_core(rows, revs)
    if not s:
        print("  too few usable samples to score.")
        return

    closure = s["closure"]
    print("  swept %.1f deg over %.0f s   (expected %.0f)" % (s["total"], s["secs"], s["expected"]))
    col = PHOSPHOR if abs(closure) <= 5 else (AMBER if abs(closure) <= 15 else ALERT)
    print("  CLOSURE ERROR: %s  — the reading is this wrong after a round trip to the"
          % c(col, "%+.1f deg" % closure))
    print("  same physical mark. Needs no assumption about rotation rate.")

    print("\n  mean rate %.1f deg/s   residual from a constant-rate fit: %.1f deg RMS, %.1f deg max"
          % (abs(s["slope"]), s["rms"], s["max"]))
    print("  (an UPPER BOUND on distortion — it still contains operator unsteadiness)")

    profiles = s["profiles"]
    print("\n  median residual per 30 deg sector, by revolution:")
    print("  sector    " + "".join("  rev%d " % (i + 1) for i in range(len(profiles))))
    for j in range(NB):
        cells = "".join("  %5s" % ("--" if p[j] is None else "%.1f" % p[j]) for p in profiles)
        print("  %3d-%3d %s" % (j * 30, (j + 1) * 30, cells))

    usable = [p for p in profiles if sum(1 for v in p if v is not None) >= NB // 2]
    print("\n  " + "-" * 68)
    if len(usable) < 2:
        print("  not enough coverage per revolution to correlate.")
        return
    rs = [x for x in (pearson(usable[i], usable[i + 1]) for i in range(len(usable) - 1))
          if x is not None]
    if not rs:
        print("  rev-to-rev test inconclusive (residual profiles too flat to correlate).")
        return
    r = sum(rs) / len(rs)
    print("  rev-to-rev correlation of the sector profile: r = %+.2f" % r)

    if r >= 0.6:
        print(c(AMBER, "  The residual REPEATS by compass sector across revolutions."))
        print("  Consistent with an azimuth-locked distortion (hard/soft iron on the rig)")
        print(c(ALERT, "  — but NOT yet distinguished from it. A wobble whose period happens to"))
        print(c(ALERT, "  divide the turn (arm mechanics repeating each revolution) produces an"))
        print(c(ALERT, "  identical signature; in bench synthetics it also scored r = +0.99."))
        print("  SETTLE IT: run `spin --direction ccw` and `--compare` the two. Iron is")
        print("  locked to the compass and survives a direction flip; turn-coupled wobble")
        print("  does not.")
    elif r <= 0.2:
        print(c(PHOSPHOR, "  The residual does NOT repeat by sector."))
        print("  Consistent with operator unsteadiness rather than azimuth-locked")
        print("  distortion. This bounds distortion below the %.1f deg RMS above; it" % s["rms"])
        print("  does not measure it. A turntable at constant rate would.")
    else:
        print("  Ambiguous (r between 0.2 and 0.6) — neither hypothesis is discriminated.")
        print("  More revolutions, or the opposite-direction --compare, would settle it.")


def compare_spins(path_a, path_b):
    """Correlate two spin runs' sector profiles — the direction-flip discriminator."""
    ma, ra = read_log(path_a)
    mb, rb = read_log(path_b)
    if ma.get("mode") != "spin" or mb.get("mode") != "spin":
        raise SystemExit("--compare takes two `spin` captures")
    sa = spin_core(ra, int(ma.get("revs", 2)))
    sb = spin_core(rb, int(mb.get("revs", 2)))
    if not sa or not sb:
        raise SystemExit("one of the captures is too short to score")
    da, db = ma.get("direction", "?"), mb.get("direction", "?")

    print("\n" + "=" * 72)
    print("SPIN COMPARE   A=%s (%s)   B=%s (%s)"
          % (os.path.basename(path_a), da, os.path.basename(path_b), db))
    print("=" * 72)
    print("  sector      A       B")
    for j in range(NB):
        va, vb = sa["pooled"][j], sb["pooled"][j]
        print("  %3d-%3d %6s  %6s" % (j * 30, (j + 1) * 30,
                                      "--" if va is None else "%.1f" % va,
                                      "--" if vb is None else "%.1f" % vb))
    r = pearson(sa["pooled"], sb["pooled"])
    print("\n  " + "-" * 68)
    if r is None:
        print("  profiles too flat or too sparse to correlate.")
        return
    print("  cross-run correlation of the sector profile: r = %+.2f" % r)

    if da == db or "?" in (da, db):
        print(c(AMBER, "  WEAK TEST: both runs are the same direction (or direction unrecorded),"))
        print(c(AMBER, "  so this repeats the same confound rather than breaking it. Capture one"))
        print(c(AMBER, "  run each way with --direction cw / --direction ccw."))
        return

    if r >= 0.6:
        print(c(AMBER, "  The sector profile SURVIVES a direction flip."))
        print("  That discriminates: turn-coupled operator wobble reverses with the turn,")
        print("  an azimuth-locked field error does not. This is hard/soft iron on the rig,")
        print("  and the A/B sector medians above are a usable first correction table.")
    elif r <= 0.2:
        print(c(PHOSPHOR, "  The sector profile does NOT survive a direction flip."))
        print("  The within-run repeat was turn-coupled — operator or mechanical, not the")
        print("  magnetometer. Do not build a per-azimuth correction from it.")
    else:
        print("  Ambiguous (r between 0.2 and 0.6). Neither hypothesis is discriminated;")
        print("  more revolutions per run is the cheapest next step.")


# ── entry ─────────────────────────────────────────────────────────────────────

ANALYZERS = {"arc": analyze_arc, "tilt": analyze_tilt, "spin": analyze_spin}


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("mode", nargs="?", choices=["probe", "arc", "tilt", "spin"])
    ap.add_argument("--analyze", metavar="CSV", help="re-score a saved capture, no board needed")
    ap.add_argument("--compare", nargs=2, metavar=("A", "B"),
                    help="spin: correlate two captures' sector profiles (use cw vs ccw)")
    ap.add_argument("--secs", type=int, default=600, help="probe: seconds to stream (default 600)")
    ap.add_argument("--target-mm", type=int, help="arc: the target's range in mm")
    ap.add_argument("--tol-mm", type=int, default=400, help="arc: on-target window (default 400)")
    ap.add_argument("--passes", type=int, default=5, help="arc: sweeps to average (default 5)")
    ap.add_argument("--pass-secs", type=int, default=30, help="arc: seconds per sweep (default 30)")
    ap.add_argument("--max-rate", type=int, default=3, help="arc: advisory deg/s ceiling (default 3)")
    ap.add_argument("--level-secs", type=int, default=15, help="tilt: seconds per level dwell")
    ap.add_argument("--tilt-secs", type=int, default=60, help="tilt: seconds of tilt sweep")
    ap.add_argument("--revs", type=int, default=2, help="spin: revolutions (min 2)")
    ap.add_argument("--rev-secs", type=int, default=90, help="spin: seconds per revolution")
    ap.add_argument("--direction", choices=["cw", "ccw"], default="cw",
                    help="spin: which way you turned — the --compare flip test needs both")
    args = ap.parse_args()

    if args.compare:
        compare_spins(args.compare[0], args.compare[1])
        return

    if args.analyze:
        meta, rows = read_log(args.analyze)
        mode = meta.get("mode")
        if mode not in ANALYZERS:
            raise SystemExit("cannot tell which mode %s came from (no `# mode=` line)" % args.analyze)
        ANALYZERS[mode](meta, rows)
        return

    if not args.mode:
        ap.error("give a mode (probe/arc/tilt/spin) or --analyze")
    {"probe": mode_probe, "arc": mode_arc, "tilt": mode_tilt, "spin": mode_spin}[args.mode](args)


if __name__ == "__main__":
    main()
