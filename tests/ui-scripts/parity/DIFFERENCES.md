# Parity differences

`run_parity.py` against these scripts, on the factory card. Every difference is fixed or accepted here.

## feat/param-identity against next d70387530

2412 of 2412 snapshots identical. The branch's three fixes show no difference with the default MIDI-follow mappings,
so their tests are param_target_check's legs:

- A MIDI clip's automation overview labels a sound shortcut the sound map leaves unmapped with no CC, where it read
  past the global map (a patched param) or showed another param's CC (portamento showed mod FX rate's).
- MIDI follow's EQ feedback goes out on the CCs that control the EQ (86, 87, 84, 85), not the filters' (71, 82, 74, 81).

Accepted, Kate 2026-10-31.

## next d70387530 against main 517fb3e3a

1444 of 2412 identical: automation_kit_song 157, automation_synth 79, cable_menus 9, learn 3, midi_follow 705,
mod_knobs 15 differ. These are next's, not a branch's; not yet sorted or accepted.

## What the runner tolerates

- Scrolling text and blinking LEDs: each snapshot is read over a 1 s window on both images; a match is a shared state.
- `random_colour X Y`: a pad drawn in its row's random colour is compared lit or unlit (D11, accepted 2026-10-24).
