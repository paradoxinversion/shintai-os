#!/usr/bin/env python3
"""Contract-drift linter for Shintai-OS.

CONTRACT.md is the declared source of truth for the firmware<->consumer seam. Three
sites mirror it and drift silently:

  1. firmware CSV_HEADER   (firmware/shintai-os/shintai-os.ino)
  2. Kotlin GATT UUIDs     (android/.../ShintaiGatt.kt)
  3. hardcoded column names in groundstation/*.py

This script parses CONTRACT.md and asserts all three agree. It has NO third-party
deps (stdlib only) so it runs anywhere, and exits non-zero on any mismatch — drop it
in a pre-commit hook or CI step.

    python3 tools/check-contract.py

The failure modes it is built to catch are the ones CLAUDE.md calls out by name:
a renamed/reordered CSV column, a mistyped GATT UUID, and the CCCD `8000`-vs-`0000`
typo that silently kills every BLE subscription.
"""
from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CONTRACT = ROOT / "CONTRACT.md"
INO = ROOT / "firmware" / "shintai-os" / "shintai-os.ino"
GATT = ROOT / "android" / "core" / "src" / "main" / "java" / "com" / "saboteur" / "shintai" / "core" / "ShintaiGatt.kt"
GROUNDSTATION = ROOT / "groundstation"

# Characteristic label in CONTRACT.md  ->  Kotlin `val` name in ShintaiGatt.kt.
CHAR_TO_KOTLIN = {
    "Distance": "DISTANCE",
    "Alert": "ALERT",
    "Heading": "HEADING",
    "Accelerometer": "ACCEL",
    "GPS": "GPS",
    "Thermal": "THERMAL",
    "Climate": "CLIMATE",
    "Environment": "ENVIRONMENT",
    "Thermal Grid": "THERMAL_GRID",
}
# Characteristics the Android app is documented NOT to expose (CONTRACT.md "Consumer
# coverage"). Absent from Kotlin => acknowledged gap, not an error.
KOTLIN_OPTIONAL = {"ENVIRONMENT"}

BASE_CCCD = "00002902-0000-1000-8000-00805f9b34fb"

errors: list[str] = []
notes: list[str] = []


def fail(msg: str) -> None:
    errors.append(msg)


def read(path: Path) -> str:
    if not path.exists():
        fail(f"missing file: {path.relative_to(ROOT)}")
        return ""
    return path.read_text()


# ---------------------------------------------------------------------------
# CONTRACT.md parsing
# ---------------------------------------------------------------------------
def parse_contract(text: str):
    # CSV columns: every backticked identifier in the first cell of each row of the
    # schema table, in document (== header) order, de-duped preserving first-seen.
    cols: list[str] = []
    seen = set()
    in_schema = False
    for line in text.splitlines():
        if line.startswith("| Column"):
            in_schema = True
            continue
        if in_schema:
            if not line.startswith("|") or set(line.replace("|", "").strip()) <= {"-"}:
                if cols:  # table ended
                    in_schema = False
                continue
            first_cell = line.split("|")[1]
            for name in re.findall(r"`([a-z0-9_]+)`", first_cell):
                if name not in seen:
                    seen.add(name)
                    cols.append(name)

    # BLE UUIDs: characteristic label -> uuid from the GATT table.
    ble: dict[str, str] = {}
    for label, uuid in re.findall(r"\|\s*(\w[\w ]*?)\s*\|\s*`([0-9a-fA-F-]{36})`", text):
        ble[label.strip()] = uuid.lower()

    m = re.search(r"Service\s+`([0-9a-fA-F-]{36})`", text)
    service = m.group(1).lower() if m else None
    return cols, ble, service


# ---------------------------------------------------------------------------
# 1. CSV header
# ---------------------------------------------------------------------------
def check_csv(contract_cols: list[str]):
    text = read(INO)
    m = re.search(r"CSV_HEADER\s*=\s*((?:\s*\"[^\"]*\"\s*)+);", text)
    if not m:
        fail("could not find CSV_HEADER string literal in shintai-os.ino")
        return set()
    joined = "".join(re.findall(r'"([^"]*)"', m.group(1)))
    firmware_cols = [c for c in joined.split(",") if c]

    if firmware_cols != contract_cols:
        # Report the precise divergence, not just "differ".
        cset, fset = set(contract_cols), set(firmware_cols)
        only_contract = [c for c in contract_cols if c not in fset]
        only_firmware = [c for c in firmware_cols if c not in cset]
        if only_contract:
            fail(f"CSV columns in CONTRACT.md but NOT in firmware CSV_HEADER: {only_contract}")
        if only_firmware:
            fail(f"CSV columns in firmware CSV_HEADER but NOT in CONTRACT.md: {only_firmware}")
        if not only_contract and not only_firmware:
            fail("CSV column ORDER differs between CONTRACT.md and firmware CSV_HEADER:\n"
                 f"    contract: {contract_cols}\n    firmware: {firmware_cols}")
    else:
        notes.append(f"CSV header: {len(firmware_cols)} columns match CONTRACT.md (order-exact)")
    return set(firmware_cols)


# ---------------------------------------------------------------------------
# 2. Kotlin GATT UUIDs
# ---------------------------------------------------------------------------
def check_gatt(contract_ble: dict[str, str], service: str | None):
    text = read(GATT)
    kt = {name: uuid.lower()
          for name, uuid in re.findall(r'val\s+(\w+)\s*:\s*UUID\s*=\s*UUID\.fromString\("([^"]+)"\)', text)}

    # Service
    if service and kt.get("SERVICE") != service:
        fail(f"SERVICE UUID mismatch: contract {service} vs Kotlin {kt.get('SERVICE')}")

    # CCCD — the load-bearing 8000-vs-0000 check.
    if kt.get("CCCD") != BASE_CCCD:
        fail(f"CCCD UUID wrong: expected {BASE_CCCD} (note the 8000), got {kt.get('CCCD')} — "
             "this typo silently kills all BLE notifications")

    # Each characteristic
    for label, kname in CHAR_TO_KOTLIN.items():
        want = contract_ble.get(label)
        if want is None:
            fail(f"characteristic '{label}' missing from CONTRACT.md GATT table")
            continue
        got = kt.get(kname)
        if got is None:
            if kname in KOTLIN_OPTIONAL:
                notes.append(f"GATT: {kname} absent from Kotlin (acknowledged consumer gap — ok)")
            else:
                fail(f"characteristic '{label}' (val {kname}) missing from ShintaiGatt.kt")
        elif got != want:
            fail(f"UUID mismatch for {label}: contract {want} vs Kotlin {kname} {got}")

    matched = sum(1 for l, k in CHAR_TO_KOTLIN.items()
                  if kt.get(k) and kt.get(k) == contract_ble.get(l))
    notes.append(f"GATT: {matched} characteristic UUIDs match CONTRACT.md (+ service + CCCD)")


# ---------------------------------------------------------------------------
# 3. groundstation hardcoded column names
# ---------------------------------------------------------------------------
def check_groundstation(valid_cols: set[str]):
    if not valid_cols:
        return
    phantom: dict[str, list[str]] = {}
    for py in sorted(GROUNDSTATION.glob("*.py")):
        src = py.read_text()
        # Columns the script itself DERIVES (e.g. `df["t_min"] = ...`) are legitimate
        # names that won't be in the contract — treat them as valid for this file.
        derived = set(re.findall(r"""(?:df\s*\[\s*|\.)['"]?([a-z][a-z0-9_]*)['"]?\s*\]?\s*=(?!=)""", src))
        local_valid = valid_cols | derived
        # Only ones that look like a data column (unit/axis suffix or a known bare
        # name) so we don't flag unrelated strings — a phantom here is a real typo or
        # a stale reference to a renamed column.
        for lit in re.findall(r"""['"]([a-z][a-z0-9_]*)['"]""", src):
            if lit in local_valid:
                continue
            if lit in {"lat", "lon", "alert", "cardinal", "sats"} or re.search(
                    r"_(mm|deg|ms|kmh|ppm|pct|hpa|ohms|fix|delta|mean|min|max|ctr|x|y|z|m|c)$", lit):
                phantom.setdefault(py.name, []).append(lit)
    for fname, cols in phantom.items():
        uniq = sorted(set(cols))
        fail(f"{fname}: references column name(s) not in the CSV contract: {uniq}")
    if not phantom:
        notes.append("groundstation: all hardcoded column names exist in the CSV contract")


# ---------------------------------------------------------------------------
def main() -> int:
    contract_text = read(CONTRACT)
    if not contract_text:
        print("FATAL: CONTRACT.md unreadable", file=sys.stderr)
        return 2
    cols, ble, service = parse_contract(contract_text)
    valid = check_csv(cols)
    check_gatt(ble, service)
    check_groundstation(valid)

    for n in notes:
        print(f"  ok  {n}")
    if errors:
        print()
        for e in errors:
            print(f" FAIL {e}", file=sys.stderr)
        print(f"\n{len(errors)} contract-drift error(s).", file=sys.stderr)
        return 1
    print("\nContract is consistent across firmware, Kotlin, and groundstation.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
