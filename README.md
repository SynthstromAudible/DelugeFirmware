# Deluge MDD Snake + Plaits — custom firmware

Unofficial OLED-focused modification of Synthstrom Audible's Deluge Community Firmware, based on 1.3.0 beta (Derbyshire), upstream beta commit `be3e993c4ae78719d3940f334848aa77973ea585`, plus reverb PR #4990.

**Modified source updated 12 October 2026.** This repository contains editable source code, third-party source, build scripts and regression tests. It is not an official Synthstrom or Mutable Instruments release.

- MDD Snake: alternative clip sequencer with Notes, Gate, Velocity and four independent Custom modulation lanes; OLED/grid editing, Song root/scale integration, WRITE and Undo.
- Plaits: FM, Waveshaping and Virtual Analog oscillator engines, with mappable engine selection. This is a three-engine subset, not the complete Plaits module.
- Additional startup, OLED, effects and overload-safety changes.

**Download:** [OLED firmware BIN](https://raw.githubusercontent.com/mileda/DelugeFirmware/main/firmware/deluge-mdd-plaits-oled-a13bf0fb.bin) · [Installation instructions](firmware/INSTALL.md) · [Exact source](https://github.com/mileda/DelugeFirmware/tree/a13bf0fba59f13217129a90c20b714223deef6a5)

**Current status:** experimental ARM Release build available, compiled from source commit `a13bf0fba59f13217129a90c20b714223deef6a5` with DBT toolchain v22. Host regression tests passed. Physical-device testing has not been performed. This is not a stable official release. See [build details and checksum](firmware/BUILD_INFO.json).

Read [beta integration and all 15 retained changes](docs/custom/BETA_INTEGRATION_20261012.md), [changes and attribution](CUSTOM_CHANGES.md), [licensing](THIRD_PARTY_NOTICES.md), [build instructions](BUILD_CUSTOM.md), and the [CPU audit](docs/custom/CPU_AUDIT.md).

## User guide (PDF)

[Read the MDD Snake + Plaits guide](docs/custom/Deluge-MDD-Plaits-Guide.pdf) · [Download PDF](https://raw.githubusercontent.com/mileda/DelugeFirmware/main/docs/custom/Deluge-MDD-Plaits-Guide.pdf)

Original English guide, dated 10 October 2026, including the interface diagram, usage instructions, release notes and Plaits integration. Later optimization and safety changes are documented in the [CPU audit](docs/custom/CPU_AUDIT.md) and [Plaits performance notes](docs/custom/PLAITS_PERFORMANCE_20261011.md). For the current BIN and installation steps, use [firmware/INSTALL.md](firmware/INSTALL.md).

## License

The combined firmware is distributed under **GNU GPL version 3 or later**; see [LICENSE](LICENSE). Existing copyright notices and component-specific licenses are retained. The imported Plaits/stmlib code is by Emilie Gillet under the MIT license; see [its notices](src/plaits_vendor/LICENSE-MIT.txt). No exclusive ownership of upstream work is claimed.

---

## Upstream project information

# Deluge Community Firmware

## Dear Deluge owners
If you want to start using the community firmware please visit https://delugecommunity.com/ for all relevant details.

## About
The [Deluge](https://synthstrom.com/product/deluge/) from [Synthstrom Audible](https://synthstrom.com/) is a portable sequencer, synthesizer and sampler. Synthstrom Audible decided to [open source](https://synthstrom.com/open/) the firmware application that runs on the Deluge to allow the community to customize and extend the capabilities of the device. 

The hardware is built around a Renesas RZ/A1L processor with an Arm® Cortex®-A9 core running at 400MHz and 3MB of on-chip SRAM. Connected to that is a 64MB SDRAM chip, a PIC for button handling and either a 7-Segment array or an OLED screen. The application is written in C and C++ and some occasional assembly in bare metal style without an operating system.

## Important links
* For acquiring and using the firmware visit the [community firmware website](https://delugecommunity.com/)
* Once you have the firmware, consult our detailed [update guide](https://github.com/SynthstromAudible/DelugeFirmware/wiki/Update-guide) for instructions on how to install it
* To contribute to the project with code, bug reports, suggestions, and feedback see: [How to contribute to Deluge Firmware](https://github.com/SynthstromAudible/DelugeFirmware/blob/main/docs/CONTRIBUTING.md)
* To learn about the toolchain, application, and to start working on the code continue with: [Getting Started with Deluge Development](https://delugecommunity.com/development/deluge/getting_started/)
* Please also visit the [Synthstrom Forum](https://forums.synthstrom.com/) and the community [Discord](https://discord.gg/BnRcyFSgaT) to interact with other users and the developers
* You can donate to the project using the [Synthstrom Patreon](https://www.patreon.com/Synthstrom)
