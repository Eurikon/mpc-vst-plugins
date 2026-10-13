#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
MPC_VST="$(cd "$here" && cd "${MPC_VST:-../../toolchain/upstream}" && pwd)"
make -C "$here" test MPC_VST="$MPC_VST"
if [ "$(uname -s):$(uname -m)" = Linux:x86_64 ]; then
    exec bash "$MPC_VST/tools/test_port.sh" "$here/vst.json"
fi
exec docker run --rm --platform linux/amd64 \
    -v "$here:/port" -v "$MPC_VST:/mv:ro" -w /port -e MPC_VST=/mv gcc:12 \
    bash -euc 'make test CXX=g++; bash /mv/tools/test_port.sh vst.json'
