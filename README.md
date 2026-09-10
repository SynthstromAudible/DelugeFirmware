# Deluge AUX Sends Firmware (7-segment only)

An assignable AUX send bus on the Deluge's two CV sockets. Send a copy of any clip to CV1/CV2 independently of the main mix. Send levels and AUX master level send are adjustable like on a typical mixing board. No soldering or hardware mods needed.


## Features

- Send any clip to CV1/CV2 independently of the main mix
- Per-clip send levels (0–50), master send levels (0–50)
- SPLIT mode to pair CV sockets as stereo, or use independently as mono
- Automatable and MIDI-learnable send controls
- Gold-knob assignable on synth tracks
  
## Documentation

📖 See [AUX Sends Feature Guide](docs/AUX_SENDS.md) for detailed controls, workflow, audio quality details and known limitations.


## Compatibility

**⚠️ 7-segment Deluge only.** 
Compatibility with OLED devices is impossible because CV DAC and OLED screen use the same SPI bus. This feature was offered as [PR #4829](https://github.com/SynthstromAudible/DelugeFirmware/pull/4829) but remains experimental due to non-portability for OLED users. This fork exists to ship and maintain this feature independently for users who want it anyway. See [AUX Sends Feature Guide](docs/AUX_SENDS.md) for more about compatibility roadblocks.

## Installation
⚠️ Experimental feature! Install at your own risk.

### For users (flash only)

1. **Back up your SD card** — copy the entire card to your computer first
2. Download the latest `.bin` file from [Releases](https://github.com/sticknobills/DelugeFirmware/releases)
3. Copy the `.bin` file to the root of your SD card (no subfolders)
4. Insert the card into your Deluge and power off
5. Hold SHIFT while powering on
6. Wait approximately 1 minute for the flash to complete — do not interrupt or power off
7. Verify: SHIFT + SELECT → Firmware Version (should show the new version)
8. Your songs and samples are unaffected by the flash

### For developers (build from source)

Clone this repository and follow the [upstream build instructions.](https://github.com/SynthstromAudible/DelugeFirmware/blob/main/docs/CONTRIBUTING.md)

```bash
git clone https://github.com/sticknobills/DelugeFirmware.git
cd DelugeFirmware
git checkout feature/aux-sends
# Then follow upstream build steps
```


## About this fork

This repo is a downstream fork of [SynthstromAudible/DelugeFirmware](https://github.com/SynthstromAudible/DelugeFirmware). The AUX Sends feature is independent and maintained separately on `feature/aux-sends`, rebasing regularly onto upstream `main` to stay current with upstream development.


## Known limitations / Pending fixes (as of 1.01)

- Gold-knob assignment only works on synth tracks (base firmware constraint, not this feature's limitation)
- Kit and sample tracks are slightly quieter at the CV sockets than synth tracks at the same send value
- Reverb tail on tracks with MAIN toggled off still reaches main outputs (acceptable behaviour)
- MIDI CC-learn on per-clip sends should work but hasn’t been tested.
- Classic CV/Gate note output hasn’t been tested simultaneous with AUX sends functionality. As of 1.0.1, note output is disabled on both CV sockets whenever either has an active send.

## Bugs

This is an experimental feature in development by one person with a limited testing environment. [Please report any bugs!](https://github.com/sticknobills/DelugeFirmware/issues/new/choose)


## License

GPL-3.0 (from Deluge community firmware)

---
