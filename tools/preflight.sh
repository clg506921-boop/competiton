#!/usr/bin/env bash
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
source /opt/ros/humble/setup.bash
source "$root/contestant_ws/install/setup.bash"
export RMTC_DECISION_CONFIG="${RMTC_DECISION_CONFIG:-$root/decision_config.json}"
exec python3 -B "$root/tools/preflight.py" "$@"
