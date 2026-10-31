#!/usr/bin/env bash
set -euo pipefail
mode="${1:---help}"
case "$mode" in --check|--launch) ;; *) echo 'Usage: bash start-final.sh --check|--launch /absolute/stage.private.json [/absolute/logs]'; exit 2;; esac
settings="$(realpath -- "${2:?private settings file required}")"
image='rmtc-bot:v0.25.1-pk'
expected='1bf25f43ac4992484c5e'
actual="$(docker image inspect --format '{{ index .Config.Labels "org.rmtc.source.iteration" }}' "$image")"
[[ "$actual" == "$expected" ]] || { echo 'Image fingerprint mismatch' >&2; exit 3; }
binary="$(docker run --rm --network none --entrypoint sha256sum "$image" /opt/rmtc/contestant_ws/install/rmtc_bot/lib/rmtc_bot/turing_bot_node)"
[[ "${binary%% *}" == 'c0411bef60dfab423ca77a0aeae9f16983542190ec1abcc21c395c8c3ded4089' ]] || { echo 'Binary hash mismatch' >&2; exit 3; }
# Clear the inherited stage-2 image default; frozen match_setup selects per stage.
common=(--rm --network host --ipc host --env RMW_FASTRTPS_PUBLICATION_MODE= --mount "type=bind,src=$settings,dst=/run/rmtc/match-settings.private.json,readonly")
docker run "${common[@]}" "$image" --check
[[ "$mode" == --launch ]] || exit 0
logs="$(realpath -- "${3:?existing writable log directory required}")"
[[ -d "$logs" && -w "$logs" && "$(stat -c %u "$logs")" == 1000 ]] || { echo 'Log directory must be writable and owned by UID 1000; set its ownership before launch.' >&2; exit 2; }
exec docker run "${common[@]}" --mount "type=bind,src=$logs,dst=/data" "$image"
