# Deluge Owlet Firmware (1.3 Port)

This is **owlet-firmware**, a personal fork of the Deluge Community Firmware maintained at [owlet-labs/DelugeFirmware](https://github.com/owlet-labs/DelugeFirmware). It serves as a playground for experimental sound design features that may be too specialized or CPU-intensive for the main community branch.

This branch is based on **Community 1.3** for stability.

### Known limitations

- **Horizontal menus must be enabled** - the UI for these effects requires horizontal menus to function properly. Future work will seek to decouple this requirement.
- Disperser (OWLPASS) has high CPU usage with >8 stages
- Sine shaper (HOOT) is moderately CPU hungry
- Other effects are fairly light

### Features (vs Main)

| Feature | Description |
|---------|-------------|
| **Scatter (Bird Brain)** | Beat-repeat, slice manipulation effect and granular looper. Rate knob controls slice length. See [testing guide](docs/features/scatter-testing.md). |
| **Featherverb** | Lightweight 4-tap FDN/allpass cascade reverb. 77 KB buffer vs Mutable's 128 KB. See [testing guide](docs/features/featherverb-testing.md). |
| **Multiband OTT Compressor (DOTT)** | 3-band upward/downward compressor with CHARACTER, RATIO, VIBE, and SKEW controls. 8 vibe zones for dynamic character. Can function as full downward, full upward, or anywhere in between. Aggressively optimized. Includes band-level metering. See [testing guide](docs/features/dott-testing.md). |
| **Retrospective Sampler** | Lookback buffer for capturing audio after the fact. Sources: Input, Master mix, or Focused Track. Duration modes: time-based (5/15/30/60 sec) or bar-synced (1/2/4 bars). Bar modes tag filenames with BPM. See [testing guide](docs/features/retrospective-sampler-testing.md). |
| **Sine Shaper (HOOT)** | Harmonic waveshaping with drive, harmonic content, symmetry, and mix controls. Adds musical saturation and overtones. See [testing guide](docs/features/sine-shaper-testing.md). |
| **Table Shaper (PELLET)** | XY wavetable-based waveshaper with smooth interpolation between shapes. See [testing guide](docs/features/table-shaper-testing.md). |
| **Disperser (OWLPASS)** | Allpass cascade effect with zone-based topology. Available per-voice and in GlobalEffectable chain. High CPU usage with >8 stages. |
| **Pulse Width Triangle** | New oscillator type (TrianglePW / TRPW). Triangle wave with dead-zone pulse width control — PW knob compresses the triangle into a narrower region with silence in the dead zone. Waveform visualization in pulse width menu. |
| **Automodulator** | Automodulation effect with phi triangle routing and 4-point XY LFO wavetable. Available per-voice and in GlobalEffectable chain. |
| **Phi Morph Oscillator** | Procedural oscillator generating waveforms from phi-triangle banks. Two zone knobs (A/B) each produce a distinct 8-segment waveform shape. Crossfade (Wave Position) morphs between them with morph excitation effects (amplitude overshoot, curvature boost, phase distortion). Waveform shaping via sine blend, odd symmetry, windowing, and slope matching. Per-sample modifiers: phase jitter, amplitude-dependent noise, asymmetric gain for even harmonics. Supports pulse width modulation. |
| **Gate Sample Mode** | New sample repeat mode for drum kit rows. Notes act as mute/unmute gates — the sample loops for the duration of the note. If BPM is detected in the filename (e.g. `_120BPM` or bare `_120_`), the sample is tempo-synced and phase-locked so triggering mid-bar starts at the position it would have reached if playing since the downbeat. Without BPM in the filename the sample still gates normally, just without time-stretching or phase-lock. **Gate View Editor:** Press SCALE in kit mode to enter gate view (SCALE LED lights, "GATE" popup). Green pads = audible, red pads = muted. Tap to toggle. Hold first + tap second on same row to span-fill. Hold a green pad + twist vertical encoder to set per-segment attack (fade-in, 0-127), horizontal encoder for release (fade-out). Hold multiple green pads simultaneously to adjust all at once. Exponential time scaling: 0 = instant, ~64 = 50ms, ~96 = 400ms, 127 = ~2.4s. Values are saved per gate segment and serialized with the song. Also available for audio clips. |
| **Plockable Sample Start Offset (positive or negative)** | Per-note start offset for samples, accessible via menu. In synced modes (GATE/STRETCH), offset is applied as a tick shift for phase-correct playback. |
| **Gate View Editor:** | Press SCALE in kit mode to enter gate view (SCALE LED lights, "GATE" popup). Green pads = audible, red pads = muted. Tap to toggle. Hold first + tap second on same row to span-fill. Hold a green pad + twist vertical encoder to set per-segment attack (fade-in, 0-127), horizontal encoder for release (fade-out). Hold multiple green pads simultaneously to adjust all at once. Exponential time scaling: 0 = instant, ~64 = 50ms, ~96 = 400ms, 127 = ~2.4s. Values are saved per gate segment and serialized with the song. Also available for audio clips. |
| **Plockable Start Offset** | Per-note start offset for samples, accessible via menu. In synced modes (GATE/STRETCH), offset is applied as a tick shift for phase-correct playback. (todo proper waveform shift in the visual pad depiction of the waveform) |
| **FX Benchmarking Framework** | Performance profiling tools for DSP development. |

### For Developers

**DSP Documentation:**
- [Phi Triangle Parameter Evolution](docs/dev/phi-triangle.md) - Design pattern for multi-parameter control evolution using golden ratio frequencies

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
