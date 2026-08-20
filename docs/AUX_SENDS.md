# AUX Sends Feature Guide

An assignable AUX send bus on the Deluge's two CV sockets. Send a copy of any clip to CV1/CV2 independently of the main mix. 7-Segment Deluge only.

## Controls

### Global Settings > AUX menu

- **CV1L** — master send level for CV1 (0–50)
- **CV2L** — master send level for CV2 (0–50, hidden when SPLT is on)
- **SPLT** — toggle between stereo pairing (on) or independent mono (off). Toggled with dot on 7SEG

### Per-clip AUX send (in Clip menu)

Available on any clip (synth, kit, audio, sampler).

When SPLT is **on** (stereo):
- **CV** — send level to both CV1 and CV2 as a stereo pair (0–50)
- **MAIN** — adjusts main mix level for this clip

When SPLT is **off** (independent mono):
- **CV1** — send level to CV1 (0–50)
- **CV2** — send level to CV2 (0–50)
- **MAIN** — adjusts main mix level for this clip

All send parameters are:
- Automatable (record automation in Session view)
- MIDI-learnable (press LEARN while a MIDI control sends data)
- Gold-knob assignable (only on synth tracks — base firmware constraint)

### Master AUX level (Song view)

While AFFECT ENTIRE is pressed in Song view, press mod button 7's bottom encoder (gold knob). This is the master send level across all clips — independent of per-clip send levels.

## Workflow example

**Routing a synth through external effects:**

1. Set SPLT **on** (stereo) or **off** (mono), depending on your external gear
2. In Clip menu > AUX: adjust **CV** (stereo) or **CV1**/**CV2** (mono) to send level you want
3. Automate the send by recording a clip automation lane for CV (or CV1/CV2)
4. Master level in Song view scales all sends simultaneously without resetting clips

**Submixing:** Send multiple clips to the same CV output and process them together through an external mixer or effects chain.

## Audio quality

- Bitrate: 16-bit (constrained by DAC on SPI chip-select pin)
- Streaming rate: ~47 kHz per channel (two channels interleaved over one SPI bus, SPBR=9)
- Frequency response: flat within ±3 dB to 4.7 kHz, −3 dB at ~6 kHz, −6 dB at 10 kHz (single-pole rolloff, 700 Hz filter corner, fixed ×6.87 correction applied)
- Noise floor: worst-case non-harmonic spur −60 dB, median −86 dB (main outs for reference: −79/−92)
- Distortion: −50 to −53 dB (main outs: −80 to −91)
- Working headroom: ~10,000 counts either side of centre before compression (about a third of the converter's swing)
- Latency: ~16.4 ms behind the main outputs (768 frames), i.e. the deliberate cost of the sync fix
- Subjectively: sounds like cassette quality or better

The implementation uses a real 16-bit DAC on a dedicated hardware chip-select pin on the processor (no software emulation).

## Known limitations

### Hardware-level

- **7SEG only** — OLED shares the SPI bus with the CV DAC, making OLED support technically impossible. This is a hardware constraint, not a software limitation. The only potential compromises would be either 1.) freezing the screen while the CV sockets are being used to send audio, or 2.) forcing normal screen functioning and instead accepting 17-25ms audio dropouts on the CV audio. Both options are obviously unacceptable. [Here is a brief report of my findings](https://github.com/user-attachments/files/31203852/Deluge-CV-sends-OLED-compatibility-investigation-2026-08-19.md). This has also been separately discussed earlier by the devs on the Discord — independently confirming the same.

### Firmware-level

- **Gold-knob assignment on kits/audio tracks** — doesn't work. This is a base firmware limitation: gold-knob assignment is only implemented for synth tracks. Not specific to AUX Sends.
- **Kit and sample tracks read quieter** — post-fader, confirmed not a fault. Kit and sampler tracks at the same send value will read slightly quieter at the CV socket than a synth track. This is expected behavior.
- **Reverb tail on MAIN-off tracks** — if you turn MAIN level to 0 on a track with reverb, the reverb tail still reaches the main outputs. This is accepted and not fixed.

## Binary size

- Growth: +28.7 KB binary
- Percentage increase: +1.70% against a from-scratch stock control build
- Status: well under the project's 5% threshold

## No hardware mods

The feature uses the existing CV sockets. No soldering or hardware modifications are needed.

---

**⚠️ Experimental feature! Please install and use this at your own risk.**
