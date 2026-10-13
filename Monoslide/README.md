# MonoSlide for MPC

![MonoSlide Voice page](../media/monoslide.png)

MonoSlide is a monophonic acid bass instrument ported from TwinSlide's MonoSlide voice.<br>
It uses MPC's sequencer and receives MIDI on all channels, with last-note priority, overlapping-note slide and velocity-triggered accent.<br>
There are 18 sound parameters across Voice and Expert pages, plus a shared theme selector: Default, Copper, Glacier, Moss, Amethyst and Ember.<br>
The theme is saved with the instrument; older states select Default.

**Current development version:** 1.0.2, targeting ARMv7 and MPC OS 3.x.<br>
The final release ZIP smoke test remains pending; source preparation and packaging alone do not establish release sign-off.

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
cd Monoslide
./test.sh
make skin
./build.sh
./package.sh 1.0.2
```

- `build.sh` applies or verifies `skin-tools.patch`, generates the skin and parameter table, then builds the ARM plugin using the upstream Docker workflow.
- `test.sh` runs engine tests and the original Linux x86 ASan/UBSan wrapper test, using Docker on macOS or ARM hosts.
- `make skin` creates local page previews with an isolated Python/Playwright environment under ignored `build/art-venv/` and `build/browser/`, using [requirements-art.txt](requirements-art.txt).<br>
  Existing development caches are reused when available.<br>
  Override `ART_PYTHON`, `ART_VENV` or `ART_BROWSER` as needed.
- `package.sh` creates `dist/MonoSlide-1.0.2-mpc-armv7.zip` and runs the upstream catalog validator.<br>
  CPU metadata is included only when a valid `bench.txt` exists.

Outputs are under ignored `build/` and `dist/`.<br>
The patch corrects themed dropdown rendering and resource names; it does not change DSP or parameters.

## Install and develop

Install the ZIP with the [Plugin Manager USB](https://github.com/Eurikon/plugin-manager-usb), following its instructions.<br>
The package includes install/uninstall scripts and installation requirements.<br>
Its installer executes as root and restarts the MPC application; SSH is not required.

## Recorded hardware checks

- MPC Live II, firmware 3.9.1: playback, project save/reload and two playing instances were user-confirmed during development.<br>
  The corrected UI was confirmed on 06-10-2026.
- MPC Live II Retro, OS 3.9.1, 10-10-2026: the original device CPU workload returned **PASS**, with worst p99 **11.9%** and maximum block **17.4%**.<br>
  These percentages measure a single core's 128-frame audio-block budget at 44,100 Hz, rather than the MPC overall CPU meter.<br>
  The tested library SHA-256 was `6d0ecf239f059286e549e6c3589a2af5a61d141d450452b3c2a37407299fc3e8`.

These are historical development checks.<br>
A complete smoke test of the exact final ZIP remains pending, including installation, all controls/Q-Links, playback, save/reload, two instances, uninstallation and preservation checks.<br>
OS 2.x, Gen2 and other device/firmware combinations remain unverified.

## Compatibility and catalog

Keep UID `TcMs`, library `monoslide.so` and parameter order fixed.<br>
Append new parameters; changing existing indices breaks saved projects and requires a major public version change.

[catalog/monoslide.json](catalog/monoslide.json) identifies `Eurikon/mpc-vst-plugins` and selects `MonoSlide-*-mpc-armv7.zip` release assets.<br>
Publish and submit only the exact final ZIP after hardware sign-off.<br>
No versions or checksums belong in the registry entry.

## Licensing

GPL-3.0-or-later, with MIT Open303 components and public-domain Ooura FFT.<br>
See [LICENSE](LICENSE), `licenses/` and [src/VENDORED.md](src/VENDORED.md) for DSP provenance.<br>
No firmware or ROMs are included.
