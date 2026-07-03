#!/bin/zsh
# Compile-check the Shintai-OS firmware WITHOUT a board attached.
#
# Uses the build profile pinned in firmware/shintai-os/sketch.yaml, so it needs
# no FQBN/library flags and no hardware — pure syntax + type + link check. Run it
# after every .ino edit to catch breakage before you ever flash.
#
#   firmware/verify.sh            # compile-check
#   firmware/verify.sh --upload   # compile AND flash (needs the board on $PORT)
#
# Exits non-zero on any compile error, so it drops straight into a pre-commit hook
# or CI step.
set -euo pipefail

SKETCH_DIR="${0:A:h}/shintai-os"   # firmware/shintai-os (folder == sketch name)
PORT="${SHINTAI_PORT:-/dev/cu.usbmodem101}"

if [[ "${1:-}" == "--upload" ]]; then
  exec arduino-cli compile --profile qtpy --upload -p "$PORT" "$SKETCH_DIR"
else
  # Default warnings only: --warnings all drowns the sketch in hundreds of
  # <command-line> macro-whitespace warnings from the ESP32 core's own build flags.
  exec arduino-cli compile --profile qtpy "$SKETCH_DIR"
fi
