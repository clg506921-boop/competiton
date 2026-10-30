#!/usr/bin/env bash
set -eo pipefail
source /opt/ros/humble/setup.bash
source /opt/rmtc/contestant_ws/install/setup.bash
if [[ "${1:-}" == "--check" ]]; then
  shift
  exec python3 -B /opt/rmtc/tools/match_setup.py --check "${1:-/run/rmtc/match-settings.private.json}"
fi
exec python3 -B /opt/rmtc/tools/match_setup.py --launch "${1:-/run/rmtc/match-settings.private.json}"
