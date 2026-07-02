#!/bin/zsh
# Shintai-OS HUD — default view of the newest excursion.
#   shintailog              newest log (default)
#   shintailog -l           list selectable logs
#   shintailog <index>      pick by the index shown by -l
#   shintailog <substring>  newest log whose filename contains <substring>
# Uses `python` (not python3) so the pyenv shim doesn't shadow the conda env.
source ~/miniconda3/etc/profile.d/conda.sh
conda activate shintai
exec python ~/shintai-os/groundstation/hud.py "$@"
