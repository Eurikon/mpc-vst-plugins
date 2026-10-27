#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"
export MPC_VST="$(cd "${MPC_VST:-../../toolchain/upstream}" && pwd)"
version="${1:?Usage: ./package.sh X.Y.Z [OUTPUT_DIR]}"
[[ $# -le 2 && "$version" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo 'Expected X.Y.Z [OUTPUT_DIR]' >&2; exit 1; }
output="${2:-dist}"
export HEXCHORD_INSTANCE_NOTE="${HEXCHORD_INSTANCE_NOTE:-CPU verdict WARN: start with one or two instances and check the MPC project CPU meter.}"
test -f build/hexachord-73.so
test -f bench.txt
python3 - <<'PY'
import hashlib, json, math, os
from pathlib import Path
bench = json.loads(Path('bench.txt').read_text().strip().splitlines()[-1])
p99, maximum = bench['p99_pct'], bench['max_pct']
assert all(type(v) in (int, float) and math.isfinite(v) for v in (p99, maximum))
assert 0 <= p99 <= maximum
verdict = 'PASS' if p99 <= 15 and maximum <= 50 else 'WARN' if p99 <= 35 and maximum <= 80 else 'FAIL'
assert bench['verdict'] == verdict and verdict != 'FAIL', 'CPU result blocks release'
note = os.environ['HEXCHORD_INSTANCE_NOTE']
assert note.strip() and all(c.isprintable() for c in note), 'Instance guidance must be a printable single line'
assert hashlib.sha256(Path('build/hexachord-73.so').read_bytes()).hexdigest() == Path('bench.sha256').read_text().strip(), 'CPU result is for a different library'
PY
python3 "$MPC_VST/tools/release.py" \
    --so build/hexachord-73.so --skin 'build/skin/Twisted Cable - VST - HexChord 73' \
    --entry build/pluginlist-entry.xml --version "$version" --bench bench.txt \
    --repo Eurikon/mpc-vst-plugins --license GPL-3.0-or-later --id hexachord-73 \
    --requires 'Gen1 MPC; MPC OS 3.9.1 or 3.10; SD card plugin storage' \
    --about 'Stereo chord synthesizer with four voice models and native MPC controls.' \
    --extra LICENSE:LICENSE --extra licenses:licenses -o "$output"
python3 - "$output/HexChord-73-$version-mpc-armv7.zip" <<'PY'
import hashlib, json, os, sys, zipfile
from pathlib import Path
path = Path(sys.argv[1])
with zipfile.ZipFile(path) as archive:
    entries = [(info, archive.read(info)) for info in archive.infolist()]
prefix = entries[0][0].filename.split('/')[0] + '/'
updated = []
for info, data in entries:
    relative = info.filename[len(prefix):]
    if relative in ('mpc-plugin.json', 'portable/Twisted Cable - VST - HexChord 73/mpc-plugin.json'):
        manifest = json.loads(data)
        assert manifest['layout'] == 'portable' and manifest['folder'] == 'portable/Twisted Cable - VST - HexChord 73'
        manifest['os_compat'] = ['3.x']
        if manifest['cpu']['verdict'] == 'WARN':
            manifest['cpu']['instance_note'] = os.environ['HEXCHORD_INSTANCE_NOTE']
        data = (json.dumps(manifest, indent=2) + '\n').encode()
    elif relative == 'INSTALL.md':
        bench = json.loads(Path('bench.txt').read_text().strip().splitlines()[-1])
        if bench['verdict'] == 'WARN':
            data += ("\nCPU benchmark: WARN.\n"
                     f"Instance note: {os.environ['HEXCHORD_INSTANCE_NOTE']}\n"
                     "Upstream WARN guidance suggests one or two instances; this is not a measured capacity guarantee.\n").encode()
    updated.append((info, data))
checksums = ''.join(hashlib.sha256(data).hexdigest() + '  ' + info.filename[len(prefix):] + '\n'
                    for info, data in updated if info.filename != prefix + 'SHA256SUMS')
temporary = path.with_suffix('.zip.new')
with zipfile.ZipFile(temporary, 'w') as archive:
    for info, data in updated:
        archive.writestr(info, checksums.encode() if info.filename == prefix + 'SHA256SUMS' else data)
temporary.replace(path)
PY
python3 "$MPC_VST/tools/catalog_check.py" "$output/HexChord-73-$version-mpc-armv7.zip" \
    --catalog --expect-id hexachord-73 --expect-repo Eurikon/mpc-vst-plugins
