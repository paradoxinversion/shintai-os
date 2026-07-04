"""
Hokan (歩勘) pedestrian dead-reckoning — base-side path integration.

The firmware logs a cumulative `steps` count per row (specs/zokyo/hokan.md); the
position math lives HERE (HkD-4), so it's testable with no board and reuses the
map the ground-station already draws. Given per-row cumulative `steps` + per-row
`heading_deg`, integrate Δsteps × STEP_LEN along each row's heading into an (x, y)
track in metres — a reconstructed walked path through GPS-denied stretches where
`lat`/`lon` are blank. Anchor it to a known lat/lon to overlay on route_map.html.

Pure functions (sequences in, sequences out) — analyze.py wires them to the frame.
Dead reckoning DRIFTS (heading is disturbed indoors; step length is estimated); this
gives "roughly where I walked," not a survey (HkD-3 / the spec's non-goals).
"""
import math

STEP_LEN_M = 0.7            # HkD-3 — constant step length; adaptive (Weinberg) is forward.
EARTH_M_PER_DEG_LAT = 111320.0


def _is_num(v):
    return v is not None and not (isinstance(v, float) and math.isnan(v))


def integrate(steps, headings, step_len=STEP_LEN_M):
    """Integrate cumulative `steps` + per-row `headings` (deg, 0 = North) into an
    (xs, ys) track in metres — East = +x, North = +y — starting at the origin (0, 0).

    Δsteps ≤ 0 (a counter reset or a bad row) contributes no motion; a missing
    heading reuses the last good one. Returns (xs, ys), each as long as `steps`, with
    the first point at the origin."""
    xs, ys = [], []
    x = y = 0.0
    last_steps = None
    last_hdg = 0.0
    for s, h in zip(steps, headings):
        if _is_num(h):
            last_hdg = h
        if last_steps is not None and _is_num(s):
            d_steps = s - last_steps
            if d_steps > 0:
                dist = d_steps * step_len
                rad = math.radians(last_hdg)
                x += dist * math.sin(rad)   # heading 0 = North -> +y, 90 = East -> +x
                y += dist * math.cos(rad)
        if _is_num(s):
            last_steps = s
        xs.append(x)
        ys.append(y)
    return xs, ys


def to_latlon(xs, ys, lat0, lon0):
    """Convert a metre track (East = +x, North = +y) to (lats, lons) anchored at
    (lat0, lon0) with an equirectangular approximation — fine over a walk's scale."""
    lats, lons = [], []
    m_per_deg_lon = EARTH_M_PER_DEG_LAT * math.cos(math.radians(lat0))
    for x, y in zip(xs, ys):
        lats.append(lat0 + y / EARTH_M_PER_DEG_LAT)
        lons.append(lon0 + (x / m_per_deg_lon if m_per_deg_lon else 0.0))
    return lats, lons


def total_steps(steps):
    """Steps walked across a possibly-resetting counter: the sum of positive Δsteps,
    so a mid-log reboot (counter -> 0) doesn't subtract or inflate the total."""
    total = 0
    last = None
    for s in steps:
        if not _is_num(s):
            continue
        if last is not None and s >= last:
            total += s - last
        last = s
    return int(total)
