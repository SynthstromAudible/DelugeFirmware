How the envelope behaves.

- ADSR: attack, decay, sustain and release, as always.
- AHD: attack, hold at full level, then decay to zero. It's one-shot, so it ignores note-off, which suits drums. The
  decay param sets the hold time (an eighth of what the same value gives a decay, since a flat hold sounds much longer
  than a decay) and the release param sets the decay time, so the release gold knob sets a drum's length. Sustain
  isn't used.
- AR: while the note is held, the envelope moves towards its level (the sustain param) at the attack rate when rising
  and the release rate when falling. Note-off releases to zero. With the level at maximum it's a plain attack-release
  envelope; automating the level or patching something to it makes the envelope a slew limiter for that modulation.
  Decay isn't used.
- MPE: like AR, plus an expression input chosen by Follow (pressure or mod wheel). The envelope heads for
  level + (1 - level) x input, so the level is where it rests with the input off. Decay isn't used.
