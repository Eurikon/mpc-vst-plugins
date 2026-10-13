# Twisted Cable MPC Plugins

Instruments and effects for MPC standalone. Each plugin has its own source,
license and release package; installable ZIPs belong in GitHub Releases.

| Plugin | Type | License | Source |
| --- | --- | --- | --- |
| MonoSlide | Monophonic acid bass instrument | GPL-3.0-or-later | [Build and usage](Monoslide/README.md) |

MonoSlide targets MPC OS 3.x. Development checks used MPC Live II Retro / OS
3.9.1. Its final release-package hardware smoke test remains pending; see the
plugin README for the recorded checks and their scope.

## Build from source

Follow each plugin's README for its commands and pinned external dependencies.
No Docker images, Akai firmware or local development toolchain are bundled here.
MonoSlide uses the external [sd88me MPC VST build tools](https://github.com/sd88me/mpc-vst-plugins).
That project established the standalone plugin build, package and community
catalog workflow on which this port builds.

## Releases and installation

Each plugin is packaged and submitted to the community catalog separately.
Release assets use plugin-specific names; source updates do not publish a new
installer automatically. The [USB Plugin Manager](https://github.com/Eurikon/plugin-manager-usb)
can install suitable released packages on the MPC.

## Licensing

Licenses apply per plugin, including its third-party notices. There is no single
license covering the entire collection. Open MPC releases from this project
are open source and free of charge; paid releases are outside this project's
publication policy unless AKAI establishes an authorized commercial platform.
