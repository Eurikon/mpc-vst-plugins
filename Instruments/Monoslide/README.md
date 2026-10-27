# MonoSlide for MPC

[![Build checks](https://github.com/Eurikon/mpc-vst-plugins/actions/workflows/monoslide-check.yml/badge.svg?branch=main)](https://github.com/Eurikon/mpc-vst-plugins/actions/workflows/monoslide-check.yml)
[![GPLv3 license](https://img.shields.io/badge/License-GPLv3-blue.svg)](LICENSE)
[![Download 1.0.3](https://img.shields.io/badge/Download-1.0.3-brightgreen?logo=github)](https://github.com/Eurikon/mpc-vst-plugins/releases/download/monoslide-v1.0.3/MonoSlide-1.0.3-mpc-armv7.zip)

![MonoSlide Voice page](../../media/monoslide.png)

MonoSlide is a monophonic acid bass instrument ported from TwinSlide's MonoSlide voice.<br>
It uses MPC's sequencer and receives MIDI on all channels, with last-note priority, overlapping-note slide and velocity-triggered accent.<br>
There are 18 sound parameters across Voice and Expert pages, plus a shared theme selector: Default, Copper, Glacier, Moss, Amethyst and Ember.<br>
The theme is saved with the instrument; older states select Default.

**Current release:** 1.0.3, targeting ARMv7 and MPC OS 3.x.

## Build and test

Requires Docker with ARMv7 support, Python 3, Make and a C++17 compiler.<br>
Linux x86 emulation is also needed for wrapper tests on macOS or ARM hosts.<br>
Local skin previews require Python 3.11 and Chromium's runtime dependencies.<br>
Use the external [sd88me build tools](https://github.com/sd88me/mpc-vst-plugins) at the pinned revision below; they are not bundled in this repository.

```sh
git clone https://github.com/Eurikon/mpc-vst-plugins.git
cd mpc-vst-plugins
git clone https://github.com/sd88me/mpc-vst-plugins.git ../mpc-vst-build-tools
git -C ../mpc-vst-build-tools checkout fc1ea20ff83779d3f47fb43177f5284f59e57a99
export MPC_VST="$(cd ../mpc-vst-build-tools && pwd)"
cd Instruments/Monoslide
./test.sh
make skin
./build.sh
./package.sh 1.0.3
```

- `build.sh` applies or verifies `skin-tools.patch` and `release-tools.patch`, generates the skin and parameter table, then builds the ARM plugin using the upstream Docker workflow.
- `test.sh` runs engine tests and the original Linux x86 ASan/UBSan wrapper test, using Docker on macOS or ARM hosts.
- `make skin` creates local page previews with an isolated Python/Playwright environment under ignored `build/art-venv/` and `build/browser/`, using [requirements-art.txt](requirements-art.txt).<br>
  Existing development caches are reused when available.<br>
  Override `ART_PYTHON`, `ART_VENV` or `ART_BROWSER` as needed.
- `package.sh` creates `dist/MonoSlide-1.0.3-mpc-armv7.zip` and runs the upstream catalog validator.<br>
  CPU metadata is included only when a valid `bench.txt` exists.<br>
  An optional second argument selects another output directory.

Outputs are under ignored `build/` and `dist/`.<br>
The patches correct themed dropdown rendering, resource names and generated installation instructions; they do not change DSP or parameters.

## Installation

Install the ZIP with the [Plugin Manager USB](https://github.com/Eurikon/plugin-manager-usb-for-open-mpc), following its instructions.<br>
The package includes install/uninstall scripts and installation requirements.<br>
Its installer executes as root and restarts the MPC application; SSH is not required.

## Local validation

**Tested on:** MPC Live 2 Retro — Gen1 / AKAI MPC OS 3.9.1.

- Playback, project save/reload and two playing instances: confirmed on hardware during development.
- Interface and theme controls: confirmed on 06-10-2026; the centered title was confirmed on 13-10-2026.
- Q-Link automation: confirmed with MPC automation set to Write; no Q-Link issue remains.
- Installation, managed removal and reinstallation on the SD card: verified on 14-10-2026, including the installed payload, USB manifest and manager reopening shortcut.
- Idle CPU processing: reduced from **8.7%** to **7.5%** of one core on 14-10-2026, about **14% less CPU**.
- Device CPU benchmark: **PASS** on 14-10-2026, with p99 **10.5%** and maximum block **15.9%**.
- Regression validation: host, sanitized and native ARM output checks matched exactly across **27 cases and 2,772,485 frames** before and after optimization.
- Final release ZIP: all **24** offline installation/reinstallation/tamper/rollback/uninstallation checks passed.

CPU percentages measure a single core's 128-frame audio-block budget at 44,100 Hz, rather than the MPC overall CPU meter.<br>
The current package includes the measured CPU result and tested library SHA-256 `c264f95582bf3ebac5a0a60e8ed093ae4b274f57fd02ebee838f6695947188ad`.<br>
OS 2.x, Gen2 and other device/firmware combinations remain unverified.

## Licensing

GPL-3.0-or-later, with MIT Open303 components and public-domain Ooura FFT.<br>
See [LICENSE](LICENSE), `licenses/` and [src/VENDORED.md](src/VENDORED.md) for DSP provenance.
