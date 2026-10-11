# Deluge MDD Snake + Plaits — custom firmware

Unofficial OLED-focused modification of Synthstrom Audible's Deluge Community Firmware, based on 1.3.0 beta (Derbyshire), upstream commit `3e06e01d713001e546480a3153f01570c0d05fa0`.

**Modified version, published 11 October 2026.** This repository contains editable source code, third-party source, build scripts and regression tests. It is not an official Synthstrom or Mutable Instruments release.

- MDD Snake: alternative clip sequencer with Notes, Gate, Velocity and four independent Custom modulation lanes; OLED/grid editing, Song root/scale integration, WRITE and Undo.
- Plaits: FM, Waveshaping and Virtual Analog oscillator engines, with mappable engine selection. This is a three-engine subset, not the complete Plaits module.
- Additional startup, OLED, effects and overload-safety changes.

**Current status:** source-only CPU audit update. Host regression tests passed; this latest update has not been ARM-built or tested on physical hardware. No new BIN is published with this commit. Do not assume an older BIN contains these changes.

Read [changes and attribution](CUSTOM_CHANGES.md), [licensing](THIRD_PARTY_NOTICES.md), [build instructions](BUILD_CUSTOM.md), and the [CPU audit](docs/custom/CPU_AUDIT.md).

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
