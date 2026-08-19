# Deluge AUX Sends Firmware

Custom firmware for the Synthstrom Deluge adding an assignable AUX send bus on the two CV sockets.

## What you get

- Send any clip to CV1/CV2 independently of the main mix
- Per-clip send levels (0–50), master send levels (0–50)
- SPLIT mode to pair CV sockets as stereo, or use independently as mono
- Automatable and MIDI-learnable send controls
- Gold-knob assignable on synth tracks

## Compatibility

**⚠️ 7-segment Deluge only.** OLED is not supported due to a hardware constraint: the CV DAC and OLED screen share the same SPI bus, making them mutually exclusive.

## Installation

### For users (flash only)

1. **Back up your SD card** — copy the entire card to your computer first
2. Download the `.bin` file from [Releases](https://github.com/sticknobills/DelugeFirmware/releases)
3. Copy the `.bin` file to the root of your SD card (no subfolders)
4. Insert the card into your Deluge and power off
5. Hold SHIFT while powering on
6. Wait approximately 1 minute for the flash to complete — do not interrupt or power off
7. Verify: SHIFT + SELECT → Firmware Version (should show the new version)
8. Your songs and samples are unaffected by the flash

### For developers (build from source)

Clone this repository and follow the [upstream build instructions](https://github.com/SynthstromAudible/DelugeFirmware/blob/main/CONTRIBUTING.md):

```bash
git clone https://github.com/sticknobills/DelugeFirmware.git
cd DelugeFirmware
git checkout feature/aux-sends
# Then follow upstream build steps
```

## Documentation

See [AUX Sends Feature Guide](docs/AUX_SENDS.md) for detailed controls, workflow, and known limitations.

## About this fork

This is a downstream fork of [SynthstromAudible/DelugeFirmware](https://github.com/SynthstromAudible/DelugeFirmware). The AUX Sends feature is independent and maintained separately on `feature/aux-sends`, rebasing regularly onto upstream `main` to stay current with upstream development.

### Development workflow

- **Working branch:** `feature/aux-sends` (this is where the feature lives)
- **Upstream tracking:** rebased monthly to stay current with upstream/main
- **Feature isolation:** only 6 commits on top of upstream — all AUX Sends code is separate and doesn't interfere with base firmware updates

### Building

Build instructions are in the upstream repository (this fork uses the same build system). The AUX Sends feature adds minimal overhead: +28.7 KB binary, ~1.7% increase from a stock build, well under the project's 5% threshold.

### Why this fork exists

The AUX Sends feature was contributed as [PR #4829](https://github.com/SynthstromAudible/DelugeFirmware/pull/4829) upstream but remains experimental and unlikely to be merged into the main firmware. This fork exists to ship and maintain the feature independently for users who want it, while allowing upstream to evolve without carrying it.

## Technical details

- **Audio quality:** flat within ±3 dB to 4.7 kHz; distortion floor −50 to −53 dB (main outputs for reference: −80 to −91 dB)
- **Implementation:** real 16-bit DAC on a dedicated hardware chip-select pin on the processor
- **No soldering or hardware mods needed** — uses existing CV sockets

## Known limitations

- Gold-knob assignment only works on synth tracks (base firmware constraint, not this feature's limitation)
- Kit and sample tracks read slightly quieter at the CV sockets than synth tracks at the same send value
- Reverb tail on MAIN-off tracks still reaches main outputs (accepted, not fixed)

## License

GPL-3.0 (inherited from Deluge community firmware)

---

Questions? Open an issue on GitHub.
