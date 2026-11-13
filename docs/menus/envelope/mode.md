How the envelope behaves.

- ADSR: attack, decay, sustain and release, as always.
- AHD: attack, hold at full level, then decay to zero. It's one-shot and ignores note offs.
- AR: while the note is held, the envelope moves towards its level (the sustain param) at the attack rate when rising
  and the release rate when falling. Note-off releases to zero. With the level at maximum it's a plain attack-release
  envelope; automating the level or patching something to it makes the envelope a slew limiter.
- MPE: like AR, plus an expression input chosen by Follow (pressure or mod wheel). The envelope heads for
  level + (1 - level) x input, kept between zero and full, so the level is where it rests with the input off.
