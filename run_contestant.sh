#!/usr/bin/env bash
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if (( $# == 0 )); then
  if [[ -z "${RMTC_SEAT:-}" || -z "${RMTC_TEAM_NAME:-}" || -z "${RMTC_REFEREE_IP:-}" ]]; then
    echo 'Set RMTC_SEAT, RMTC_TEAM_NAME, RMTC_REFEREE_IP, or pass three launch arguments' >&2; exit 2
  fi
  set -- "$RMTC_SEAT" "$RMTC_TEAM_NAME" "$RMTC_REFEREE_IP"
fi
if (( $# != 3 )); then echo 'Usage: run_contestant.sh A|B|C|D team_name referee_ip (or environment-only launch)' >&2; exit 2; fi
case "$1" in A|B|C|D) ;; *) echo 'Invalid seat' >&2; exit 2;; esac
source /opt/ros/humble/setup.bash
source "$root/contestant_ws/install/setup.bash"
export RMTC_LOG_FILE="${RMTC_LOG_FILE:-$root/logs/bot-$(date +%Y%m%d-%H%M%S)-$$.jsonl}"
mkdir -p -- "$(dirname -- "$RMTC_LOG_FILE")"
# ROS discovery and the gateway endpoint follow the announced connection metadata.
# Do not infer an unannounced gateway port from referee_ip. Credentials stay outside source control.
export RMTC_DECISION_CONFIG="${RMTC_DECISION_CONFIG:-$root/decision_config.json}"
# launch_bot.py selects the same stage-specific transport as local preflight.
# Reliable QoS, bounded history, and action protocol remain unchanged.
exec python3 "$root/tools/launch_bot.py" "$1" "$2" "$3"
