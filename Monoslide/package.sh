#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"
MPC_VST="$(cd "${MPC_VST:-../toolchain/upstream}" && pwd)"
version="${1:?Usage: ./package.sh X.Y.Z [OUTPUT_DIR]}"
[[ $# -le 2 ]] || { echo "Usage: ./package.sh X.Y.Z [OUTPUT_DIR]" >&2; exit 1; }
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Expected X.Y.Z" >&2; exit 1; }
output="${2:-dist}"
make -C "$here" prepare-tools MPC_VST="$MPC_VST"
args=()
if [ -f bench.txt ]; then
    python3 - <<'PYBENCH'
import json, math
from pathlib import Path
b = json.loads(Path('bench.txt').read_text().strip().splitlines()[-1])
p99, worst = float(b['p99_pct']), float(b['max_pct'])
assert math.isfinite(p99) and math.isfinite(worst) and p99 >= 0 and worst >= p99
verdict = 'PASS' if p99 <= 15 and worst <= 50 else 'WARN' if p99 <= 35 and worst <= 80 else 'FAIL'
assert b['verdict'] == verdict and verdict != 'FAIL', 'Benchmark blocks release'
PYBENCH
    args+=(--bench bench.txt)
fi
python3 "$MPC_VST/tools/release.py" \
    --so build/monoslide.so --skin "build/skin/Twisted Cable - VST - MonoSlide" \
    --entry build/pluginlist-entry.xml --version "$version" \
    --repo Eurikon/mpc-vst-plugins --license GPL-3.0-or-later --id monoslide \
    --about "Monophonic acid bass instrument for MPC standalone." \
    --extra LICENSE:LICENSE --extra licenses:licenses "${args[@]}" -o "$output"
python3 "$MPC_VST/tools/catalog_check.py" "$output/MonoSlide-$version-mpc-armv7.zip" \
    --catalog --expect-id monoslide --expect-repo Eurikon/mpc-vst-plugins
