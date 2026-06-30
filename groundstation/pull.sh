#!/bin/zsh
# Shintai-OS — pull onboard flash logs over USB into logs/.
#   shintaipull   (after sourcing ~/.zshrc)
# Uses `python` (not python3) so the pyenv shim doesn't shadow the conda env.
source ~/miniconda3/etc/profile.d/conda.sh
conda activate shintai
exec python ~/shintai-os/groundstation/shintai-pull.py
