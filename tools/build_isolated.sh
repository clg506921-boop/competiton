#!/usr/bin/env bash
# New build/install/log trees only. No tests, system edits or dependency downloads.
set -eo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
if (( $# != 1 )); then echo 'Usage: bash tools/build_isolated.sh new-directory-under-project' >&2; exit 2; fi
target="$(realpath -m -- "$1")"
case "$target/" in "$root/"*) ;; *) echo 'Build directory must stay inside this project' >&2; exit 2;; esac
if [[ -e "$target" || -L "$target" ]]; then echo 'Refusing to overwrite an existing build directory' >&2; exit 2; fi
mkdir -p -- "$target"
source /opt/ros/humble/setup.bash
export CMAKE_BUILD_PARALLEL_LEVEL=2
export MAKEFLAGS=-j2
cd "$target"
colcon --log-base "$target/log" build --base-paths "$root/contestant_ws/src/rmtc_bot" \
  --build-base "$target/build" --install-base "$target/install" --symlink-install \
  --parallel-workers 1 --cmake-args -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
source "$target/install/setup.bash"
ros2 pkg prefix rmtc_bot
python3 -B "$root/tools/inspect_build.py" "$target"
