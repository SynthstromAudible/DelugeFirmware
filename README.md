# Deluge Owlet Firmware (1.3 Port)

This is **owlet-firmware**, a personal fork of the Deluge Community Firmware maintained at [owlet-labs/DelugeFirmware](https://github.com/owlet-labs/DelugeFirmware). It serves as a playground for experimental sound design features that may be too specialized or CPU-intensive for the main community branch.

This branch is based on **Community 1.3** for stability.

### Known limitations

- **Horizontal menus must be enabled** - the UI for these effects requires horizontal menus to function properly. Future work will seek to decouple this requirement.
- Disperser has high CPU usage with >8 stages
- Sine shaper is moderately CPU hungry
- Other effects are fairly light

### Features (vs Community 1.3)

| Feature | Description |
|---------|-------------|
| **Scatter (Bird Brain)** | Beat-repeat and slice manipulation effect. Double-buffer system for glitch-free triggering. Rate knob controls slice length. Momentary mode supported. |
| **Featherverb** | Lightweight 4-tap FDN reverb. 77 KB buffer vs Mutable's 128 KB (40% smaller). ~19k cycles under heavy load (DX7), ~10k with simple synths. Multiple modes: Feather, Owl, Sky, Vast. |
| **Multiband OTT Compressor (DOTT)** | 3-band upward/downward compressor with CHARACTER, RATIO, VIBE, and SKEW controls. 8 vibe zones for dynamic character. Can function as full downward, full upward, or anywhere in between. Aggressively optimized. Includes band-level metering. |
| **Retrospective Sampler** | Lookback buffer for capturing audio after the fact. Sources: Input, Master mix, or Focused Track. Duration modes: time-based (5/15/30/60 sec) or bar-synced (1/2/4 bars). Bar modes tag filenames with BPM. |
| **Sine Shaper (HOOT)** | Harmonic waveshaping with drive, harmonic content, symmetry, and mix controls. Adds musical saturation and overtones. |
| **Table Shaper (PELLET)** | XY wavetable-based waveshaper with smooth interpolation between shapes. |
| **FX Benchmarking Framework** | Performance profiling tools for DSP development. |

### Not Yet Ported

| Feature | Status |
|---------|--------|
| Disperser (OWLPASS) | Pending - high CPU usage with >8 stages |
| Pulse Width Triangle | Pending |
| Automodulator | Shelved - stability issues during preset switching |

### For Developers

This fork is open source under the same GPL-3.0 license as the community firmware. Feel free to:
- Fork this repo for your own experiments
- Cherry-pick individual features into your own builds
- Submit PRs to the main community repo if you refine something worth sharing

No guarantees of stability or compatibility. Use at your own risk.

---

## Dear Deluge owners
If you want to start using the community firmware please visit https://synthstromaudible.github.io/DelugeFirmware/ for all relevant details.

## About
The [Deluge](https://synthstrom.com/product/deluge/) from [Synthstrom Audible](https://synthstrom.com/) is a portable sequencer, synthesizer and sampler. Synthstrom Audible decided to [open source](https://synthstrom.com/open/) the firmware application that runs on the Deluge to allow the community to customize and extend the capabilities of the device.

The hardware is built around a Renesas RZ/A1L processor with an Arm® Cortex®-A9 core running at 400MHz and 3MB of on-chip SRAM. Connected to that is a 64MB SDRAM chip, a PIC for button handling and either a 7-Segment array or an OLED screen. The application is written in C and C++ and some occasional assembly in bare metal style without an operating system.

## Important links
* For acquiring and using the firmware visit the [community firmware website](https://synthstromaudible.github.io/DelugeFirmware)
* Once you have the firmware, consult our detailed [update guide](https://github.com/SynthstromAudible/DelugeFirmware/wiki/Update-guide) for instructions on how to install it
* To contribute to the project with code, bug reports, suggestions, and feedback see: [How to contribute to Deluge Firmware](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/docs/CONTRIBUTING.md)
* To learn about the toolchain, application, and to start working on the code continue with: [Getting Started with Deluge Development](https://github.com/SynthstromAudible/DelugeFirmware/blob/community/docs/dev/getting_started.md)
* Please also visit the [Synthstrom Forum](https://forums.synthstrom.com/) and the community [Discord](https://discord.gg/BnRcyFSgaT) to interact with other users and the developers
* You can donate to the project using the [Synthstrom Patreon](https://www.patreon.com/Synthstrom)
