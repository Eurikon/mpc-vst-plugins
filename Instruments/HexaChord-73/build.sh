#!/usr/bin/env bash
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
cd "$here"
export MPC_VST="$(cd "${MPC_VST:-../../toolchain/upstream}" && pwd)"
pin=fc1ea20ff83779d3f47fb43177f5284f59e57a99
mode="${1:-arm}"
[[ $# -le 1 && "$mode" =~ ^(arm|test|skin)$ ]] || { echo 'Usage: ./build.sh [arm|test|skin]' >&2; exit 1; }
if [[ "$MPC_VST" != "$(cd ../../toolchain/upstream 2>/dev/null && pwd || true)" ]]; then
    [[ "$(git -C "$MPC_VST" rev-parse HEAD)" = "$pin" ]] || { echo "MPC_VST must use $pin" >&2; exit 1; }
fi
python3 - <<'PY'
import hashlib, json, os, subprocess
from pathlib import Path
root, tools = Path.cwd(), Path(os.environ['MPC_VST'])
expected = json.loads((root / 'upstream.json').read_text())['patched_files']
def matches():
    return all((tools / name).is_file() and hashlib.sha256((tools / name).read_bytes()).hexdigest() == value
               for name, value in expected.items())
if not matches():
    subprocess.run(['git', '-C', tools, 'diff', '--quiet', 'HEAD', '--', *expected], check=True)
    for name in ('skin-tools.patch', 'release-tools.patch', 'qlink-state-reset.patch', 'qlink-continuous-enum.patch'):
        subprocess.run(['git', '-C', tools, 'apply', root / name], check=True)
    if not matches():
        raise SystemExit('Patched upstream files differ from the approved pin')
PY
python3 metadata.py
python3 skin.py
python3 "$MPC_VST/tools/gen_vst.py" vst.json --params-h
if [[ "$mode" = skin ]]; then
    art_venv="${ART_VENV:-build/art-venv}"
    if [[ ! -x "$art_venv/bin/python" ]]; then "${ART_PYTHON:-python3.11}" -m venv "$art_venv"; fi
    "$art_venv/bin/python" -c 'import PIL, playwright' 2>/dev/null || "$art_venv/bin/python" -m pip install -r requirements-art.txt
    export PLAYWRIGHT_BROWSERS_PATH="${ART_BROWSER:-$here/build/browser}"
    "$art_venv/bin/python" -c 'from pathlib import Path; from playwright.sync_api import sync_playwright; p=sync_playwright().start(); executable=p.chromium.executable_path; p.stop(); assert Path(executable).is_file()' 2>/dev/null || "$art_venv/bin/python" -m playwright install chromium
    PATH="$(cd "$art_venv/bin" && pwd):$PATH" SHADOW_ART="$MPC_VST/tools/html_art.py" "$art_venv/bin/python" "$MPC_VST/tools/gen_vst.py" vst.json
    "$art_venv/bin/python" skin.py --finalize 'build/skin/Twisted Cable - VST - HexChord 73/Plugin Skins'
    "$art_venv/bin/python" skin.py --preview 'build/skin/Twisted Cable - VST - HexChord 73/Plugin Skins' -o 'build/preview_%d.png'
    exit
fi
if [[ "$mode" = arm ]]; then
    mkdir -p build/portable-arm
    docker run --rm --network none --platform linux/arm/v7 -u "$(id -u):$(id -g)" \
        -v "$here:/work" -v "$MPC_VST:/upstream:ro" -w /work \
        "${MPC_BUILD_IMAGE:-arm32v7/gcc:11-bullseye}" bash -euc '
        flags=(-O3 -DNDEBUG -fPIC -fvisibility=hidden -ffp-contract=off -pthread -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -Isrc -Ibuild/native -Ibuild -I/upstream/wrapper -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard -flto)
        g++ -std=c++17 "${flags[@]}" -mtune=cortex-a17 -fno-fast-math -c /work/src/native_dsp.cpp -o build/portable-arm/engine-0.o
        g++ -std=c++17 "${flags[@]}" -c /work/src/engine.cpp -o build/portable-arm/engine-1.o
        gcc -std=gnu11 "${flags[@]}" -c /work/src/vst_ui_wrap.c -o build/portable-arm/vst-wrapper.o
        g++ -shared -Wl,--no-undefined build/portable-arm/engine-0.o build/portable-arm/engine-1.o build/portable-arm/vst-wrapper.o -pthread -lm -flto -mtune=cortex-a17 -o build/portable-arm/hexachord-73.so
        strip build/portable-arm/hexachord-73.so
        cp build/portable-arm/hexachord-73.so build/hexachord-73.so
        readelf -h build/hexachord-73.so'
    exit
fi
out=build/portable-sanitize
mkdir -p "$out"
cc="${CC:-clang}"
cxx="${CXX:-clang++}"
flags=(-O1 -g -fPIC -fvisibility=hidden -ffp-contract=off -pthread -Wall -Wextra -Wno-unused-parameter -D_GNU_SOURCE -Isrc -Ibuild/native -Ibuild "-I$MPC_VST/wrapper" -fsanitize=address,undefined -fno-omit-frame-pointer)
"$cxx" -std=c++17 "${flags[@]}" -c src/native_dsp.cpp -o "$out/dsp.o"
"$cxx" -std=c++17 "${flags[@]}" -c src/engine.cpp -o "$out/engine.o"
"$cc" -std=gnu11 "${flags[@]}" -c src/vst_ui_wrap.c -o "$out/wrapper.o"
# Sanitized Linux libraries resolve sanitizer hooks from the test host.
shared=(-shared)
if [[ "$(uname -s)" = Darwin ]]; then shared=(-dynamiclib -Wl,-undefined,error); fi
"$cxx" "${shared[@]}" "$out/dsp.o" "$out/engine.o" "$out/wrapper.o" -pthread -lm -fsanitize=address,undefined -o "$out/hexachord-73.so"
"$cxx" -std=c++17 "${flags[@]}" -DHEXACHORD_TESTING tests/native_engine_test.cpp src/engine.cpp "$out/dsp.o" -lm -o "$out/engine-test"
"$cxx" -std=c++17 "${flags[@]}" tests/vst_host_test.cpp -ldl -o "$out/vst-test"
"$out/engine-test"
"$out/vst-test" "$out/hexachord-73.so"
