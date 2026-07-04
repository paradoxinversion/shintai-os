"""
Kiatsu (気圧) barometric floor detection — base-side altitude reconstruction.

The firmware only keeps the slow 3-hour WEATHER trend on-device (KiD-4, KiatsuBand.h);
the FAST half — storey-scale floor steps — is slower than the 1.5 s log, so it survives
untouched in `pressure_hpa` and the reconstruction lives HERE, testable with no board.
This is Hokan inverted: Hokan needed a new `steps` column because gait outran the log;
Kiatsu adds NO column because its signals fit inside it (the spec's central finding).

Given the per-row `pressure_hpa` (and `air_temp_c` for the temperature correction), we:
  * convert pressure to RELATIVE altitude (metres above the walk's start), and
  * detect floor transitions — a sustained ~0.4 hPa step against a short-window
    reference — into a per-row relative floor index.

Both are DIFFERENCES (KiD-3), so neither needs a calibrated reference: Kiatsu reports
relative moves ("up two, down one"), not "you are on floor 3". Composed onto Hokan's
(x, y) dead-reckoned track by analyze.py, this tags the 2-D PDR path with z — turning a
GPS-denied walk into a 3-D one (which floors, in what order). Pure functions (sequences
in, sequences out) — analyze.py wires them to the frame. Degrades cleanly: no pressure
(old logs / no BME688) -> callers skip.
"""
import math

# KiD-6 thresholds (tune on-site). A storey is ~3-4 m ≈ ~0.4 hPa near sea level; the
# floor step must clear FLOOR_HYST_HPA against a short reference to count as a transition
# (weather moves far slower, so it never masquerades as a floor).
FLOOR_HYST_HPA = 0.3

# Barometric constant: metres per hPa = (R/(M·g))·T_K / P_hpa, temperature-corrected
# with the BME688's own air_temp_c (KiD-2) rather than a fixed 15 °C. R/(M·g) ≈ 29.27,
# so at 288 K / 1013 hPa this is ~8.3 m/hPa — the spec's sea-level figure.
_R_OVER_MG = 29.27
_DEFAULT_T_C = 15.0


def _is_num(v):
    return v is not None and not (isinstance(v, float) and math.isnan(v))


def m_per_hpa(temp_c, pressure_hpa):
    """Metres of altitude per hPa of pressure at the given air temperature and
    pressure — the local linearised barometric gradient, temperature-corrected (KiD-2).
    Falls back to 15 °C / 1013 hPa when a term is missing."""
    t = temp_c if _is_num(temp_c) else _DEFAULT_T_C
    p = pressure_hpa if (_is_num(pressure_hpa) and pressure_hpa > 0) else 1013.25
    return _R_OVER_MG * (t + 273.15) / p


def altitude_profile(pressures, temps=None):
    """Relative altitude (metres above the first valid sample) per row, from
    `pressures` (hPa) and optional per-row `temps` (°C, for KiD-2). Higher altitude =
    lower pressure, so a pressure drop reads as a climb. Rows with no pressure repeat
    the last known altitude (the track holds, it doesn't jump). Returns a list as long
    as `pressures`, first valid point at 0.0 m."""
    if temps is None:
        temps = [None] * len(pressures)
    out = []
    p_ref = None
    last_alt = 0.0
    for p, t in zip(pressures, temps):
        if _is_num(p) and p > 0:
            if p_ref is None:
                p_ref = p
            last_alt = m_per_hpa(t, p) * (p_ref - p)   # p < p_ref -> positive -> climbed
        out.append(last_alt)
    return out


def floor_steps(pressures, hyst_hpa=FLOOR_HYST_HPA):
    """Per-row RELATIVE floor index (starts at 0) from `pressures` (hPa). Tracks a
    reference pressure for the current floor; when pressure falls hyst below it (climbed)
    the index increments and the reference re-anchors, when it rises hyst above (descended)
    it decrements. A level walk — pressure wandering inside the ±hyst dead zone — adds no
    transition. Rows with no pressure repeat the last index. Returns a list as long as
    `pressures`."""
    out = []
    ref = None
    floor = 0
    for p in pressures:
        if _is_num(p) and p > 0:
            if ref is None:
                ref = p
            if ref - p >= hyst_hpa:         # pressure dropped a storey -> went up
                floor += 1
                ref = p                     # re-anchor to the new resting level
            elif p - ref >= hyst_hpa:       # pressure rose a storey -> went down
                floor -= 1
                ref = p
        out.append(floor)
    return out


def net_tendency(pressures):
    """Net pressure change across the series: last valid − first valid (hPa). Negative
    = the barometer fell over the walk (weather turning). None if <2 valid samples."""
    vals = [p for p in pressures if _is_num(p) and p > 0]
    if len(vals) < 2:
        return None
    return vals[-1] - vals[0]


def summary(pressures, temps=None):
    """A one-line human report over a walk's pressure series — floors crossed (net and
    the up/down tally), metres climbed, and the net barometric tendency. Returns None
    when there's no usable pressure. Mirrors the spec's report line."""
    vals = [p for p in pressures if _is_num(p) and p > 0]
    if len(vals) < 2:
        return None
    floors = floor_steps(pressures)
    transitions = [b - a for a, b in zip(floors, floors[1:]) if b != a]
    ups = sum(d for d in transitions if d > 0)
    downs = -sum(d for d in transitions if d < 0)
    alt = altitude_profile(pressures, temps)
    climb = max(alt) - min(alt) if alt else 0.0
    net = net_tendency(pressures)
    wx = "weather turning" if (net is not None and net <= -1.0) else "steady"
    return (f"crossed {ups + downs} floors (+{ups}, -{downs}), {climb:.0f} m of relief; "
            f"barometer {'fell' if (net or 0) < 0 else 'rose'} {abs(net or 0):.1f} hPa "
            f"over the walk ({wx})")
