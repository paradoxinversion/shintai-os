"""SHINTAI-OS boot roll-call — the cassette-futurism launch ritual (docs/style.md §5.8).

A scanline sweep, the SHINTAI-OS mark framed in reticle ticks, then a typed
module-by-module roll-call keyed to each sensor's LIVE presence
(`OK` / `— OFFLINE` / `FAULT`), ending `SHINTAI-OS // ONLINE`. It turns the
firmware's graceful-degradation architecture into the launch centerpiece — the
thing that makes starting a capture feel like booting a real machine.

Presence is probed from the board's `'I'` I2C scan (authoritative, live). Colors are
the style-guide tokens as ANSI truecolor; animation is TTY-gated and degrades to a
plain roll-call when piped. Run standalone for a demo:  `python bootroll.py`.
"""
import re
import sys
import time

# --- style.md tokens as 24-bit ANSI ---------------------------------------
def _rgb(h):
    return tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))

PHOSPHOR, PHOSPHOR_DIM = _rgb("58F07A"), _rgb("2E7A45")
AMBER, AMBER_DIM = _rgb("F2A93B"), _rgb("7A5620")
ALERT, ALERT_DIM = _rgb("FF4438"), _rgb("8A2820")
BONE, BONE_DIM, GRID = _rgb("C9CDBC"), _rgb("6B6F62"), _rgb("1C4028")

def c(rgb, s):
    return "\x1b[38;2;%d;%d;%dm%s\x1b[0m" % (rgb[0], rgb[1], rgb[2], s)

# --- module roster: (tag, name, addresses whose presence == module present) --
# Addresses mirror the firmware scanI2C() list + CONTRACT/REGISTRY.
ROSTER = [
    ("REAR", "VL53L5CX FIELD", (0x70, 0x29)),   # Zanshin: behind the mux, or direct
    ("IMU ", "LSM6DSOX",       (0x6a,)),
    ("MAG ", "LIS3MDL",        (0x1c, 0x1e)),
    ("GPS ", "PA1010D",        (0x10,)),
    ("THRM", "MLX90640",       (0x33,)),
    ("AIR ", "SCD-40",         (0x62,)),
    ("NOSE", "BME688",         (0x77,)),
    ("EYE ", "APDS9960",       (0x39,)),
    ("LANT", "ANDON MATRIX",   (0x3f,)),
    ("LGT ", "AS3935 ENRAI",   (0x03,)),
]

WIDTH = 52


def probe_addresses(ser, timeout=2.5):
    """Send `'I'` and collect the I2C addresses the board reports present."""
    ser.write(b"I\n")
    found, end = set(), time.time() + timeout
    while time.time() < end:
        ln = ser.readline().decode("utf-8", "replace").strip()
        m = re.match(r"0x([0-9a-fA-F]{2})$", ln)
        if m:
            found.add(int(m.group(1), 16))
        if "device(s)" in ln:
            break
    return found


def states_from_addresses(found):
    """Roster → [(tag, name, status)] with status OK / OFFLINE from a live scan."""
    return [(tag, name, "OK" if any(a in found for a in addrs) else "OFFLINE")
            for tag, name, addrs in ROSTER]


def _row(left, right, right_rgb):
    """A dotted-leader row (§3): BONE label · GRID dots · colored right value, to WIDTH."""
    dots = "·" * max(3, WIDTH - len(left) - len(right) - 2)
    return "  " + c(BONE, left) + " " + c(GRID, dots) + " " + c(right_rgb, right)


BANDS = {"far": PHOSPHOR, "mid": AMBER, "near": ALERT, "nominal": PHOSPHOR}
BANDS_DIM = {"far": PHOSPHOR_DIM, "mid": AMBER_DIM, "near": ALERT_DIM, "nominal": PHOSPHOR_DIM}

def seg_bar(frac, band="nominal", width=14):
    """Pulse-rifle segmented bar (§5.4): `width` discrete blocks, `frac` (0..1) filled in the
    band color (far/mid/near → phosphor/amber/alert); spent segments drop to that band's DIM."""
    frac = 0.0 if frac is None else max(0.0, min(1.0, frac))
    on = int(round(frac * width))
    return c(BANDS.get(band, PHOSPHOR), "█" * on) + c(BANDS_DIM.get(band, PHOSPHOR_DIM), "█" * (width - on))


def meter(label, frac, value, band="nominal", lwidth=8):
    """A labeled meter line: BONE label · segmented bar · colored value."""
    return "  " + c(BONE, label.ljust(lwidth)) + " " + seg_bar(frac, band) \
        + "  " + c(BANDS.get(band, PHOSPHOR), value)


def banner(text, band="near"):
    """Full-width alert strip (§5.7): a stencil mark + imperative, in the band color."""
    return "  " + c(BANDS.get(band, ALERT), ("◆ " + text).ljust(WIDTH))


def _rollcall_line(tag, name, status):
    scol = {"OK": PHOSPHOR, "OFFLINE": AMBER, "FAULT": ALERT}[status]
    stext = {"OK": "OK", "OFFLINE": "— OFFLINE", "FAULT": "FAULT"}[status]
    return _row("> %s %s" % (tag, name), stext, scol)


def play(states, out=sys.stdout, animate=None):
    """Render the ritual for [states] (a list of (tag, name, status))."""
    tty = out.isatty()
    animate = tty if animate is None else (animate and tty)
    step = (lambda s=0.055: time.sleep(s)) if animate else (lambda s=0: None)

    if tty:
        out.write("\x1b[2J\x1b[H")

    # 1 — scanline sweep down the frame
    if animate:
        for row in range(1, 7):
            out.write("\x1b[%d;3H%s" % (row, c(GRID, "▁" * WIDTH)))
            out.flush(); time.sleep(0.025)
            out.write("\x1b[%d;3H%s" % (row, " " * WIDTH))
        out.write("\x1b[H")

    # 2 — the mark, framed in reticle corner ticks (§4)
    pad = "  "
    out.write("\n%s%s\n" % (pad, c(GRID, "┌╴" + " " * (WIDTH - 4) + "╶┐")))
    out.write("%s%s\n" % (pad, _centered(c(PHOSPHOR, "S H I N T A I — O S"), WIDTH, 19)))
    out.write("%s%s\n" % (pad, _centered(c(BONE_DIM, "v0.1  ·  INITIALIZING"), WIDTH, 21)))
    out.write("%s%s\n\n" % (pad, c(GRID, "└╴" + " " * (WIDTH - 4) + "╶┘")))
    out.flush(); step(0.12)

    # 3 — the roll-call, revealed line by line
    out.write(_row("> BUS  I2C 0x03‥0x77", "SCAN", PHOSPHOR_DIM) + "\n"); out.flush(); step()
    for tag, name, status in states:
        out.write(_rollcall_line(tag, name, status) + "\n"); out.flush(); step()
    out.write(_row("> HUD  RAYNEO GATT", "LINK", PHOSPHOR_DIM) + "\n"); out.flush(); step(0.12)

    # 4 — online
    off = sum(1 for _, _, s in states if s != "OK")
    tail = c(PHOSPHOR, "SHINTAI-OS  //  ONLINE") if not off \
        else c(PHOSPHOR, "SHINTAI-OS  //  ONLINE") + c(AMBER, "  (%d OFFLINE)" % off)
    out.write("\n  " + tail + "\n\n"); out.flush()


def _centered(colored, width, visible_len):
    """Center text of known VISIBLE length inside width (ANSI codes are zero-width)."""
    left = max(0, (width - visible_len) // 2)
    return " " * left + colored


if __name__ == "__main__":
    # Standalone demo: a representative roster (NOSE offline) so the visuals are visible
    # without a board. In the logger, states come from a live probe_addresses() scan.
    demo = [(t, n, "OFFLINE" if t == "NOSE" else "OK") for t, n, _ in ROSTER]
    play(demo)
