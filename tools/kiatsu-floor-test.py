#!/usr/bin/env python3
"""Host unit tests for Kiatsu's base-side floor detection (groundstation/kiatsu.py).

The floor/altitude math lives base-side (KiD-4), so it's testable with plain Python —
no board, no pandas. Replay synthetic `pressure_hpa` series and assert the reconstructed
floor index / altitude profile. Mirrors the C++ *-test.cpp and hokan-pdr-test.py posture.

    python3 tools/kiatsu-floor-test.py
"""
import math
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "groundstation"))
import kiatsu  # noqa: E402


def approx(a, b, tol=1e-3):
    return abs(a - b) <= tol


def test_m_per_hpa_sea_level():
    # ~8.3 m/hPa at the spec's 15 °C / 1013 hPa reference (KiD-2/KiD-6).
    assert approx(kiatsu.m_per_hpa(15.0, 1013.0), 8.32, tol=0.05), kiatsu.m_per_hpa(15.0, 1013.0)
    # Warmer air is less dense -> more metres per hPa (temperature correction bites).
    assert kiatsu.m_per_hpa(35.0, 1013.0) > kiatsu.m_per_hpa(5.0, 1013.0)


def test_climb_three_floors():
    # A storey ≈ 0.4 hPa; walk up 3 floors as sustained 0.4 hPa steps (a few rows each).
    p = 1013.0
    pressures = [p]
    for _ in range(3):
        p -= 0.4
        pressures += [p, p, p]          # dwell on each floor a few rows
    floors = kiatsu.floor_steps(pressures)
    assert floors[-1] == 3, floors
    # Monotonic non-decreasing on a pure climb.
    assert all(b >= a for a, b in zip(floors, floors[1:])), floors


def test_level_walk_no_transition():
    # Pressure wandering inside the ±hyst dead zone (noise, not a storey) adds nothing.
    pressures = [1013.0, 1013.1, 1012.95, 1013.05, 1012.9, 1013.0]
    assert kiatsu.floor_steps(pressures) == [0] * len(pressures)


def test_up_then_down_returns():
    # Up two floors (−0.8 hPa) then back down two (+0.8) nets to floor 0.
    pressures = [1013.0, 1012.6, 1012.2, 1012.6, 1013.0]
    floors = kiatsu.floor_steps(pressures)
    assert floors[-1] == 0, floors
    assert max(floors) == 2, floors


def test_altitude_profile_and_climb():
    # A pressure drop reads as a climb (positive metres); rising back returns toward 0.
    pressures = [1013.0, 1012.0, 1011.0, 1012.0]
    alt = kiatsu.altitude_profile(pressures)
    assert approx(alt[0], 0.0), alt
    assert alt[2] > alt[1] > 0, alt          # deepest drop = highest point
    assert alt[3] < alt[2], alt              # came back down


def test_net_tendency():
    assert kiatsu.net_tendency([1015.0, 1013.0, 1010.8]) < -4.0
    assert kiatsu.net_tendency([1010.0, 1012.0]) > 0
    assert kiatsu.net_tendency([1013.0]) is None      # <2 valid


def test_degrades_on_missing():
    nan = float("nan")
    assert kiatsu.floor_steps([nan, None, nan]) == [0, 0, 0]     # holds at 0, no crash
    assert kiatsu.altitude_profile([nan, None]) == [0.0, 0.0]
    assert kiatsu.summary([nan, None]) is None
    # A blank row mid-climb repeats the last floor, doesn't reset.
    floors = kiatsu.floor_steps([1013.0, 1012.6, nan, 1012.2])
    assert floors == [0, 1, 1, 2], floors


def test_summary_line():
    p = 1013.0
    pressures = [p]
    for _ in range(4):
        p -= 0.4
        pressures += [p, p]
    line = kiatsu.summary(pressures)
    assert "floors" in line and "hPa" in line, line


if __name__ == "__main__":
    for name, fn in sorted(globals().items()):
        if name.startswith("test_") and callable(fn):
            fn()
    print("kiatsu-floor-test: all assertions passed")
