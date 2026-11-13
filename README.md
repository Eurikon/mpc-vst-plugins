# Twisted Cable - MPC Plugins

[![Releases](https://img.shields.io/badge/Releases-GitHub-blue?logo=github)](https://github.com/Eurikon/mpc-vst-plugins/releases)
[![Report an issue](https://img.shields.io/badge/Issues-GitHub-red?logo=github)](https://github.com/Eurikon/mpc-vst-plugins/issues)

Instruments and effects for MPC standalone.<br>
Each plugin has its own source, license and release package.

|  | Plugin | Type |
| --- | --- | --- |
| <a href="Instruments/Monoslide/README.md"><img src="media/monoslide.png" width="160" alt="MonoSlide Voice page"></a> | [MonoSlide](Instruments/Monoslide/README.md) | Instrument |
| <a href="Instruments/HexaChord-73/README.md"><img src="media/hexachord-73.png" width="160" alt="HexChord 73 Perform page"></a> | [HexChord 73](Instruments/HexaChord-73/README.md) | Instrument |

## Hardware and software compatibility

**Tested locally:** MPC Live 2 Retro — Gen1 / AKAI MPC OS 3.9.1, 3.10 and 3.10.1.<br>
Gen2 builds are checked offline on ARM64 Linux; physical Gen2 testing remains pending.<br>
Each plugin's page records its compatibility and test coverage.

## Build from source

Follow each plugin's README for its commands and pinned external dependencies.<br>
No Docker images, Akai firmware or local development toolchain are bundled here.<br>
Build workflows use the external [Open MPC VST build tools](https://github.com/open-mpc/mpc-vst-plugins).<br>
That project established the standalone plugin build, package and community catalog workflow used by this collection.

## Releases and installation

Each plugin is packaged and submitted to the community catalog separately.<br>
Release assets use plugin-specific names; source updates do not publish a new installer automatically.<br>
On MPC OS 3.10.1 or later, copy the SDK-format plugin folder into `VST2 Plugins` at the root of a drive.<br>
The [OpenMPC Plugin Manager](https://github.com/open-mpc/mpc-vst-plugins/tree/main/tools/plugin_manager_g2) also installs community releases.<br>
Existing [Plugin Manager USB](https://github.com/Eurikon/plugin-manager-usb-for-open-mpc) installations remain an option on earlier supported firmware.

## Licensing

Licenses apply per plugin, including its third-party notices.<br>
There is no single license covering the entire collection.
