#!/bin/zsh
# Shintai-OS logger launcher — one command, anywhere.
#   ./start.sh   (or, after sourcing ~/.zshrc:  shintai)
# Uses `python` (not python3) so the pyenv shim doesn't shadow the conda env.
source ~/miniconda3/etc/profile.d/conda.sh
conda activate shintai
exec python ~/shintai-os/groundstation/shintai-logger.py
