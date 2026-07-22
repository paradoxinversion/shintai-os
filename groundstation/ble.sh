#!/bin/zsh
# Shintai-OS BLE central launcher — untethered wireless capture (no USB).
#   ./ble.sh                       capture until Ctrl+C, reconnecting on drop
#   ./ble.sh --name ShintaiOS-fwd  pick a pod
# Uses `python` (not python3) so the pyenv shim doesn't shadow the conda env.
source ~/miniconda3/etc/profile.d/conda.sh
conda activate shintai
exec python ~/shintai-os/groundstation/shintai-ble.py "$@"
