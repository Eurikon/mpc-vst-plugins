#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="$(cd "$here" && cd "${MPC_VST:-../toolchain/upstream}" && pwd)"
make -C "$here" prepare-tools MPC_VST="$MPC_VST"
python3 "$here/skin.py"
exec bash "$MPC_VST/tools/build_port.sh" "$here/vst.json"
