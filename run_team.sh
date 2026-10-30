#!/usr/bin/env bash
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
export RMTC_TEAM_NAME='肥鱼说的都队'
if (( $# == 2 )); then
  exec bash "$root/run_contestant.sh" "$1" "$RMTC_TEAM_NAME" "$2"
fi
if (( $# != 0 )); then echo 'Usage: run_team.sh A|B|C|D referee_ip (or environment-only launch)' >&2; exit 2; fi
exec bash "$root/run_contestant.sh"
