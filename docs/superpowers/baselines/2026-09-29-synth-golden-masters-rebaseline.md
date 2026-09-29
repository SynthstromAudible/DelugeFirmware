# Synth golden masters: C host to Embassy host

2026-09-29. The evidence behind recording `tests/integration/golden/presets.json` on the Embassy host. It replaces the
C-host `fixtures.json` that all 336 cases were recorded against. See `docs/superpowers/specs/2026-09-28-render-preset-design.md`
§7. Every case is explained below: as the same sound, or by a named cause shown by experiment.

## Method

Every render was kept, and each case was compared with `tests/integration/golden/compare_renders.py`. The tool aligns
the two renders on the first note's onset. It reports the RMS level difference, the correlation of the RMS envelopes
(512-frame windows), the difference in spectral centroid (power-weighted), and the best waveform correlation.

The references are the synth branch's own C host (`feat/synth-golden-masters`, 5ce28969c), built in a scratch worktree
and changed one variable at a time:

| Reference | Blocks | Render cost | Random stream | Note delivery |
|---|---|---|---|---|
| the C host as it recorded | 32 frames | measured on the frame clock | as boot left it | MIDI learn + MIDI follow |
| old code, matched | 128 frames | none, as on the Embassy host | restarted at the first note | MIDI follow |
| old code, own blocks | 32 frames | measured on the frame clock | restarted at the first note | MIDI follow |

Each case's timing tolerance is the largest change seen when only the new render's timing or randomness moves:
- the notes delayed by 3, 7, 16 and 29 blocks, and by 50, 100, 173 and 300 blocks for the cases still unexplained;
- the random stream restarted from four other seeds (`RENDER_RANDOM_SEED`).

A case is within tolerance if its level, envelope and centroid changes stay within 1.5x of that plus a small floor.

## Causes, and the experiment that showed each

1. **The old harness delivered every note twice.** run.py injected a MIDI learn on channel 1, and MIDI follow also
   sends channel 1 to the active instrument, so each note-on reached the Sound twice.
   - With a voice counter in the C host, one note-on started two voices at frame 4608, in every case tried.
   - Without the learn, one voice started.
   - In some presets the second voice was freed a block later. In others it lived for the whole note: two identical
     voices, twice the amplitude. 19 old records are at least 2 dB above a single delivery (the "old record doubled"
     column below), for example `factory/078 House` and `community/MPE Wood Pluck` at +6 dB.
   - The gate now strips every MIDI learn from the preset it stages, and plays through MIDI follow alone.
2. **The C host's 32-frame blocks swell each note's first cycle, up to 1.8x.** On the old code, `sine_basic` and
   `saw_basic` show the swell at 32-frame blocks and none at 128, where they match the new render. The peak-only
   differences in the "vs record" column come from this.
3. **The random stream stood wherever boot and loading had left it.** Random oscillator phases, noise and random
   modulation depend on how many random numbers were drawn before the note.
   - Restarting the stream at the first note on both hosts made `noise_only` match sample for sample.
   - It also changed 305 of the 336 old renders, so nearly every preset draws on the stream.
   - The gate keeps the restart.
4. **Free-running oscillators and LFOs.** Their phase at the note depends on how long the preset had been loaded, which
   differs between the harnesses. These cases change level, envelope or spectrum when only the new render's note is
   delayed. That's the "note timing / random draws" class.
5. **Old-code quirks at 128-frame blocks.**
   - The C host measures render cost on its frame clock, so a 128-frame block looks like 128 frames of CPU. That pins
     direness at 14 and falls back to degraded paths (`analog_osc` downgraded to the crude saw). The matched reference
     removes the cost.
   - Even then, the old code renders `wavetable/sync` and `morph_sync` silent at 128 frames, mishandles
     `sample/timestretch`, and fails `input_pitchshift`. Those cases are judged against its own 32-frame blocks.
6. **Wavetable morphing steps once per block.** Moving the old code alone from 32- to 64-frame blocks moves
   `wavetable/sync` by -1.4 dB and 12% in centroid, the same order as the old-to-new change.
7. **`next`'s reverb fixes, #4936 (inverted damping curve) and #4856 (the digital reverb's right channel read the left's
   filter state).** With both reverted in the new tree, 18 more cases fall within tolerance. Those are this class.

Found along the way and fixed with this re-record: when the blank boot song took in its first `SYNTHS` preset, the
preset lost the MIDI learn it was saved with. `replaceInstrument` handed it the default synth's empty input. The gate's
`RP-LEARN` check covers it.

## Totals

| Class | Cases |
|---|---|
| same as the old code (Δrms within 0.05 dB, envelope ≥ 0.999) | 147 |
| within the note-timing / random-draw tolerance | 150 |
| reverb fixes #4936 #4856 | 18 |
| same, judged at the old code's 32-frame blocks | 10 |
| wavetable morph steps per block | 7 |
| marginal: just outside timing tolerance | 4 |

The four marginal cases are presets whose sound depends heavily on when their notes land: `community/Echoy Melody`,
`community/Organic 1`, `factory/128 Lunar Landing` and `factory/137 Epic Saw Modulation Pad`. The widest delays nearly
cover them, and nothing about them points to a code change.

## Every case

- "vs record": against the C host's recorded render.
- "old record doubled": how much louder that record was than a single delivery, where the difference is 2 dB or more.
- "vs old code", "env" and "centroid": against the matched old-code reference, or its own 32-frame blocks for the
  wavetable, sample and input cases.

| case | vs record Δrms dB | old record doubled dB | vs old code Δrms dB | env | centroid % | timing tol Δrms / env | class |
|---|---|---|---|---|---|---|---|
| analog_osc | -1.04 |  | +0.00 | 1.000 | -0.0 | 1.67 / 1.000 | same |
| community/12 Metalliche | +0.20 |  | +0.14 | 0.998 | -0.6 | 0.13 / 0.998 | note timing / random draws |
| community/1994 Su | -0.25 |  | -0.00 | 1.000 | -0.0 | 0.45 / 0.837 | same |
| community/8 O'Clock | +5.53 |  | +0.27 | 0.986 | +1.5 | 1.66 / 0.832 | note timing / random draws |
| community/80's Ffunk | +0.00 |  | +0.00 | 1.000 | -0.2 | 0.00 / 1.000 | same |
| community/A Busted Sqb | +0.21 |  | -0.02 | 0.727 | -1.7 | 1.41 / 0.480 | note timing / random draws |
| community/A Fm Bass Funk | +0.03 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| community/A Fm Floot | +0.15 |  | +0.20 | 0.988 | +1.2 | 0.25 / 0.972 | note timing / random draws |
| community/A Fm Pad | +2.65 |  | +0.72 | 0.963 | -2.0 | 2.79 / 0.799 | note timing / random draws |
| community/A Ring Bass | +0.28 |  | +0.22 | 1.000 | +7.1 | 0.09 / 0.999 | reverb fixes #4936 #4856 |
| community/Aberration | +0.57 |  | -0.00 | 1.000 | -0.0 | 0.32 / 0.986 | same |
| community/Analohania | -0.37 |  | +0.01 | 1.000 | +0.4 | 0.76 / 0.732 | same |
| community/Apex Stab | +0.33 |  | +0.01 | 1.000 | +0.0 | 0.37 / 0.932 | same |
| community/BASS Simple Distorted | +1.09 |  | +0.74 | 0.976 | -2.9 | 0.52 / 0.980 | note timing / random draws |
| community/Backdraft Pad | -1.20 |  | +0.64 | 0.996 | +0.0 | 3.01 / 0.901 | note timing / random draws |
| community/Bad Zwirlz Su | +0.08 |  | +0.08 | 0.980 | +1.6 | 0.50 / 0.987 | note timing / random draws |
| community/Basedland | +0.30 |  | +0.21 | 0.957 | +0.6 | 0.11 / 0.978 | note timing / random draws |
| community/Basic Bass | -0.35 |  | -0.35 | 0.982 | +1.6 | 0.25 / 0.971 | note timing / random draws |
| community/BassGuitar | -0.00 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| community/Bell Fm Synthwave | -0.99 |  | -0.98 | 0.711 | +10.3 | 0.00 / 0.998 | reverb fixes #4936 #4856 |
| community/Bird | -3.42 | +3.14 | -0.00 | 1.000 | +0.0 | 3.38 / 0.966 | same |
| community/Blade Vang | +1.28 |  | -0.69 | 0.671 | -6.2 | 3.69 / 0.591 | note timing / random draws |
| community/Boomf | -2.45 | +2.89 | -0.02 | 0.991 | -6.5 | 0.82 / 0.958 | note timing / random draws |
| community/Brick Speedboat | +0.29 |  | +0.79 | 0.982 | +1.9 | 0.42 / 0.736 | reverb fixes #4936 #4856 |
| community/Broadcast | -5.21 | +6.02 | +0.78 | 0.488 | -10.5 | 0.47 / 0.534 | note timing / random draws |
| community/Brzcrtch | -0.03 |  | -0.00 | 1.000 | +0.0 | 0.08 / 0.955 | same |
| community/Celestial Su | -2.50 |  | -0.13 | 0.934 | +1.6 | 0.16 / 0.934 | note timing / random draws |
| community/Classic Pad | -0.63 |  | -0.67 | 0.947 | -8.8 | 0.41 / 0.925 | note timing / random draws |
| community/Cornwall | -4.12 | +3.59 | +0.00 | 1.000 | +0.2 | 22.73 / 0.329 | same |
| community/Crime Time | -0.04 |  | -0.15 | 0.996 | +1.8 | 0.34 / 0.978 | note timing / random draws |
| community/Crumbling Pad | -0.34 |  | -0.27 | 0.804 | +4.1 | 3.36 / 0.317 | note timing / random draws |
| community/D-303 | -0.01 |  | +0.00 | 1.000 | +0.0 | 0.01 / 0.970 | same |
| community/Desparate Synth | +0.24 |  | +0.11 | 0.851 | +8.5 | 0.68 / 0.877 | note timing / random draws |
| community/Distorted Bass | +0.39 |  | +0.01 | 1.000 | +0.4 | 0.61 / 0.994 | same |
| community/Drop | +0.04 |  | +0.05 | 1.000 | +4.1 | 0.01 / 1.000 | note timing / random draws |
| community/Dystopian Sky | +0.19 |  | -0.00 | 0.979 | -5.1 | 1.55 / 0.797 | note timing / random draws |
| community/Echoy Melody | +0.39 |  | +0.38 | 0.988 | +8.4 | 0.02 / 0.995 | marginal: just outside timing tolerance |
| community/Farvoice | +0.29 |  | +0.30 | 0.999 | +1.3 | 0.03 / 1.000 | reverb fixes #4936 #4856 |
| community/Faulty Circuit | -0.10 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| community/Fauxtar | -2.88 | +2.87 | -0.01 | 0.999 | -0.3 | 0.46 / 0.973 | same |
| community/Flea Circuits Su | -0.00 |  | -0.00 | 1.000 | +0.5 | 0.00 / 1.000 | same |
| community/Fm Hammer String | -3.53 | +3.57 | -0.00 | 1.000 | -0.0 | 0.49 / 0.997 | same |
| community/Fm Leadandbass | +1.07 |  | +0.04 | 1.000 | +0.2 | 2.85 / 0.686 | same |
| community/Fm Pads | +0.49 |  | +0.08 | 0.987 | +0.7 | 1.91 / 0.983 | note timing / random draws |
| community/Fm Strimgs | +0.02 |  | -0.17 | 0.956 | -3.0 | 0.95 / 0.937 | note timing / random draws |
| community/Frances2 | -0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| community/Fuzzring | -0.03 |  | +0.00 | 1.000 | +0.0 | 0.52 / 0.952 | same |
| community/Fw Landscape | +0.04 |  | +0.05 | 1.000 | +5.9 | 0.01 / 1.000 | reverb fixes #4936 #4856 |
| community/Giant Robot Fm | +0.04 |  | +0.04 | 0.969 | +1.2 | 0.58 / 0.965 | note timing / random draws |
| community/Glass Desert | +0.64 |  | +0.88 | 0.967 | +3.7 | 3.60 / 0.601 | note timing / random draws |
| community/Glass Pluck | -5.52 | +6.01 | +0.45 | 0.998 | -2.4 | 0.00 / 1.000 | reverb fixes #4936 #4856 |
| community/Glistening | +0.97 |  | -0.00 | 1.000 | -0.0 | 1.11 / 0.946 | same |
| community/Gong | -0.08 |  | -0.04 | 0.969 | -0.0 | 0.12 / 0.972 | note timing / random draws |
| community/Growler | +0.06 |  | +0.06 | 1.000 | +1.7 | 0.03 / 0.951 | note timing / random draws |
| community/Growlstrom | +0.03 |  | -0.00 | 1.000 | -0.0 | 0.06 / 0.990 | same |
| community/Heartspark | -0.04 |  | -0.00 | 1.000 | -0.0 | 0.09 / 0.978 | same |
| community/Hollow Dark | +0.67 |  | -0.03 | 0.997 | +0.5 | 1.17 / 0.935 | note timing / random draws |
| community/Hollowring | +1.70 |  | +0.80 | 0.944 | +0.7 | 1.23 / 0.873 | note timing / random draws |
| community/Infernal Ring | +0.07 |  | +0.03 | 1.000 | +0.5 | 0.50 / 0.906 | same |
| community/J6 Pad | -0.58 |  | -0.34 | 0.999 | +4.3 | 0.23 / 0.935 | note timing / random draws |
| community/Jehorn | +1.36 |  | +0.30 | 0.999 | +3.7 | 0.64 / 0.983 | note timing / random draws |
| community/Juno Style | -1.05 |  | +0.12 | 1.000 | -0.3 | 2.41 / 0.961 | note timing / random draws |
| community/Kaleidoscope | +0.03 |  | +0.31 | 0.964 | -3.7 | 0.57 / 0.890 | note timing / random draws |
| community/Livewire Arp | -0.24 |  | +0.21 | 1.000 | -1.4 | 0.58 / 0.951 | note timing / random draws |
| community/MPE 80'S Soundtrack | +0.64 |  | +0.62 | 0.959 | +43.1 | 0.62 / 0.956 | note timing / random draws |
| community/MPE Curvilinear | +0.15 |  | +0.24 | 0.998 | +2.8 | 0.47 / 0.964 | note timing / random draws |
| community/MPE Desert Strata | +0.82 |  | +0.12 | 0.994 | +17.2 | 0.33 / 0.782 | note timing / random draws |
| community/MPE FM Growler | +5.03 |  | +0.02 | 1.000 | +11.1 | 4.58 / 0.927 | note timing / random draws |
| community/MPE Flute | +0.12 |  | +0.05 | 0.955 | -0.1 | 2.27 / 0.970 | note timing / random draws |
| community/MPE Haunted Music Box | -5.54 | +6.02 | +0.47 | 0.851 | -0.3 | 0.30 / 0.861 | note timing / random draws |
| community/MPE Rich Saw Bass | +1.83 |  | +0.00 | 1.000 | +0.0 | 2.17 / 0.884 | same |
| community/MPE Super Saw | +0.00 |  | +0.39 | 0.997 | -2.3 | 0.21 / 0.680 | note timing / random draws |
| community/MPE Wood Pluck | -14.94 | +6.01 | +0.08 | 1.000 | +0.1 | 0.01 / 1.000 | note timing / random draws |
| community/Mean Pluck | -2.73 |  | -0.70 | 0.992 | -3.0 | 0.87 / 0.999 | note timing / random draws |
| community/Modello Uno | -0.10 |  | -0.07 | 0.986 | +4.1 | 0.18 / 0.993 | note timing / random draws |
| community/Mouse Tail | +0.07 |  | +0.06 | 0.997 | +6.3 | 0.01 / 0.999 | reverb fixes #4936 #4856 |
| community/Nilsish | +1.13 |  | +0.53 | 0.998 | -2.9 | 0.39 / 0.957 | note timing / random draws |
| community/Noisewobble | +0.56 |  | +0.80 | -0.092 | +2.5 | 0.21 / -0.188 | reverb fixes #4936 #4856 |
| community/Nowhere Home | +0.11 |  | -0.25 | 0.888 | +5.0 | 1.52 / 0.792 | note timing / random draws |
| community/Obassity | -0.05 |  | -0.05 | 0.993 | -0.9 | 0.05 / 0.995 | note timing / random draws |
| community/Oimp | -0.39 |  | +0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| community/Organic 1 | +1.85 |  | +0.53 | 0.765 | +0.5 | 0.81 / 0.904 | marginal: just outside timing tolerance |
| community/Orgavolution | -0.06 |  | -0.06 | 1.000 | +1.0 | 0.05 / 1.000 | note timing / random draws |
| community/Overloading | -0.49 |  | -0.48 | 0.937 | +15.4 | 0.61 / 0.941 | note timing / random draws |
| community/Pakmahn Su | -0.18 |  | -0.00 | 1.000 | -0.0 | 0.16 / 0.926 | same |
| community/Pianism I | +0.22 |  | +0.09 | 0.999 | +4.1 | 0.78 / 0.849 | note timing / random draws |
| community/Pomp | +0.01 |  | +0.02 | 1.000 | +1.0 | 0.01 / 1.000 | same |
| community/Pusher | -1.21 |  | +0.00 | 1.000 | +0.7 | 1.59 / 0.915 | same |
| community/Rainy Wasteland | +0.25 |  | -0.05 | 0.995 | +0.1 | 1.45 / 0.817 | note timing / random draws |
| community/Richy Rich | +1.73 |  | +0.27 | 0.995 | -1.4 | 1.13 / 0.542 | note timing / random draws |
| community/Right On | -0.58 |  | +0.00 | 0.999 | +0.6 | 0.22 / 0.835 | same |
| community/Ringmod Arp | +0.08 |  | +0.07 | 1.000 | +0.2 | 0.05 / 0.979 | note timing / random draws |
| community/Ringmod Melody | +0.02 |  | +0.08 | 1.000 | +0.3 | 0.05 / 0.999 | note timing / random draws |
| community/Ringmod Pad | +0.15 |  | +0.27 | 0.941 | -1.1 | 0.39 / 0.586 | note timing / random draws |
| community/Rubberdemon | +2.65 |  | +0.55 | 0.987 | -6.9 | 2.31 / 0.949 | note timing / random draws |
| community/Runner Lead | +0.45 |  | +0.34 | 0.839 | +9.8 | 0.58 / 0.804 | note timing / random draws |
| community/Sad And Hollow | +0.24 |  | +0.22 | 0.988 | +0.2 | 0.02 / 0.992 | reverb fixes #4936 #4856 |
| community/Saturnine | -2.61 | +3.09 | +0.46 | 0.997 | +5.1 | 0.00 / 1.000 | reverb fixes #4936 #4856 |
| community/Sawborg | +0.13 |  | -0.32 | 0.998 | +4.0 | 0.49 / 0.979 | note timing / random draws |
| community/Schizophonyx Rm | +0.01 |  | -0.03 | 0.932 | +1.3 | 0.17 / 0.973 | note timing / random draws |
| community/Sense Of Doom | -1.28 |  | +0.11 | 0.934 | +17.7 | 3.36 / 0.307 | note timing / random draws |
| community/Sik Rm | -0.00 |  | +0.00 | 1.000 | -0.0 | 0.03 / 0.999 | same |
| community/Slogro | +0.20 |  | -0.53 | 0.953 | +1.0 | 2.35 / 0.367 | note timing / random draws |
| community/Slow Hula Hoop | -1.16 |  | +0.26 | 0.997 | +2.7 | 4.19 / 0.616 | note timing / random draws |
| community/Space Bass | +0.13 |  | +0.10 | 0.995 | -0.0 | 0.04 / 0.998 | note timing / random draws |
| community/Speed Lead | +2.29 |  | -0.27 | 0.999 | +1.5 | 4.60 / 0.758 | note timing / random draws |
| community/Spilled Soup | +0.01 |  | +0.01 | 1.000 | +0.2 | 0.00 / 1.000 | same |
| community/Super Square | +0.78 |  | +0.81 | 0.844 | -13.2 | 0.79 / 0.832 | note timing / random draws |
| community/Sync Squared | -0.01 |  | -0.00 | 1.000 | -0.0 | 0.01 / 0.993 | same |
| community/Synthetic 1 | +1.16 |  | +0.31 | 0.997 | -1.0 | 1.48 / 0.910 | note timing / random draws |
| community/Tension | -1.45 |  | -0.85 | 0.988 | -6.9 | 0.00 / 1.000 | reverb fixes #4936 #4856 |
| community/Thirsty Machine | +0.22 |  | -0.04 | 1.000 | +1.6 | 0.27 / 0.961 | note timing / random draws |
| community/Threat | +0.11 |  | +0.00 | 1.000 | -0.0 | 0.35 / 0.999 | same |
| community/Throaty | -3.33 | +3.04 | -0.32 | 1.000 | -5.0 | 0.21 / 1.000 | note timing / random draws |
| community/Thunder | -1.87 |  | -0.00 | 1.000 | -0.0 | 1.96 / 0.478 | same |
| community/Trust Me It's Useful Noise | -3.61 |  | -2.60 | 0.923 | -46.7 | 5.78 / 0.771 | note timing / random draws |
| community/Ttty | +0.04 |  | +0.08 | 0.830 | -1.6 | 0.45 / 0.835 | note timing / random draws |
| community/UK Dub Siren | +0.43 |  | +0.12 | 0.995 | -1.4 | 0.04 / 0.998 | note timing / random draws |
| community/Undulating Drone | +2.28 |  | +0.96 | 0.959 | -2.8 | 1.39 / 0.735 | note timing / random draws |
| community/Wall Of Static | +0.45 |  | -0.04 | 0.901 | +4.1 | 0.34 / 0.781 | note timing / random draws |
| community/Warble Harp Lead | +0.22 |  | -0.00 | 1.000 | -0.0 | 1.55 / 0.973 | same |
| community/Warm DX7 Pad | +0.55 |  | +0.23 | 0.983 | -2.2 | 0.93 / 0.889 | note timing / random draws |
| community/Wavedash | -0.03 |  | +0.01 | 1.000 | +2.0 | 0.00 / 1.000 | note timing / random draws |
| community/Weather | +2.88 |  | +0.85 | 0.954 | -23.3 | 3.82 / 0.423 | note timing / random draws |
| community/Wind Whistle | +0.25 |  | +0.54 | 0.995 | +0.2 | 0.59 / 0.906 | note timing / random draws |
| community/Woodeep | -1.21 |  | -1.01 | 0.908 | +9.9 | 1.07 / 0.915 | note timing / random draws |
| community/Working Engine | -6.53 | +6.76 | +0.26 | 0.876 | +3.9 | 4.99 / 0.563 | note timing / random draws |
| community/Worrisome | -2.46 | +2.48 | -0.00 | 1.000 | +0.0 | 2.56 / 0.907 | same |
| community/Wub Sync Bass | -0.01 |  | +0.02 | 1.000 | -0.1 | 0.01 / 1.000 | same |
| env_shape | +0.00 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/000 Rich Saw Bass | +1.83 |  | +0.00 | 1.000 | +0.0 | 2.18 / 0.885 | same |
| factory/001 Sync Bass | +1.91 |  | -0.14 | 0.995 | -1.9 | 4.35 / 0.962 | note timing / random draws |
| factory/002 Basic Square Bass | -0.00 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/003 Synthwave Bass | +2.09 |  | +0.02 | 1.000 | +3.0 | 5.86 / 0.954 | note timing / random draws |
| factory/004 Dubby Bass | +0.09 |  | +0.09 | 1.000 | -0.1 | 0.05 / 1.000 | note timing / random draws |
| factory/005 Sweet Mono Bass | +0.74 |  | +0.00 | 1.000 | -0.0 | 2.08 / 0.792 | same |
| factory/006 Vaporwave Bass | +3.58 |  | -0.00 | 1.000 | +0.0 | 2.72 / 0.798 | same |
| factory/007 Detuned Saw Bass | -0.35 |  | -0.00 | 0.999 | -0.1 | 1.38 / 0.891 | note timing / random draws |
| factory/008 FM Rich Distorted Bass | -0.89 |  | +0.00 | 1.000 | +0.0 | 4.73 / 0.865 | same |
| factory/009 Hoover Bass | +0.08 |  | -0.00 | 1.000 | -0.0 | 3.35 / 0.858 | same |
| factory/010 Gravel Basscamp | -0.00 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/011 Dubstep Bass | -0.01 |  | -0.01 | 1.000 | +0.4 | 0.01 / 0.991 | same |
| factory/012 Blunt Sync Bass | +0.01 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/013 Trap Bass 1 | -0.01 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/014 Trap Bass 2 | -0.00 |  | -0.00 | 1.000 | +0.0 | 0.01 / 0.997 | same |
| factory/015 Resonant Filter Bass | +0.03 |  | +0.00 | 1.000 | -0.0 | 0.03 / 0.999 | same |
| factory/016 Dark Saturated Bass | +0.52 |  | +0.00 | 0.978 | +0.1 | 1.10 / 0.789 | note timing / random draws |
| factory/017 Impact Saw Lead | +0.47 |  | +0.00 | 1.000 | +0.0 | 1.41 / 0.883 | same |
| factory/018 Rich Saw Lead | +0.08 |  | -0.01 | 1.000 | +0.2 | 0.22 / 0.818 | same |
| factory/019 Fizzy Strings | +0.02 |  | +0.01 | 0.995 | +0.9 | 0.10 / 0.992 | note timing / random draws |
| factory/020 Soft Saw Lead | -0.00 |  | -0.00 | 1.000 | +0.1 | 0.00 / 1.000 | same |
| factory/021 80s TV Lead | +0.01 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/022 Rich Filter LFO Lead | +1.69 |  | -0.00 | 1.000 | -0.3 | 1.10 / 0.732 | same |
| factory/023 Analog Mono Wow | +2.17 |  | -0.14 | 0.980 | +0.8 | 4.20 / 0.751 | note timing / random draws |
| factory/024 Warble Bass Pluck | -0.30 |  | -0.16 | 0.962 | +2.5 | 0.22 / 0.970 | note timing / random draws |
| factory/025 Soft Synth Organ | +0.08 |  | -0.01 | 1.000 | +0.1 | 0.09 / 0.997 | same |
| factory/026 PW Organ | -0.00 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/027 PW Envelope | -0.52 |  | +0.00 | 1.000 | +0.6 | 0.00 / 1.000 | same |
| factory/028 PWM | +0.00 |  | -0.00 | 1.000 | +4.8 | 0.00 / 1.000 | note timing / random draws |
| factory/029 Chiptune Trill | -0.68 |  | +0.00 | 1.000 | +0.0 | 0.03 / 0.998 | same |
| factory/030 Distant Porta | -0.01 |  | +0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/031 Nasal Choir | -0.07 |  | -0.00 | 1.000 | +0.3 | 0.12 / 0.998 | same |
| factory/032 Bandpass Choir | +0.00 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/033 Rich Square | -0.07 |  | +0.00 | 1.000 | -0.0 | 0.60 / 0.951 | same |
| factory/034 Square Choir | +0.02 |  | +0.02 | 0.974 | +2.1 | 0.06 / 0.984 | note timing / random draws |
| factory/035 Bell Lead & Bass | +2.07 |  | -0.00 | 1.000 | +0.0 | 2.03 / 0.654 | same |
| factory/036 Analog Ambient Square | -0.32 |  | +0.00 | 0.997 | +0.7 | 0.05 / 0.999 | note timing / random draws |
| factory/037 Echo Chord | +0.43 |  | +0.00 | 1.000 | +0.0 | 1.00 / 0.848 | same |
| factory/038 Vapor Arp | -2.46 | +2.05 | -0.36 | 0.815 | +1.6 | 0.53 / 0.732 | note timing / random draws |
| factory/039 Detuned Retriggering Saws | +0.00 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/040 Spacer Leader | -0.18 |  | -0.17 | 0.206 | +0.5 | 0.16 / 0.219 | note timing / random draws |
| factory/041 Zithar - Vibed | -5.37 | +4.98 | -0.39 | 0.640 | +0.9 | 5.85 / 0.154 | note timing / random draws |
| factory/042 High Triangle | -0.00 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/043 Square Porta | +0.00 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/044 8-Bit Lead | +0.00 |  | -0.00 | 1.000 | -3.0 | 0.00 / 1.000 | note timing / random draws |
| factory/045 Square Sync | -0.12 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/046 Saw Sync | -0.20 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/047 Basic Dirty Bass | +0.00 |  | +0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/048 Thin Pulse Bass | -0.13 |  | +0.00 | 0.999 | -0.2 | 0.30 / 0.964 | note timing / random draws |
| factory/049 Basic FM | -0.02 |  | +0.00 | 1.000 | -0.0 | 0.14 / 0.998 | same |
| factory/050 FM Basic Bass | +0.02 |  | +0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/051 FM Rich Brass | +1.21 |  | -0.00 | 0.998 | +0.2 | 2.01 / 0.957 | note timing / random draws |
| factory/052 Soft FM | -0.54 |  | +0.00 | 1.000 | -0.0 | 1.21 / 0.700 | same |
| factory/053 Detuned FM Horns | +0.18 |  | +0.47 | 0.995 | +8.3 | 0.30 / 0.893 | note timing / random draws |
| factory/054 Ghostly Sines | +0.01 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/055 FM Theremin | +0.02 |  | +0.00 | 1.000 | +0.0 | 0.04 / 0.949 | same |
| factory/056 FM Bell Modulation | +0.73 |  | -0.03 | 0.995 | -2.0 | 0.81 / -0.222 | note timing / random draws |
| factory/057 FM Lead | +0.01 |  | +0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/058 FM Rising Attack | +0.01 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/059 Distorted Lead Guitar | +0.01 |  | +0.00 | 1.000 | -0.1 | 0.00 / 1.000 | same |
| factory/060 Bass Guitar | +0.00 |  | -0.02 | 1.000 | -0.6 | 0.04 / 0.999 | same |
| factory/061 Blown-Staccato-Panpipes | +0.32 |  | +0.29 | 0.998 | -1.7 | 0.04 / 0.998 | reverb fixes #4936 #4856 |
| factory/062 Trumpet | +0.00 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/063 Tuba | +2.03 |  | -0.00 | 1.000 | +0.0 | 2.23 / 0.917 | same |
| factory/064 Reeds-Flute-Oboe | +0.65 |  | +0.65 | 0.942 | +4.7 | 0.12 / 0.998 | reverb fixes #4936 #4856 |
| factory/065 Cello | -0.03 |  | -0.00 | 0.999 | +0.0 | 0.03 / 0.999 | same |
| factory/066 Violin | +0.00 |  | +0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/067 Marimba | +0.05 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/068 FM Bells 1 | +0.01 |  | -0.00 | 1.000 | +0.2 | 0.00 / 1.000 | same |
| factory/069 FM Bells 2 | +0.01 |  | -0.00 | 1.000 | +0.1 | 0.00 / 1.000 | same |
| factory/070 Glockenspiel | +0.00 |  | -0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/071 Rhodes | +1.11 |  | +0.00 | 1.000 | -0.0 | 3.23 / 0.916 | same |
| factory/072 Kyoto Phono | +0.06 |  | +0.04 | 1.000 | -0.2 | 0.00 / 1.000 | same |
| factory/073 Piano | -0.71 |  | -0.00 | 1.000 | +0.0 | 3.70 / 0.971 | same |
| factory/074 Electric Piano | +0.18 |  | -0.00 | 1.000 | -0.0 | 0.47 / 0.910 | same |
| factory/075 Electric Piano With Strings | +0.09 |  | +0.36 | 0.793 | -1.7 | 1.49 / 0.749 | note timing / random draws |
| factory/076 Organ | +0.02 |  | -0.00 | 1.000 | -0.0 | 0.29 / 0.870 | same |
| factory/077 FM Perc-Organ | -1.00 |  | -0.64 | 0.749 | -0.5 | 0.79 / 0.581 | note timing / random draws |
| factory/078 House | -5.96 | +6.01 | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/079 Phased Arper | +4.68 |  | +0.38 | 0.907 | -2.1 | 3.54 / 0.789 | note timing / random draws |
| factory/080 House | +0.04 |  | +0.00 | 1.000 | +0.0 | 0.01 / 1.000 | same |
| factory/081 Xylophone Big Bass | -0.81 |  | +0.00 | 1.000 | -0.0 | 2.19 / 0.871 | same |
| factory/082 Short Sharp Delay | -1.02 |  | -0.04 | 1.000 | +0.9 | 2.54 / 0.965 | same |
| factory/083 Dark Chorus | -0.01 |  | -0.04 | 0.995 | -0.2 | 0.23 / 0.967 | note timing / random draws |
| factory/084 FM Narrow Band | -0.10 |  | +0.00 | 1.000 | -0.0 | 0.40 / 0.989 | same |
| factory/085 Deep Fizz | +0.00 |  | +0.00 | 1.000 | -0.0 | 0.01 / 0.989 | same |
| factory/086 Techno Organ | +0.28 |  | -0.02 | 0.990 | +0.5 | 0.39 / 0.955 | note timing / random draws |
| factory/087 Define Leader | -0.03 |  | -0.01 | 0.950 | -0.2 | 0.03 / 0.971 | note timing / random draws |
| factory/088 Yelp Chords | -0.02 | +2.51 | -0.00 | 0.996 | +6.4 | 2.30 / 0.942 | note timing / random draws |
| factory/089 Degraded Retro Lead | -0.02 |  | -0.02 | 0.975 | +0.4 | 0.03 / 0.981 | note timing / random draws |
| factory/090 FM Organ | +0.00 |  | -0.00 | 1.000 | +0.0 | 0.01 / 1.000 | same |
| factory/091 FM Ricochet | +0.34 |  | -0.00 | 0.998 | +0.4 | 0.78 / 0.957 | note timing / random draws |
| factory/092 Degraded Tremolo | +1.40 |  | -0.07 | 0.996 | +0.0 | 2.12 / 0.701 | note timing / random draws |
| factory/093 FM Distorted Bells | +0.02 |  | -0.00 | 1.000 | +0.0 | 0.02 / 0.999 | same |
| factory/094 Ambient Occlusion Lead | +0.58 |  | +0.55 | 0.891 | -0.3 | 0.22 / 0.814 | reverb fixes #4936 #4856 |
| factory/095 Harsh FM Feedback | -0.19 |  | +0.00 | 1.000 | +0.0 | 0.32 / 0.977 | same |
| factory/096 FM Guitar Power Chord | +0.00 |  | +0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/097 Saturated Filter | -0.00 |  | +0.00 | 1.000 | -0.0 | 0.06 / 0.999 | same |
| factory/098 Saturated Sync | -0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/099 Overdrive Reese Sync | +0.07 |  | -0.08 | 0.953 | -0.0 | 0.36 / 0.915 | note timing / random draws |
| factory/100 Noise Lead | -0.09 |  | +0.00 | 1.000 | +0.0 | 0.13 / 0.999 | same |
| factory/101 Atebit | +0.01 |  | +0.01 | 0.995 | -0.2 | 0.02 / 0.991 | note timing / random draws |
| factory/102 Harsh 5th | -0.52 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| factory/103 Sci-fi Chaos | -1.71 |  | +0.09 | 1.000 | -0.8 | 0.00 / 1.000 | note timing / random draws |
| factory/104 Alien Vomit | +0.01 |  | +0.00 | 1.000 | +0.0 | 0.01 / 0.997 | same |
| factory/105 Attack Bass | -0.11 |  | -0.14 | 0.988 | +7.9 | 0.35 / 0.969 | note timing / random draws |
| factory/106 Hang Drum | +0.62 |  | +0.55 | 0.917 | +4.7 | 1.45 / 0.884 | note timing / random draws |
| factory/107 FM LPG Percussion | -16.23 | +17.41 | +0.22 | 0.999 | -0.1 | 7.79 / 0.600 | note timing / random draws |
| factory/108 Robo Arp | +0.29 |  | -0.00 | 1.000 | +0.0 | 1.69 / 0.835 | same |
| factory/109 Talking Arp | -0.67 |  | -1.06 | 0.901 | +23.4 | 1.32 / 0.911 | note timing / random draws |
| factory/110 Crystalline Ringmod | +0.27 |  | +0.25 | 0.998 | +11.2 | 0.16 / 0.999 | note timing / random draws |
| factory/111 Satellite Drum | +0.27 |  | +0.56 | 0.941 | -4.9 | 0.90 / 0.925 | note timing / random draws |
| factory/112 Hard Tech Beat | +0.15 |  | +0.05 | 0.998 | +0.8 | 0.08 / 0.993 | note timing / random draws |
| factory/113 Bio Lab | -1.67 |  | -2.24 | 0.827 | +78.9 | 4.20 / 0.683 | note timing / random draws |
| factory/114 Sootheerio | +0.02 |  | +0.03 | 0.993 | +0.1 | 0.01 / 0.951 | note timing / random draws |
| factory/115 Sounds After Take-off | -0.56 |  | -0.69 | 0.966 | +0.5 | 2.24 / 0.757 | note timing / random draws |
| factory/116 Evolving Frequencies | -1.69 |  | -0.02 | 0.582 | +1.1 | 0.81 / 0.581 | note timing / random draws |
| factory/117 Belledy | -0.01 |  | +0.00 | 0.952 | -2.2 | 0.17 / 0.968 | note timing / random draws |
| factory/118 Small Bridge Pad | +0.02 |  | +0.01 | 1.000 | +0.0 | 0.01 / 1.000 | same |
| factory/119 Stars Of The Bin Pad | +1.02 |  | +1.03 | 0.975 | -1.6 | 0.01 / 0.997 | reverb fixes #4936 #4856 |
| factory/120 High Harsh Pad | +0.12 |  | -0.11 | 0.989 | -0.2 | 0.31 / 0.857 | note timing / random draws |
| factory/121 Tiny Lights | -0.01 |  | +0.61 | 0.992 | -10.1 | 0.33 / 0.847 | reverb fixes #4936 #4856 |
| factory/122 Majestic Synth Orchestra | -0.27 |  | +0.02 | 0.947 | +0.1 | 0.50 / 0.718 | note timing / random draws |
| factory/123 Space Dust | +0.78 |  | +0.47 | 0.871 | -6.4 | 1.79 / 0.899 | note timing / random draws |
| factory/124 Filter Modulation Pad | -0.25 |  | +0.00 | 1.000 | +0.0 | 0.05 / 0.999 | same |
| factory/125 Evolving Pad | +0.04 |  | +0.04 | 0.959 | +1.9 | 0.04 / 0.977 | note timing / random draws |
| factory/126 Dark FM Pad | +0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/127 Alien Larvae | -0.35 |  | -0.35 | 0.807 | +3.6 | 0.34 / 0.816 | note timing / random draws |
| factory/128 Lunar Landing | +1.63 |  | +0.28 | 0.970 | +0.0 | 0.10 / 0.814 | marginal: just outside timing tolerance |
| factory/129 Sci-fi Scenic | +0.43 |  | +0.65 | 0.845 | -8.3 | 1.32 / 0.736 | note timing / random draws |
| factory/130 Dark Strings | +0.23 |  | +0.07 | 0.997 | +1.0 | 2.30 / 0.643 | note timing / random draws |
| factory/131 Warm Strings | +0.19 |  | +0.20 | 0.985 | +0.9 | 0.15 / 0.990 | note timing / random draws |
| factory/132 Organ Strings | +0.00 |  | -0.00 | 1.000 | +0.0 | 0.02 / 1.000 | same |
| factory/133 80s Strings | -0.43 |  | -0.07 | 0.627 | -13.3 | 0.48 / 0.232 | reverb fixes #4936 #4856 |
| factory/134 Melody String | +0.84 |  | +0.00 | 1.000 | -0.0 | 1.38 / 0.587 | same |
| factory/135 Soothing Growth Pad | -0.08 |  | +0.11 | 0.919 | +3.7 | 5.67 / 0.388 | note timing / random draws |
| factory/136 Synthwave Pad | -0.02 |  | -0.03 | 0.505 | +3.3 | 1.30 / 0.520 | note timing / random draws |
| factory/137 Epic Saw Modulation Pad | +1.90 |  | +1.78 | 0.552 | -18.2 | 0.91 / 0.485 | marginal: just outside timing tolerance |
| factory/138 Brassy Pad | +0.82 |  | +0.00 | 1.000 | -0.0 | 1.40 / 0.815 | same |
| factory/139 Detuned Saw Pad | +1.35 |  | +0.00 | 1.000 | +0.0 | 2.26 / 0.948 | same |
| factory/140 Slow Aural Swells | -0.02 |  | +0.11 | 0.796 | -1.5 | 1.23 / 0.764 | note timing / random draws |
| factory/141 Ringmod Pad | -0.10 |  | -0.00 | 1.000 | -0.0 | 0.32 / 0.998 | same |
| factory/142 Phaser | +0.18 |  | +0.26 | 0.999 | -4.1 | 0.15 / 1.000 | note timing / random draws |
| factory/143 Chillout Pad | -0.02 |  | -0.01 | 0.947 | -0.3 | 0.07 / 0.962 | note timing / random draws |
| factory/144 Sweep Chords | +0.57 |  | +0.09 | 0.986 | +5.9 | 0.73 / 0.838 | note timing / random draws |
| factory/145 Eerie High Pad | +0.01 |  | +0.00 | 1.000 | +0.1 | 0.00 / 1.000 | same |
| factory/146 Atmospheric Squares Pad | +0.53 |  | +0.24 | 0.991 | -0.2 | 0.26 / 0.993 | note timing / random draws |
| factory/147 Resonant Filter Pad | +0.13 |  | +0.03 | 1.000 | +0.4 | 0.32 / 0.623 | same |
| factory/148 Warm 5th Pad | -0.24 |  | -0.22 | 0.933 | +7.4 | 2.98 / 0.763 | note timing / random draws |
| factory/149 Cold 5th Pad | +0.81 |  | +0.03 | 0.926 | +2.2 | 0.78 / 0.905 | note timing / random draws |
| factory/150 Vaporwave Pad | -0.27 |  | -0.24 | 0.944 | +5.2 | 0.15 / 0.962 | note timing / random draws |
| factory/151 Radiant FM Pad | -0.18 |  | -0.00 | 1.000 | -0.0 | 0.24 / 0.943 | same |
| factory/152 Small Jet Pad | +0.66 |  | +0.64 | 0.999 | -1.7 | 0.00 / 1.000 | reverb fixes #4936 #4856 |
| factory/153 FM Modulation Pad | +0.05 |  | +0.06 | 0.970 | -1.8 | 0.39 / 0.961 | note timing / random draws |
| factory/154 Rich FM Pad 1 | -0.30 |  | -0.00 | 1.000 | -0.0 | 0.15 / 0.725 | same |
| factory/155 Rich FM Pad 2 | -0.90 |  | -0.00 | 0.999 | +0.1 | 0.90 / 0.938 | same |
| factory/156 Rich FM Pad 3 | -0.04 |  | -0.02 | 0.970 | -0.2 | 0.06 / 0.990 | note timing / random draws |
| factory/157 Rich FM Pad 4 | +0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| factory/158 Tempo-Synced LFO | -2.82 | +2.93 | +0.10 | 0.880 | +1.7 | 2.38 / 0.827 | note timing / random draws |
| factory/159 80s Bass Rhythm | +0.11 |  | +0.18 | 0.996 | +3.4 | 0.40 / 0.977 | note timing / random draws |
| factory/160 Synthwave Bass Arp | +3.24 |  | -0.00 | 1.000 | -0.0 | 2.68 / 0.716 | same |
| factory/161 Synthwave Vibrato Arp | +0.24 |  | -0.15 | 0.970 | +0.0 | 0.99 / 0.845 | note timing / random draws |
| factory/162 Busy Arp | +0.96 |  | -0.72 | 0.842 | -3.9 | 2.37 / 0.394 | note timing / random draws |
| factory/163 Crisp Pop Arp | +0.42 |  | -0.02 | 0.997 | -0.4 | 0.03 / 0.995 | note timing / random draws |
| factory/164 Study Arp | +1.13 |  | -0.22 | 0.898 | -4.2 | 0.28 / 0.903 | note timing / random draws |
| factory/165 Acid Arp | -0.03 |  | -0.01 | 0.988 | -0.4 | 1.07 / 0.883 | note timing / random draws |
| factory/166 Harpsichord Cyborg | +0.04 |  | +0.04 | 0.997 | -1.1 | 0.91 / 0.960 | note timing / random draws |
| factory/167 FM Metallic Bass Arp | -0.70 |  | +0.04 | 1.000 | -0.1 | 0.00 / 1.000 | same |
| fm_2op | +0.36 |  | -0.00 | 1.000 | -0.0 | 0.33 / 1.000 | same |
| fm_feedback | +0.56 |  | +0.00 | 1.000 | -0.0 | 0.66 / 1.000 | same |
| input_mono | +0.00 |  | +0.00 | 1.000 | +0.0 | n/a | same (at 32 frames) |
| input_pitchshift | +0.00 |  | +0.00 | 1.000 | +0.0 | n/a | same (at 32 frames) |
| input_stereo | +0.00 |  | +0.00 | 1.000 | -0.0 | n/a | same (at 32 frames) |
| lfo_vibrato | -0.00 |  | +0.00 | 1.000 | +0.3 | 0.00 / 1.000 | same |
| lpf_24db_env2 | -0.02 |  | +0.00 | 1.000 | -0.0 | 0.10 / 0.998 | same |
| mono_legato | -0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| noise_only | -0.02 |  | -0.00 | 1.000 | -0.0 | 0.06 / 0.998 | same |
| pitch_bend | -0.01 |  | -0.00 | 1.000 | +0.0 | 0.00 / 0.999 | same |
| poly_chord | -0.01 |  | -0.00 | 1.000 | -0.0 | 0.01 / 0.962 | same |
| porta | -0.02 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| ringmod | +0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| sample/linear | -0.00 |  | -0.00 | 1.000 | +0.1 | n/a | same (at 32 frames) |
| sample/mono | -0.00 |  | -0.00 | 1.000 | +0.0 | n/a | same (at 32 frames) |
| sample/reversed | -0.00 |  | -0.00 | 1.000 | +0.1 | n/a | same (at 32 frames) |
| sample/smooth_sweep | -0.08 |  | -0.03 | 0.987 | -0.1 | n/a | wavetable morph steps per block |
| sample/timestretch | +3.76 |  | -0.04 | 1.000 | -4.3 | n/a | same (at 32 frames) |
| saw_basic | -0.00 |  | +0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| sine_basic | +0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| square_basic | +0.00 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| square_pw | +0.00 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| square_pw_low | -0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| svf_band | -0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| triangle_basic | +0.00 |  | -0.00 | 1.000 | +0.0 | 0.00 / 1.000 | same |
| two_osc_detune | +0.00 |  | +0.00 | 1.000 | -0.0 | 0.00 / 0.999 | same |
| unison_spread | +0.12 |  | -0.00 | 1.000 | -0.0 | 0.49 / 0.986 | same |
| velocity_dynamics | -0.01 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| wavefold_sine | +0.00 |  | +0.00 | 1.000 | -0.0 | 0.00 / 1.000 | same |
| wavetable/morph_lfo | +0.18 |  | +0.17 | 0.983 | -1.0 | n/a | wavetable morph steps per block |
| wavetable/morph_note | -0.41 |  | +1.19 | 0.981 | -19.8 | n/a | wavetable morph steps per block |
| wavetable/morph_sync | +1.35 |  | +1.58 | 0.575 | -26.5 | n/a | wavetable morph steps per block |
| wavetable/scw | +0.28 |  | +0.28 | 0.969 | +1.0 | n/a | wavetable morph steps per block |
| wavetable/scw_hi | -0.02 |  | +0.07 | 0.985 | +5.3 | n/a | wavetable morph steps per block |
| wavetable/static | -1.52 |  | +0.14 | 1.000 | -1.4 | n/a | same (at 32 frames) |
| wavetable/static_lo | -1.11 |  | +0.12 | 1.000 | -1.8 | n/a | same (at 32 frames) |
| wavetable/sync | +1.33 |  | +1.60 | 0.666 | -28.9 | n/a | wavetable morph steps per block |
| wavetable_basic | +0.00 |  | +0.00 | 1.000 | +0.0 | n/a | same (at 32 frames) |
