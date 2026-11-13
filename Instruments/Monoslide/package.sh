#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"
MPC_VST="$(cd "${MPC_VST:-../../toolchain/upstream}" && pwd)"
version="${1:?Usage: ./package.sh X.Y.Z [OUTPUT_DIR]}"
[[ $# -le 2 ]] || { echo "Usage: ./package.sh X.Y.Z [OUTPUT_DIR]" >&2; exit 1; }
[[ "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "Expected X.Y.Z" >&2; exit 1; }
output="${2:-dist}"
make -C "$here" prepare-tools MPC_VST="$MPC_VST"
args=()
if [ -f bench.txt ]; then
    python3 - <<'PYBENCH'
import hashlib, json, math
from pathlib import Path
b = json.loads(Path('bench.txt').read_text().strip().splitlines()[-1])
assert hashlib.sha256(Path('build/monoslide.so').read_bytes()).hexdigest() == Path('bench.sha256').read_text().strip(), 'CPU result is for a different library'
p99, worst = float(b['p99_pct']), float(b['max_pct'])
assert math.isfinite(p99) and math.isfinite(worst) and p99 >= 0 and worst >= p99
verdict = 'PASS' if p99 <= 15 and worst <= 50 else 'WARN' if p99 <= 35 and worst <= 80 else 'FAIL'
if 'triggers' in b or 'trigger_max_pct' in b:
    count, peak = b.get('triggers'), b.get('trigger_max_pct')
    assert type(count) is int and count > 0 and type(peak) in (int, float) and math.isfinite(peak) and peak >= 0
    if peak >= 90 and verdict != 'FAIL':
        verdict = 'WARN'
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
test -f build/aarch64/monoslide.so
python3 "$MPC_VST/tools/release.py" \
    --so build/aarch64/monoslide.so --skin "build/skin/Twisted Cable - VST - MonoSlide" \
    --entry build/pluginlist-entry.xml --version "$version" \
    --repo Eurikon/mpc-vst-plugins --license GPL-3.0-or-later --id monoslide \
    --about "Monophonic acid bass instrument for MPC standalone." \
    --extra LICENSE:LICENSE --extra licenses:licenses -o "$output"
python3 - "$MPC_VST/tools" "$output/MonoSlide-$version-mpc-armv7.zip" "$output/MonoSlide-$version-mpc-aarch64.zip" <<'PYCHECK'
import sys
sys.path.insert(0, sys.argv[1])
from catalog_check import check
for path in sys.argv[2:]:
    errors, warnings, record = check(path, catalog=True, expect_id="monoslide", expect_repo="Eurikon/mpc-vst-plugins", require_portable=True)
    for message in errors + warnings:
        print(message, file=sys.stderr)
    if errors or warnings:
        raise SystemExit('Catalog errors and warnings block release')
print('Catalog package validation: OK, zero warnings')
PYCHECK
python3 "$MPC_VST/tools/sdk_zip.py" "$output/MonoSlide-$version-mpc-sdk.zip" \
    "$output/MonoSlide-$version-mpc-armv7.zip" "$output/MonoSlide-$version-mpc-aarch64.zip"
