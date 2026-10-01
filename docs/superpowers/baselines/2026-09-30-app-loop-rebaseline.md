# Golden masters: the audio tier and the app loop

2026-09-30. The evidence behind re-recording `tests/integration/golden/presets.json` (golden-synth, 336 cases) and the
`RS_STREAM` record in `tests/integration/golden/songs.json` (render-song) on `fix/scheduler-correctness`. This branch's
scheduler change is the audio tier driving the render (74c53a070), the app's tasks running as jobs in the app loop
(f121a0235, 3d734c318), and the task table's deletion (d86b7cb79, 9fb316d8e). Every case is explained below by one
cause, which an experiment showed byte for byte.

## The two commits compared

- **Old**: c4d0d1fa6, where this branch starts. Its records are identical to `next`'s 5707693a4 (no diff under
  `tests/integration/golden/`). It was built in a scratch worktree (`.superpowers/app-loop-old`). It passes both
  gates against the committed records: all 336 kept preset renders hash to their records, and RS_STREAM renders
  `1a2ad432…`.
- **New**: 9fb316d8e, this branch's head.

Renders were kept with `RENDER_PRESET_KEEP` (presets) and a scratch-only copy of render-song's output (songs). Both are
under `.superpowers/renders/apploop_old` and `.superpowers/renders/apploop_new`.

## Method

1. Each case was compared with `tests/integration/golden/compare_renders.py`, against the per-case timing tolerance
   that the 2026-09-29 rebaseline measured (`.superpowers/renders/tolerance.json`, built by `tolerance.py`: the largest
   change seen when only the notes' timing or the random seed moves). The rule is `tolerance.py`'s: Δrms within 1.5x
   the tolerance + 0.1 dB, envelope correlation at least min(tol − 0.05, 0.99), and centroid within 1.5x + 3 %.
   The script also checks that both sides have the same length, that nothing went silent, and that the old renders
   match the old records (`classify.py`).
2. The cause, from the output dither. The master dither has its own LCG (`outputDither()` in `audio_engine.cpp`),
   advanced twice for every frame the output stage drains since boot. Before the first note, a render is pure dither
   (0 or −256), so its first 64 frames give its position in that stream: how many frames the audio tier rendered
   before the capture began (`ditherpos.c`).
3. The proof: the old code, with each case's note script delayed by those extra blocks (`old_shift_sweep.py`,
   `shift_identity.py`), was compared byte for byte with the new render, from the frame where the new capture began.

## Results

| | Cases |
|---|---|
| same (byte-identical) | 0 |
| within the timing tolerance | 336 |
| beyond the timing tolerance | 0 |

- No case changed length, and none went silent. Every record keeps its `frames`. Only `sha256`, `peak` and `rms_db`
  moved.
- The worst changes are |Δrms| 1.465 dB and |Δpeak| 1.600 dB. The lowest envelope correlation is 0.6545
  (`community/A Busted Sqb`, whose own tolerance is 0.480), and the largest |Δcentroid| is 37.1 %, all inside each
  case's own tolerance.
- 136 cases differ by at most one output LSB (raw ±256), so only their dither moved.
- render-song (RS_STREAM): Δrms 0.00 dB, Δpeak 0.00 dB, envelope 1.000, wave 1.000. The largest sample difference is
  one output LSB (raw 256), on 31 % of samples. Frames, peak (142067968) and rms_db (−31.24) are unchanged, and only
  the hash moved: `1a2ad432…` → `70372f48…`.

## The cause: the capture begins at a different render block

Under the app loop, boot and the preset load take a different number of the audio tier's 128-frame blocks in virtual
time before the note script (and the capture) starts. The new loop runs one job per call, with an executor turn
between jobs, where the old code had one Embassy task per job. Measured from the dither stream:

| Cases | Old capture starts at frame | New | Change |
|---|---|---|---|
| 322 | 6016 (47 blocks) | 6656 (52 blocks) | +5 blocks |
| 14 (`sample/*`, `wavetable*`, which load sample files) | 7296 (57 blocks) | 8064 (63 blocks) | +6 blocks |
| render-song RS_STREAM | 2944 (23 blocks) | 2432 (19 blocks) | −4 blocks |

The note lands at the same frame of its capture (onset shift 0 for 303 cases). In absolute render time, though, it
lands 5 or 6 blocks later. So free-running state that the render advances block by block (LFO and oscillator phases,
effect tails, the dither) meets the note at a different point. This is the note-timing dependence that the 2026-09-29
shift sweep measured (3 and 7 blocks bracket it). The synth's own random stream is reseeded at the first note, so it
does not depend on this.

**Experiment.** The old code rendered every case with its notes delayed by 5 blocks (6 for the 14 sample and
wavetable cases). From frame `shift × 128`, that render is **byte-identical to the new render in 335 of 336 cases**.
The remaining case, `input_stereo`, plays the harness's test input. That input is a sawtooth counted from the
capture's start, with a 128-frame period on the left and 256 frames on the right. A 5-block delay moves the right
channel's phase by half a period relative to the note. When the old render's input origin was also offset by 640
frames (a scratch-only change to the worktree's `host.rs`), `input_stereo` is byte-identical as well. **All 336
cases are therefore the old code's sound, with the note arriving 5 or 6 render blocks later after boot.** The only
render-song difference is the dither (at most one output LSB), from its capture starting 4 blocks earlier.

No case pointed to a change in the sound engine, a different note pattern, or a lost or extra note.

## Every case

"onset shift" is the new onset minus the old, in frames. The timing tolerance is the 2026-09-29 sweep's, per case.

| case | onset shift | Δrms dB | Δpeak dB | env | centroid % | timing tol Δrms / env / centroid | class |
|---|---|---|---|---|---|---|---|
| analog_osc | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 1.67 / 1.000 / 43.3 | within |
| community/12 Metalliche | 0 | -0.010 | -0.042 | 0.9999 | +0.18 | 0.09 / 0.998 / 1.2 | within |
| community/1994 Su | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.45 / 0.837 / 8.7 | within |
| community/8 O'Clock | 0 | +0.010 | -0.010 | 0.9998 | -0.12 | 1.50 / 0.827 / 14.3 | within |
| community/80's Ffunk | 0 | +0.001 | +0.000 | 1.0000 | -0.68 | 0.00 / 1.000 / 1.0 | within |
| community/A Busted Sqb | 0 | +0.335 | -0.074 | 0.6545 | +2.19 | 1.40 / 0.480 / 6.4 | within |
| community/A Fm Bass Funk | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| community/A Fm Floot | 0 | -0.018 | -0.007 | 0.9998 | +0.11 | 0.33 / 0.966 / 5.9 | within |
| community/A Fm Pad | -198 | -0.014 | +0.614 | 0.9922 | +0.17 | 2.20 / 0.798 / 32.7 | within |
| community/A Ring Bass | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.30 / 0.998 / 9.8 | within |
| community/Aberration | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.32 / 0.986 / 8.3 | within |
| community/Analohania | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.77 / 0.733 / 2.6 | within |
| community/Apex Stab | 0 | +0.008 | +0.000 | 1.0000 | +1.11 | 0.33 / 0.930 / 59.1 | within |
| community/BASS Simple Distorted | 0 | +0.082 | +0.046 | 0.9998 | -0.28 | 0.80 / 0.979 / 8.8 | within |
| community/Backdraft Pad | 0 | -0.001 | -0.005 | 1.0000 | +0.03 | 3.66 / 0.895 / 4.6 | within |
| community/Bad Zwirlz Su | 0 | -0.103 | -0.079 | 0.9994 | +1.05 | 0.50 / 0.987 / 0.7 | within |
| community/Basedland | -1 | +0.021 | -0.011 | 0.9992 | +0.51 | 0.41 / 0.975 / 4.5 | within |
| community/Basic Bass | 0 | -0.035 | +0.003 | 0.9989 | +1.03 | 0.25 / 0.971 / 1.9 | within |
| community/BassGuitar | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| community/Bell Fm Synthwave | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.99 / 0.710 / 10.3 | within |
| community/Bird | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 3.38 / 0.966 / 11.7 | within |
| community/Blade Vang | 0 | +0.016 | -0.108 | 0.9610 | +0.42 | 4.20 / 0.593 / 57.4 | within |
| community/Boomf | 0 | +0.015 | +0.000 | 0.9996 | -0.00 | 0.82 / 0.958 / 39.2 | within |
| community/Brick Speedboat | 0 | +0.039 | -0.036 | 0.9997 | -0.13 | 0.46 / 0.740 / 14.5 | within |
| community/Broadcast | 0 | +0.106 | +0.189 | 0.9924 | -1.14 | 0.48 / 0.429 / 7.6 | within |
| community/Brzcrtch | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.08 / 0.955 / 17.7 | within |
| community/Celestial Su | 0 | +0.087 | -0.227 | 0.9934 | -0.28 | 0.33 / 0.920 / 5.8 | within |
| community/Classic Pad | 59 | -0.076 | +0.130 | 0.9988 | -1.03 | 0.41 / 0.925 / 12.9 | within |
| community/Cornwall | 0 | +0.000 | +0.000 | 1.0000 | -0.06 | 22.73 / 0.329 / 2266.9 | within |
| community/Crime Time | 0 | -0.049 | +0.034 | 0.9998 | +0.20 | 0.71 / 0.975 / 6.3 | within |
| community/Crumbling Pad | 1 | +0.652 | +1.377 | 0.9542 | +19.86 | 3.59 / 0.046 / 102.6 | within |
| community/D-303 | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.01 / 0.970 / 0.3 | within |
| community/Desparate Synth | 0 | -0.004 | -0.111 | 0.9806 | -0.33 | 0.36 / 0.872 / 14.9 | within |
| community/Distorted Bass | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.60 / 0.994 / 13.8 | within |
| community/Drop | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.05 / 0.999 / 4.1 | within |
| community/Dystopian Sky | 0 | +0.045 | +0.115 | 0.9996 | -0.63 | 1.59 / 0.805 / 17.0 | within |
| community/Echoy Melody | 0 | +0.011 | -0.095 | 0.9998 | +0.10 | 0.38 / 0.996 / 3.1 | within |
| community/Farvoice | 0 | +0.004 | +0.000 | 1.0000 | +0.03 | 0.29 / 1.000 / 1.2 | within |
| community/Faulty Circuit | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.2 | within |
| community/Fauxtar | 0 | +0.010 | +0.000 | 1.0000 | -0.15 | 0.46 / 0.973 / 13.8 | within |
| community/Flea Circuits Su | 0 | -0.001 | +0.000 | 1.0000 | +0.14 | 0.00 / 1.000 / 1.6 | within |
| community/Fm Hammer String | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.49 / 0.997 / 7.8 | within |
| community/Fm Leadandbass | 0 | +0.002 | -0.003 | 1.0000 | -0.01 | 2.83 / 0.686 / 20.8 | within |
| community/Fm Pads | 0 | -0.016 | -0.012 | 0.9999 | -0.02 | 2.10 / 0.982 / 8.4 | within |
| community/Fm Strimgs | 0 | +0.010 | -0.193 | 0.9999 | -0.31 | 0.78 / 0.915 / 17.1 | within |
| community/Frances2 | 0 | +0.000 | -0.019 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| community/Fuzzring | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.52 / 0.952 / 23.5 | within |
| community/Fw Landscape | 0 | +0.003 | +0.007 | 1.0000 | +0.15 | 0.04 / 1.000 / 4.7 | within |
| community/Giant Robot Fm | 0 | -0.077 | -0.005 | 0.9817 | -6.91 | 0.58 / 0.965 / 41.0 | within |
| community/Glass Desert | -1 | -0.379 | -0.296 | 0.9994 | +0.65 | 3.60 / 0.601 / 17.0 | within |
| community/Glass Pluck | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.45 / 0.998 / 2.4 | within |
| community/Glistening | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 1.11 / 0.946 / 10.4 | within |
| community/Gong | 0 | -0.008 | +0.031 | 0.9986 | +0.10 | 0.13 / 0.973 / 0.9 | within |
| community/Growler | 0 | +0.001 | +0.004 | 1.0000 | -0.12 | 0.06 / 0.951 / 2.2 | within |
| community/Growlstrom | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.06 / 0.990 / 1.9 | within |
| community/Heartspark | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.09 / 0.978 / 0.6 | within |
| community/Hollow Dark | 0 | +0.038 | -0.336 | 0.9952 | +0.38 | 1.09 / 0.935 / 3.5 | within |
| community/Hollowring | 0 | +0.007 | -0.102 | 0.9988 | -0.41 | 1.39 / 0.870 / 12.3 | within |
| community/Infernal Ring | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.47 / 0.907 / 7.8 | within |
| community/J6 Pad | 0 | -0.032 | -0.029 | 1.0000 | +0.56 | 0.23 / 0.935 / 3.5 | within |
| community/Jehorn | 0 | +0.006 | +0.013 | 1.0000 | -0.35 | 0.42 / 0.981 / 61.7 | within |
| community/Juno Style | 0 | +0.009 | +0.005 | 1.0000 | -0.16 | 2.43 / 0.960 / 2.9 | within |
| community/Kaleidoscope | 0 | +0.183 | +0.008 | 0.9821 | -1.15 | 0.57 / 0.890 / 11.2 | within |
| community/Livewire Arp | 0 | +0.000 | +0.000 | 1.0000 | -0.02 | 0.79 / 0.952 / 27.2 | within |
| community/MPE 80'S Soundtrack | 0 | -0.112 | +0.076 | 0.9982 | -2.95 | 0.52 / 0.960 / 21.9 | within |
| community/MPE Curvilinear | 0 | +0.007 | +0.006 | 1.0000 | +0.14 | 0.65 / 0.959 / 7.3 | within |
| community/MPE Desert Strata | 0 | +0.003 | +0.157 | 0.9998 | +0.92 | 0.23 / 0.781 / 91.7 | within |
| community/MPE FM Growler | 0 | -0.003 | +0.050 | 1.0000 | -0.04 | 4.55 / 0.926 / 2062.1 | within |
| community/MPE Flute | 3 | +0.009 | +0.000 | 0.9986 | +0.15 | 2.33 / 0.971 / 5.3 | within |
| community/MPE Haunted Music Box | 0 | -0.039 | +0.000 | 0.9941 | +0.34 | 0.17 / 0.843 / 2.8 | within |
| community/MPE Rich Saw Bass | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 2.17 / 0.884 / 30.2 | within |
| community/MPE Super Saw | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.60 / 0.671 / 4.9 | within |
| community/MPE Wood Pluck | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.10 / 1.000 / 0.2 | within |
| community/Mean Pluck | 0 | -0.174 | -0.434 | 1.0000 | +2.21 | 0.69 / 0.999 / 12.4 | within |
| community/Modello Uno | 0 | -0.028 | -0.023 | 0.9998 | +0.52 | 0.11 / 0.993 / 3.4 | within |
| community/Mouse Tail | 0 | +0.004 | +0.056 | 0.9999 | +0.14 | 0.04 / 0.999 / 4.6 | within |
| community/Nilsish | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.74 / 0.954 / 11.4 | within |
| community/Noisewobble | 0 | -0.033 | +0.000 | 0.9286 | -0.48 | 0.55 / -0.174 / 3.8 | within |
| community/Nowhere Home | 0 | -0.087 | +0.114 | 0.9926 | +0.49 | 1.94 / 0.801 / 41.5 | within |
| community/Obassity | 0 | +0.006 | +0.000 | 0.9996 | +0.04 | 0.05 / 0.995 / 1.0 | within |
| community/Oimp | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.1 | within |
| community/Organic 1 | 1 | -0.086 | -0.002 | 0.9949 | +0.05 | 0.44 / 0.778 / 1.8 | within |
| community/Orgavolution | 0 | -0.008 | +0.017 | 1.0000 | +0.12 | 0.05 / 1.000 / 0.8 | within |
| community/Overloading | 0 | +0.069 | +0.467 | 0.9959 | +3.94 | 0.61 / 0.941 / 10.6 | within |
| community/Pakmahn Su | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.16 / 0.926 / 3.8 | within |
| community/Pianism I | 0 | +0.001 | +0.000 | 1.0000 | +0.34 | 0.89 / 0.850 / 4.8 | within |
| community/Pomp | 0 | +0.002 | +0.002 | 1.0000 | -0.13 | 0.01 / 1.000 / 2.2 | within |
| community/Pusher | 0 | -0.003 | +0.003 | 1.0000 | +0.02 | 1.57 / 0.914 / 12.1 | within |
| community/Rainy Wasteland | 0 | +0.004 | -1.041 | 0.9963 | -0.19 | 1.58 / 0.815 / 3.1 | within |
| community/Richy Rich | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 1.40 / 0.578 / 22.8 | within |
| community/Right On | 0 | -0.001 | -0.001 | 1.0000 | -0.03 | 0.23 / 0.833 / 5.9 | within |
| community/Ringmod Arp | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.12 / 0.980 / 1.2 | within |
| community/Ringmod Melody | 0 | +0.008 | +0.021 | 1.0000 | +0.05 | 0.05 / 0.999 / 0.3 | within |
| community/Ringmod Pad | 0 | -0.004 | -0.009 | 1.0000 | -0.00 | 0.67 / 0.670 / 4.1 | within |
| community/Rubberdemon | 0 | +0.032 | +0.092 | 0.9999 | +0.08 | 2.31 / 0.949 / 56.3 | within |
| community/Runner Lead | 0 | +0.024 | -0.121 | 0.9903 | +1.54 | 1.38 / 0.775 / 23.5 | within |
| community/Sad And Hollow | 0 | -0.000 | -0.029 | 0.9997 | -0.01 | 0.30 / 0.982 / 1.8 | within |
| community/Saturnine | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.46 / 0.997 / 5.2 | within |
| community/Sawborg | 0 | -0.033 | +0.000 | 0.9999 | +0.40 | 0.49 / 0.979 / 16.1 | within |
| community/Schizophonyx Rm | 0 | -0.008 | -0.087 | 0.9991 | +0.14 | 0.17 / 0.973 / 0.5 | within |
| community/Sense Of Doom | 0 | +0.002 | -0.675 | 0.9852 | -0.54 | 3.23 / 0.265 / 194.5 | within |
| community/Sik Rm | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.03 / 0.999 / 55.9 | within |
| community/Slogro | 0 | +0.007 | +0.021 | 0.9973 | +0.52 | 2.91 / 0.254 / 12.4 | within |
| community/Slow Hula Hoop | 0 | -0.046 | -0.178 | 0.9997 | -2.40 | 4.19 / 0.616 / 10.3 | within |
| community/Space Bass | 0 | +0.009 | +0.000 | 0.9999 | -0.06 | 0.04 / 0.998 / 0.4 | within |
| community/Speed Lead | 0 | -0.047 | -0.034 | 1.0000 | -0.17 | 4.45 / 0.741 / 62.4 | within |
| community/Spilled Soup | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.01 / 1.000 / 0.2 | within |
| community/Super Square | 0 | +0.065 | +0.075 | 0.9957 | -1.63 | 0.68 / 0.828 / 16.2 | within |
| community/Sync Squared | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.01 / 0.993 / 0.2 | within |
| community/Synthetic 1 | 0 | -0.004 | -0.004 | 1.0000 | -0.05 | 1.13 / 0.917 / 11.8 | within |
| community/Tension | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.85 / 0.988 / 6.9 | within |
| community/Thirsty Machine | 0 | -0.005 | +0.002 | 1.0000 | -0.41 | 0.27 / 0.961 / 7.2 | within |
| community/Threat | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.35 / 0.999 / 0.1 | within |
| community/Throaty | 0 | -0.035 | -0.002 | 1.0000 | -0.47 | 0.20 / 1.000 / 2.4 | within |
| community/Thunder | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 1.97 / 0.478 / 69.7 | within |
| community/Trust Me It's Useful Noise | 3 | +1.465 | +1.600 | 0.9275 | +37.11 | 5.78 / 0.771 / 195.3 | within |
| community/Ttty | 0 | +0.068 | -0.048 | 0.9939 | +0.56 | 0.49 / 0.833 / 5.1 | within |
| community/UK Dub Siren | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.12 / 0.993 / 1.6 | within |
| community/Undulating Drone | 0 | -0.037 | +0.150 | 0.9976 | -1.04 | 1.12 / 0.693 / 9.1 | within |
| community/Wall Of Static | 0 | -0.020 | -0.039 | 0.9983 | -0.05 | 0.34 / 0.781 / 2.4 | within |
| community/Warble Harp Lead | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 1.55 / 0.973 / 4.7 | within |
| community/Warm DX7 Pad | 1 | -0.074 | -0.083 | 0.9997 | -1.13 | 0.77 / 0.890 / 11.5 | within |
| community/Wavedash | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.01 / 1.000 / 2.0 | within |
| community/Weather | 12 | -0.893 | -0.336 | 0.9491 | +29.87 | 3.81 / 0.422 / 212.5 | within |
| community/Wind Whistle | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.84 / 0.900 / 1.3 | within |
| community/Woodeep | 0 | -0.128 | -0.517 | 0.9963 | +0.91 | 1.45 / 0.901 / 17.6 | within |
| community/Working Engine | 0 | +0.054 | +0.395 | 0.8673 | -0.46 | 5.27 / 0.501 / 74.4 | within |
| community/Worrisome | 0 | -0.000 | -0.005 | 1.0000 | +0.00 | 2.56 / 0.907 / 22.8 | within |
| community/Wub Sync Bass | 0 | +0.002 | +0.000 | 1.0000 | +0.00 | 0.01 / 1.000 / 0.1 | within |
| env_shape | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/000 Rich Saw Bass | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 2.18 / 0.885 / 30.0 | within |
| factory/001 Sync Bass | 0 | -0.017 | +0.019 | 0.9999 | -0.33 | 4.35 / 0.962 / 50.8 | within |
| factory/002 Basic Square Bass | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/003 Synthwave Bass | 0 | +0.002 | -0.017 | 1.0000 | +0.30 | 5.86 / 0.954 / 39.5 | within |
| factory/004 Dubby Bass | 0 | +0.009 | -0.009 | 1.0000 | +0.05 | 0.05 / 1.000 / 0.3 | within |
| factory/005 Sweet Mono Bass | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 2.08 / 0.792 / 27.6 | within |
| factory/006 Vaporwave Bass | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 2.72 / 0.798 / 70.6 | within |
| factory/007 Detuned Saw Bass | 0 | -0.002 | +0.011 | 0.9999 | +0.26 | 1.38 / 0.891 / 4.1 | within |
| factory/008 FM Rich Distorted Bass | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 4.73 / 0.865 / 35.1 | within |
| factory/009 Hoover Bass | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 3.35 / 0.858 / 74.8 | within |
| factory/010 Gravel Basscamp | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/011 Dubstep Bass | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.01 / 0.991 / 0.4 | within |
| factory/012 Blunt Sync Bass | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/013 Trap Bass 1 | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.1 | within |
| factory/014 Trap Bass 2 | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.01 / 0.997 / 0.7 | within |
| factory/015 Resonant Filter Bass | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.03 / 0.999 / 0.5 | within |
| factory/016 Dark Saturated Bass | 0 | +0.006 | +0.219 | 0.9993 | +0.41 | 1.10 / 0.789 / 49.2 | within |
| factory/017 Impact Saw Lead | 0 | +0.000 | +0.002 | 1.0000 | -0.00 | 1.41 / 0.883 / 30.0 | within |
| factory/018 Rich Saw Lead | 0 | -0.000 | -0.012 | 0.9999 | +0.00 | 0.22 / 0.818 / 3.4 | within |
| factory/019 Fizzy Strings | 0 | -0.006 | -0.046 | 0.9996 | -0.92 | 0.10 / 0.992 / 4.8 | within |
| factory/020 Soft Saw Lead | 0 | +0.000 | -0.006 | 1.0000 | -0.01 | 0.00 / 1.000 / 0.1 | within |
| factory/021 80s TV Lead | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/022 Rich Filter LFO Lead | 0 | -0.002 | -0.005 | 1.0000 | -0.06 | 1.10 / 0.732 / 19.7 | within |
| factory/023 Analog Mono Wow | 0 | +0.059 | +0.003 | 0.9995 | -0.31 | 4.20 / 0.751 / 63.7 | within |
| factory/024 Warble Bass Pluck | 0 | +0.042 | -0.008 | 0.9969 | -0.15 | 0.22 / 0.970 / 2.8 | within |
| factory/025 Soft Synth Organ | 0 | -0.035 | -0.103 | 0.9996 | +0.43 | 0.09 / 0.997 / 1.1 | within |
| factory/026 PW Organ | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/027 PW Envelope | 0 | -0.000 | +0.002 | 1.0000 | +0.03 | 0.00 / 1.000 / 0.1 | within |
| factory/028 PWM | 0 | +0.000 | -0.069 | 1.0000 | +0.64 | 0.00 / 1.000 / 3.6 | within |
| factory/029 Chiptune Trill | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.03 / 0.998 / 0.3 | within |
| factory/030 Distant Porta | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/031 Nasal Choir | 0 | +0.001 | +0.013 | 1.0000 | +0.05 | 0.12 / 0.998 / 6.7 | within |
| factory/032 Bandpass Choir | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.1 | within |
| factory/033 Rich Square | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.60 / 0.951 / 14.1 | within |
| factory/034 Square Choir | 0 | +0.002 | -0.089 | 0.9994 | +0.01 | 0.22 / 0.982 / 0.5 | within |
| factory/035 Bell Lead & Bass | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 2.03 / 0.654 / 29.0 | within |
| factory/036 Analog Ambient Square | 0 | -0.008 | +0.037 | 1.0000 | +0.02 | 0.04 / 0.999 / 0.2 | within |
| factory/037 Echo Chord | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 1.00 / 0.848 / 4.0 | within |
| factory/038 Vapor Arp | 0 | +0.132 | +0.018 | 0.9222 | +0.58 | 0.53 / 0.732 / 2.3 | within |
| factory/039 Detuned Retriggering Saws | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/040 Spacer Leader | 5 | +0.014 | -0.015 | 0.9756 | +0.08 | 0.16 / 0.219 / 0.6 | within |
| factory/041 Zithar - Vibed | 1 | +0.021 | -0.022 | 0.9780 | -0.19 | 5.86 / 0.154 / 29.3 | within |
| factory/042 High Triangle | 0 | -0.000 | +0.002 | 0.9999 | +0.02 | 0.00 / 1.000 / 0.1 | within |
| factory/043 Square Porta | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.1 | within |
| factory/044 8-Bit Lead | 0 | -0.000 | -0.032 | 1.0000 | -0.34 | 0.00 / 1.000 / 2.0 | within |
| factory/045 Square Sync | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.1 | within |
| factory/046 Saw Sync | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/047 Basic Dirty Bass | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/048 Thin Pulse Bass | 0 | -0.000 | +0.005 | 1.0000 | +0.19 | 0.30 / 0.964 / 2.0 | within |
| factory/049 Basic FM | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.14 / 0.998 / 5.9 | within |
| factory/050 FM Basic Bass | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/051 FM Rich Brass | 0 | +0.000 | +0.000 | 0.9999 | -0.04 | 2.01 / 0.957 / 40.4 | within |
| factory/052 Soft FM | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 1.21 / 0.700 / 24.7 | within |
| factory/053 Detuned FM Horns | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.77 / 0.871 / 9.5 | within |
| factory/054 Ghostly Sines | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/055 FM Theremin | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.04 / 0.949 / 13.4 | within |
| factory/056 FM Bell Modulation | 0 | -0.003 | -0.239 | 1.0000 | -0.22 | 0.80 / -0.222 / 2.8 | within |
| factory/057 FM Lead | 0 | -0.001 | +0.002 | 1.0000 | -0.01 | 0.00 / 1.000 / 0.0 | within |
| factory/058 FM Rising Attack | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/059 Distorted Lead Guitar | 0 | -0.005 | +0.000 | 1.0000 | +0.06 | 0.00 / 1.000 / 0.2 | within |
| factory/060 Bass Guitar | 0 | +0.009 | +0.002 | 0.9999 | +0.24 | 0.04 / 0.999 / 1.0 | within |
| factory/061 Blown-Staccato-Panpipes | 0 | +0.002 | -0.014 | 1.0000 | -0.49 | 0.29 / 0.997 / 3.6 | within |
| factory/062 Trumpet | 0 | -0.001 | +0.006 | 1.0000 | +0.02 | 0.00 / 1.000 / 0.0 | within |
| factory/063 Tuba | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 2.23 / 0.917 / 99.6 | within |
| factory/064 Reeds-Flute-Oboe | 0 | -0.004 | +0.002 | 1.0000 | -0.01 | 0.54 / 0.939 / 5.5 | within |
| factory/065 Cello | 0 | +0.000 | +0.000 | 0.9999 | +0.00 | 0.03 / 0.999 / 0.7 | within |
| factory/066 Violin | 0 | -0.000 | -0.017 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/067 Marimba | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/068 FM Bells 1 | 0 | +0.000 | -0.011 | 1.0000 | -0.03 | 0.00 / 1.000 / 0.2 | within |
| factory/069 FM Bells 2 | 0 | +0.000 | -0.013 | 1.0000 | -0.02 | 0.00 / 1.000 / 0.1 | within |
| factory/070 Glockenspiel | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/071 Rhodes | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 3.23 / 0.916 / 43.4 | within |
| factory/072 Kyoto Phono | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.04 / 1.000 / 0.2 | within |
| factory/073 Piano | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 3.70 / 0.971 / 48.7 | within |
| factory/074 Electric Piano | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.47 / 0.910 / 6.9 | within |
| factory/075 Electric Piano With Strings | 0 | +0.039 | +0.101 | 0.9952 | -0.26 | 1.49 / 0.749 / 8.7 | within |
| factory/076 Organ | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.29 / 0.870 / 4.7 | within |
| factory/077 FM Perc-Organ | 0 | -0.073 | +0.027 | 0.9921 | +0.11 | 0.79 / 0.581 / 9.6 | within |
| factory/078 House | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/079 Phased Arper | -1 | -0.031 | -0.133 | 0.9965 | +0.14 | 3.54 / 0.789 / 27.2 | within |
| factory/080 House | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.01 / 1.000 / 0.5 | within |
| factory/081 Xylophone Big Bass | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 2.19 / 0.871 / 62.8 | within |
| factory/082 Short Sharp Delay | 1 | -0.004 | +0.145 | 1.0000 | +0.08 | 2.54 / 0.965 / 39.4 | within |
| factory/083 Dark Chorus | 0 | +0.012 | +0.225 | 0.9999 | -0.29 | 0.23 / 0.967 / 10.2 | within |
| factory/084 FM Narrow Band | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.40 / 0.989 / 8.2 | within |
| factory/085 Deep Fizz | 0 | +0.001 | +0.000 | 1.0000 | +0.00 | 0.01 / 0.989 / 0.1 | within |
| factory/086 Techno Organ | -1 | -0.020 | -0.093 | 0.9925 | +0.15 | 0.39 / 0.955 / 17.2 | within |
| factory/087 Define Leader | 0 | +0.014 | +0.156 | 0.9858 | +0.89 | 0.03 / 0.971 / 2.0 | within |
| factory/088 Yelp Chords | 0 | -0.001 | -0.080 | 0.9998 | +0.45 | 2.30 / 0.942 / 69.3 | within |
| factory/089 Degraded Retro Lead | 0 | +0.012 | +0.206 | 0.9914 | -0.28 | 0.03 / 0.981 / 1.2 | within |
| factory/090 FM Organ | 0 | -0.004 | -0.035 | 0.9998 | +0.08 | 0.01 / 1.000 / 0.2 | within |
| factory/091 FM Ricochet | 0 | +0.002 | +0.067 | 0.9998 | -0.06 | 0.78 / 0.957 / 18.2 | within |
| factory/092 Degraded Tremolo | -1 | +0.183 | +0.054 | 0.9684 | -0.24 | 2.12 / 0.701 / 28.5 | within |
| factory/093 FM Distorted Bells | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.02 / 0.999 / 5.9 | within |
| factory/094 Ambient Occlusion Lead | 0 | +0.086 | -0.234 | 0.9725 | -0.13 | 0.68 / 0.792 / 9.2 | within |
| factory/095 Harsh FM Feedback | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.32 / 0.977 / 7.9 | within |
| factory/096 FM Guitar Power Chord | 0 | +0.000 | +0.000 | 1.0000 | -0.01 | 0.00 / 1.000 / 0.0 | within |
| factory/097 Saturated Filter | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.06 / 0.999 / 2.9 | within |
| factory/098 Saturated Sync | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.7 | within |
| factory/099 Overdrive Reese Sync | 0 | -0.226 | +0.006 | 0.9779 | +0.92 | 0.30 / 0.915 / 3.0 | within |
| factory/100 Noise Lead | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.13 / 0.999 / 70.7 | within |
| factory/101 Atebit | 0 | -0.003 | -0.463 | 0.9998 | -0.06 | 0.02 / 0.991 / 0.2 | within |
| factory/102 Harsh 5th | 0 | -0.000 | +0.053 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/103 Sci-fi Chaos | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.09 / 1.000 / 0.8 | within |
| factory/104 Alien Vomit | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.01 / 0.997 / 0.4 | within |
| factory/105 Attack Bass | 0 | +0.036 | +0.127 | 0.9976 | -5.54 | 0.35 / 0.969 / 13.2 | within |
| factory/106 Hang Drum | 1 | +0.097 | +0.545 | 0.9747 | +0.34 | 1.41 / 0.885 / 5.8 | within |
| factory/107 FM LPG Percussion | 0 | -0.145 | -0.013 | 0.9998 | -0.02 | 8.10 / 0.613 / 4.5 | within |
| factory/108 Robo Arp | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 1.69 / 0.835 / 1.6 | within |
| factory/109 Talking Arp | -120 | +0.287 | -0.369 | 0.9852 | -9.99 | 1.32 / 0.911 / 20.7 | within |
| factory/110 Crystalline Ringmod | 0 | +0.027 | +0.000 | 1.0000 | +1.14 | 0.16 / 0.999 / 7.0 | within |
| factory/111 Satellite Drum | 0 | -0.881 | -0.421 | 0.9300 | +7.44 | 0.90 / 0.925 / 6.9 | within |
| factory/112 Hard Tech Beat | 0 | -0.001 | -0.004 | 0.9998 | +0.16 | 0.05 / 0.992 / 4.6 | within |
| factory/113 Bio Lab | 0 | +0.067 | -0.024 | 0.9947 | -1.76 | 4.21 / 0.683 / 69.0 | within |
| factory/114 Sootheerio | 0 | -0.003 | -0.026 | 0.9994 | +0.05 | 0.03 / 0.951 / 0.2 | within |
| factory/115 Sounds After Take-off | 0 | -0.062 | +0.000 | 0.9996 | +0.23 | 2.20 / 0.757 / 14.4 | within |
| factory/116 Evolving Frequencies | 0 | +0.031 | +0.010 | 0.8982 | +0.25 | 0.81 / 0.581 / 30.9 | within |
| factory/117 Belledy | 0 | -0.019 | +0.020 | 0.9983 | -0.06 | 0.17 / 0.968 / 0.5 | within |
| factory/118 Small Bridge Pad | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.02 / 1.000 / 2.0 | within |
| factory/119 Stars Of The Bin Pad | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 1.04 / 0.973 / 2.0 | within |
| factory/120 High Harsh Pad | 0 | -0.044 | -0.175 | 0.9988 | -0.09 | 0.50 / 0.863 / 7.4 | within |
| factory/121 Tiny Lights | 0 | +0.009 | -0.004 | 1.0000 | -0.27 | 0.84 / 0.870 / 9.1 | within |
| factory/122 Majestic Synth Orchestra | 0 | +0.003 | -0.022 | 0.9255 | -0.09 | 0.48 / 0.718 / 21.2 | within |
| factory/123 Space Dust | 1 | +0.074 | +0.319 | 0.9940 | -1.24 | 1.79 / 0.899 / 15.8 | within |
| factory/124 Filter Modulation Pad | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.05 / 0.999 / 0.2 | within |
| factory/125 Evolving Pad | 0 | +0.021 | -0.692 | 0.9948 | -1.05 | 0.04 / 0.977 / 1.0 | within |
| factory/126 Dark FM Pad | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/127 Alien Larvae | 0 | -0.019 | +0.055 | 0.9956 | +0.49 | 0.34 / 0.816 / 9.1 | within |
| factory/128 Lunar Landing | -62 | +0.048 | +0.388 | 0.9003 | -1.51 | 0.09 / 0.814 / 2.1 | within |
| factory/129 Sci-fi Scenic | 172 | +0.022 | +0.592 | 0.9509 | +1.04 | 1.15 / 0.735 / 6.1 | within |
| factory/130 Dark Strings | 0 | +0.001 | -0.014 | 1.0000 | +0.01 | 2.37 / 0.644 / 35.6 | within |
| factory/131 Warm Strings | 1 | -0.014 | -0.096 | 0.9996 | -0.10 | 0.15 / 0.990 / 1.4 | within |
| factory/132 Organ Strings | -1 | +0.000 | +0.003 | 0.9998 | -0.02 | 0.02 / 1.000 / 0.4 | within |
| factory/133 80s Strings | 0 | -0.028 | +0.726 | 0.9654 | -0.03 | 0.60 / 0.288 / 20.0 | within |
| factory/134 Melody String | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 1.38 / 0.588 / 25.4 | within |
| factory/135 Soothing Growth Pad | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 5.55 / 0.303 / 27.5 | within |
| factory/136 Synthwave Pad | 0 | -0.035 | -0.010 | 0.9589 | +1.09 | 1.30 / 0.520 / 17.6 | within |
| factory/137 Epic Saw Modulation Pad | 0 | +0.109 | +0.170 | 0.9844 | -1.38 | 1.20 / 0.507 / 10.8 | within |
| factory/138 Brassy Pad | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 1.40 / 0.815 / 19.9 | within |
| factory/139 Detuned Saw Pad | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 2.26 / 0.948 / 26.4 | within |
| factory/140 Slow Aural Swells | 0 | +0.001 | +0.060 | 0.9923 | +0.14 | 1.23 / 0.764 / 196.5 | within |
| factory/141 Ringmod Pad | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.32 / 0.998 / 30.8 | within |
| factory/142 Phaser | -1 | +0.026 | +0.016 | 0.9999 | -0.39 | 0.15 / 1.000 / 3.4 | within |
| factory/143 Chillout Pad | -2 | -0.003 | -0.276 | 0.9985 | +0.06 | 0.08 / 0.962 / 1.0 | within |
| factory/144 Sweep Chords | 0 | -0.026 | -0.020 | 0.9994 | +0.77 | 0.60 / 0.842 / 24.3 | within |
| factory/145 Eerie High Pad | 0 | +0.000 | -0.011 | 1.0000 | +0.01 | 0.00 / 1.000 / 0.1 | within |
| factory/146 Atmospheric Squares Pad | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.24 / 0.986 / 4.3 | within |
| factory/147 Resonant Filter Pad | 0 | +0.002 | -0.005 | 1.0000 | +0.04 | 0.32 / 0.623 / 3.2 | within |
| factory/148 Warm 5th Pad | 1 | +0.018 | +0.028 | 0.9973 | +0.22 | 2.98 / 0.763 / 30.6 | within |
| factory/149 Cold 5th Pad | 5 | +0.001 | +0.090 | 0.9930 | +0.44 | 0.78 / 0.905 / 9.2 | within |
| factory/150 Vaporwave Pad | 0 | -0.023 | +0.145 | 0.9965 | +0.27 | 0.15 / 0.962 / 2.5 | within |
| factory/151 Radiant FM Pad | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.24 / 0.943 / 4.0 | within |
| factory/152 Small Jet Pad | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.64 / 0.999 / 1.7 | within |
| factory/153 FM Modulation Pad | 1 | -0.012 | +0.071 | 0.9988 | -0.39 | 0.60 / 0.961 / 9.8 | within |
| factory/154 Rich FM Pad 1 | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.15 / 0.725 / 2.4 | within |
| factory/155 Rich FM Pad 2 | 1 | -0.000 | +0.003 | 1.0000 | -0.00 | 0.90 / 0.938 / 18.9 | within |
| factory/156 Rich FM Pad 3 | 1 | +0.004 | +0.054 | 0.9997 | -0.18 | 0.06 / 0.990 / 1.4 | within |
| factory/157 Rich FM Pad 4 | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| factory/158 Tempo-Synced LFO | -5 | +0.008 | +0.110 | 0.9927 | +0.61 | 2.38 / 0.827 / 29.2 | within |
| factory/159 80s Bass Rhythm | 0 | -0.024 | -0.025 | 1.0000 | +0.22 | 0.81 / 0.980 / 2.4 | within |
| factory/160 Synthwave Bass Arp | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 2.68 / 0.716 / 54.0 | within |
| factory/161 Synthwave Vibrato Arp | 0 | -0.039 | +0.719 | 0.9965 | +1.87 | 0.98 / 0.845 / 31.1 | within |
| factory/162 Busy Arp | 1 | -0.178 | -0.179 | 0.9874 | +1.27 | 2.39 / 0.394 / 15.3 | within |
| factory/163 Crisp Pop Arp | 0 | +0.005 | -0.005 | 0.9997 | -0.05 | 0.03 / 0.995 / 0.4 | within |
| factory/164 Study Arp | 0 | -0.073 | -0.116 | 0.9965 | -1.19 | 0.35 / 0.906 / 7.0 | within |
| factory/165 Acid Arp | 0 | -0.001 | -0.003 | 0.9992 | -0.30 | 1.07 / 0.883 / 89.3 | within |
| factory/166 Harpsichord Cyborg | 1 | +0.033 | +0.193 | 0.9992 | +1.29 | 0.91 / 0.960 / 3.5 | within |
| factory/167 FM Metallic Bass Arp | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.04 / 1.000 / 0.1 | within |
| fm_2op | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.33 / 1.000 / 21.3 | within |
| fm_feedback | 0 | -0.000 | +0.000 | 1.0000 | -0.00 | 0.66 / 1.000 / 14.5 | within |
| input_mono | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| input_pitchshift | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| input_stereo | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| lfo_vibrato | 0 | -0.002 | +0.000 | 0.9999 | -0.55 | 0.00 / 1.000 / 0.2 | within |
| lpf_24db_env2 | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.10 / 0.998 / 25.8 | within |
| mono_legato | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.2 | within |
| noise_only | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.06 / 0.998 / 0.7 | within |
| pitch_bend | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 0.999 / 0.0 | within |
| poly_chord | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.01 / 0.962 / 0.3 | within |
| porta | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.1 | within |
| ringmod | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.4 | within |
| sample/linear | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| sample/mono | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| sample/reversed | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| sample/smooth_sweep | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| sample/timestretch | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 3.36 / 0.739 / 69.4 | within |
| saw_basic | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| sine_basic | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.1 | within |
| square_basic | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.1 | within |
| square_pw | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.1 | within |
| square_pw_low | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.4 | within |
| svf_band | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.0 | within |
| triangle_basic | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.1 | within |
| two_osc_detune | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 0.999 / 0.9 | within |
| unison_spread | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.49 / 0.986 / 33.9 | within |
| velocity_dynamics | 0 | -0.000 | +0.000 | 1.0000 | +0.00 | 0.00 / 1.000 / 0.3 | within |
| wavefold_sine | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.0 | within |
| wavetable/morph_lfo | 0 | +0.026 | +0.039 | 0.9998 | -0.06 | 0.12 / 0.996 / 0.1 | within |
| wavetable/morph_note | 0 | +0.081 | +0.084 | 0.9993 | -3.98 | 1.33 / 0.990 / 47.2 | within |
| wavetable/morph_sync | 0 | -0.057 | +0.479 | 0.9950 | -0.18 | 0.35 / 0.880 / 29.9 | within |
| wavetable/scw | 0 | +0.000 | +0.000 | 1.0000 | +0.00 | 0.55 / 0.960 / 7.4 | within |
| wavetable/scw_hi | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.27 / 0.992 / 6.8 | within |
| wavetable/static | 0 | +0.010 | +0.000 | 1.0000 | -0.15 | 0.08 / 1.000 / 0.6 | within |
| wavetable/static_lo | 0 | +0.010 | +0.000 | 1.0000 | -0.20 | 0.06 / 0.835 / 0.7 | within |
| wavetable/sync | 0 | -0.031 | +0.477 | 0.9962 | -0.05 | 0.46 / 0.878 / 35.7 | within |
| wavetable_basic | 0 | +0.000 | +0.000 | 1.0000 | -0.00 | 0.00 / 1.000 / 0.1 | within |
