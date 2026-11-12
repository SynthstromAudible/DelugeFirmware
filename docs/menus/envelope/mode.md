How the envelope behaves.

- ADSR: attack, decay, sustain and release, as always.
- AHD: attack, hold at full level, then decay to zero. It's one-shot, so it ignores note-off, which suits drums. The
  decay param sets the hold time and the release param sets the decay time, so the release gold knob sets a drum's
  length. Sustain isn't used.
- AR: attack and release around a level that follows MPE pressure (Z, or aftertouch). The envelope heads for
  sustain + (1 - sustain) x pressure, at the attack rate when rising and the release rate when falling. With sustain at
  maximum it's a plain attack-release envelope. Note-off releases to zero. Decay isn't used.
