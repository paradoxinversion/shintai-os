#!/bin/zsh
# Shintai-OS HUD — default view of the newest excursion.
#   shintailog   (after sourcing ~/.zshrc)
# Uses `python` (not python3) so the pyenv shim doesn't shadow the conda env.
source ~/miniconda3/etc/profile.d/conda.sh
conda activate shintai
exec python ~/shintai-os/groundstation/hud.py
