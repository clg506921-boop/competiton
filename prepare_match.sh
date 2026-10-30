#!/usr/bin/env bash
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
source /opt/ros/humble/setup.bash
source "$root/contestant_ws/install/setup.bash"
exec python3 -B "$root/tools/match_setup.py" "$@"
