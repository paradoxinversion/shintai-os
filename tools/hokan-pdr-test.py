#!/usr/bin/env python3
"""Host unit tests for Hokan's base-side dead-reckoning (groundstation/hokan.py).

The PDR math lives base-side (HkD-4), so it's testable with plain Python — no board,
no pandas. Replay a synthetic `steps`/`heading` walk and assert the reconstructed
(x, y) track. Mirrors the C++ *-test.cpp posture.

    python3 tools/hokan-pdr-test.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "groundstation"))
import hokan  # noqa: E402


def approx(a, b, tol=1e-6):
    return abs(a - b) <= tol


def test_straight_north():
    # 10 steps due north (heading 0): the track advances +y only, by 10*STEP_LEN.
    steps = list(range(0, 11))            # cumulative 0..10, Δ=1 each row
    headings = [0.0] * 11
    xs, ys = hokan.integrate(steps, headings, step_len=0.7)
    assert approx(xs[-1], 0.0), xs[-1]
    assert approx(ys[-1], 10 * 0.7), ys[-1]


def test_east_quarter_turn():
    # Heading 90 (east) advances +x only.
    steps = [0, 5]
    headings = [90.0, 90.0]
    xs, ys = hokan.integrate(steps, headings, step_len=0.7)
    assert approx(xs[-1], 5 * 0.7), xs[-1]
    assert approx(ys[-1], 0.0, tol=1e-6), ys[-1]


def test_closed_square_loop():
    # A square: N, E, S, W legs of equal step counts return ~to the origin (AC-3:
    # a walked loop reconstructs as a closed-ish track). 10 steps per leg.
    steps, headings = [0], [0.0]
    cum = 0
    for hdg in (0.0, 90.0, 180.0, 270.0):     # N, E, S, W
        for _ in range(10):
            cum += 1
            steps.append(cum)
            headings.append(hdg)
    xs, ys = hokan.integrate(steps, headings, step_len=0.7)
    assert approx(xs[-1], 0.0, tol=1e-9), xs[-1]
    assert approx(ys[-1], 0.0, tol=1e-9), ys[-1]


def test_delta_never_negative():
    # A counter reset (reboot mid-log) must not drag the track backwards.
    steps = [100, 105, 0, 3]              # reset from 105 -> 0
    headings = [0.0, 0.0, 0.0, 0.0]
    xs, ys = hokan.integrate(steps, headings, step_len=0.7)
    # Only the +5 and +3 positive deltas count: 8 steps north.
    assert approx(ys[-1], 8 * 0.7), ys[-1]


def test_total_steps_across_reset():
    assert hokan.total_steps([0, 10, 25]) == 25
    assert hokan.total_steps([100, 105, 0, 3]) == 8       # 5 + 3, reset ignored
    assert hokan.total_steps([]) == 0
    assert hokan.total_steps([float("nan"), 4, 9]) == 5   # NaNs skipped


def test_to_latlon_anchor():
    # A pure-north track anchored at a fix moves latitude up, longitude unchanged.
    xs, ys = [0.0, 0.0], [0.0, 111.32]                    # 111.32 m north
    lats, lons = hokan.to_latlon(xs, ys, 37.0, -122.0)
    assert approx(lons[-1], -122.0, tol=1e-9), lons[-1]
    assert approx(lats[-1], 37.0 + 111.32 / hokan.EARTH_M_PER_DEG_LAT, tol=1e-9), lats[-1]


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    print("hokan-pdr-test: all assertions passed")
