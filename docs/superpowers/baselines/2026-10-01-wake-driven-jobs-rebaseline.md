# Golden masters: wake-driven jobs

2026-10-01. The evidence behind re-recording `tests/integration/golden/presets.json` (golden-synth, 336 cases) and the
`RS_STREAM` record in `tests/integration/golden/songs.json` (render-song) on `feat/wake-driven-jobs`. This branch turns
the app loop's polling jobs into jobs that run when a producer wakes or requests them. The goldens moved at one commit,
c7ed79a05 ("preset and pattern loads run when their channels answer"), and render-song at ed09419bd ("song load runs
when its channels answer"); no other commit on the branch changed a rendered byte. Every case is explained below by one
cause, which an experiment showed byte for byte.

## The two commits compared

- **Old**: 3b685d7ee, where this branch starts. Its records are identical to those at 85a3b1532 (no commit between
  them touched a render). The old renders (`.superpowers/renders/apploop_new`, kept by the 2026-09-30 rebaseline) hash
  to the old records in all 336 cases, and the old RS_STREAM render hashes to its record `70372f48…`.
- **New**: this branch's head (the idle-job-rates scenario, c76cb18c9, on top of 9cdef24d9). Its renders were kept with
  `RENDER_PRESET_KEEP` (`.superpowers/renders/wake10_head`) during the re-record. They are byte-identical in 336 of 336
  cases to the renders made at c7ed79a05 (`.superpowers/renders/wake9_il`), the commit where the goldens moved, and
  RS_STREAM renders `0b8279f4…`, the same hash as at ed09419bd (`.superpowers/renders/wake9_sl`). The commits after
  them changed comments and tests only.

## Method

1. Each case was compared with `tests/integration/golden/compare_renders.py` against the per-case timing tolerance the
   2026-09-29 rebaseline measured (`.superpowers/renders/tolerance.json`, `tolerance.py`'s rule: Δrms within 1.5x the
   tolerance + 0.1 dB, envelope correlation at least min(tol − 0.05, 0.99), centroid within 1.5x + 3 %), with
   `.superpowers/renders/wake9_evidence/tolcheck.py`.
2. The cause, from the output dither. The master dither's LCG (`outputDither()` in `audio_engine.cpp`) advances twice
   per frame the output stage drains since boot, and a render is pure dither before its first note, so its first 64
   frames give the frame at which the capture began (`ditherall.py`).
3. The proof: the **new** code rendered each case with its note script delayed by the measured shift
   (`shiftrun.py`, using `.superpowers/renders/shift_sweep.py`), and that render, from frame `shift × 128`, was
   compared byte for byte with the old render (`shiftcmp.py`). This was done at c7ed79a05 (`wake9_il_shift`) and
   repeated at the head (`wake10_shift`), with the same result.

The scripts and the per-case table are in `.superpowers/renders/wake9_evidence/` (`per_case.tsv`, `shifts.json`); the
head's re-measured shifts and the idle-job-rates output are in `.superpowers/renders/wake10_evidence/`.

## Results

| | Cases |
|---|---|
| same (byte-identical) | 0 |
| within the timing tolerance | 333 |
| beyond the timing tolerance | 3 |
| byte-identical to the old render once the new code's notes are shifted | **336** |

- No case changed length, and none went silent. Every record keeps its `frames`. `sha256` moved in all 336, `peak` in
  218 and `rms_db` in 161.
- The worst changes within tolerance are |Δrms| 2.365 dB, envelope correlation 0.0387 and |Δcentroid| 56.2 %, each
  inside that case's own tolerance.
- 136 cases differ by at most one output LSB (raw ±256): only their dither moved.
- The ARM leg (golden-synth-arm) passes all 336 cases against the new records.

### The three beyond the tolerance

All three are shift 50, and all three are byte-identical to the old render once the new code's notes are delayed by 50
blocks, so they are the same timing shift as the other 333: the 2026-09-29 sweep measured shifts of up to 29 blocks,
which do not bracket 50. The latter two were already "marginal: just outside timing tolerance" in the 2026-09-29 record.

| case | shift (blocks) | Δrms dB (tol) | env (tol) | Δcentroid % (tol) | shifted identical |
|---|---|---|---|---|---|
| community/Classic Pad | 50 | +0.764 (0.410) | 0.9488 (0.9251) | +11.3 (12.9) | yes |
| factory/128 Lunar Landing | 50 | −0.277 (0.092) | 0.9193 (0.8141) | +1.5 (2.1) | yes |
| factory/137 Epic Saw Modulation Pad | 50 | −1.002 (1.201) | 0.4435 (0.5066) | +14.5 (10.8) | yes |

## The cause: the note arrives 50 render blocks earlier after boot

Boot's blank preset and each case's preset load used to wait out the instrument load's 30 ms job gap at every step;
now the job runs when its channel answers. So the case's note script (and the capture) starts earlier in render time.
Measured from the dither stream, identical at c7ed79a05 and at the head:

| Cases | Old capture starts at frame | New | Change |
|---|---|---|---|
| 322 | 6656 (52 blocks) | 256 (2 blocks) | −50 blocks |
| 14 (`sample/*`, `wavetable*`, which load sample files) | 8064 (63 blocks) | 256 (2 blocks) | −61 blocks |
| render-song RS_STREAM | 2432 (19 blocks) | 896 (7 blocks) | −12 blocks |

The note lands at the same frame of its capture, but 50 (61) blocks earlier in absolute render time, so free-running
state the render advances block by block (LFO and oscillator phases, effect tails, the dither) meets it at a different
point. The synth's own random stream is reseeded at the first note, so it does not depend on this.

**Experiment.** The new code rendered every case with its notes delayed by 50 blocks (61 for the 14 sample and
wavetable cases). From frame `shift × 128`, that render is **byte-identical to the old render in 336 of 336 cases**,
`input_stereo` included (a 50-block shift is a whole number of the test input's 128- and 256-frame periods). **All 336
cases are therefore the old code's sound with the note arriving 50 or 61 render blocks earlier after boot.**

**render-song.** RS_STREAM moves only in the dither: Δrms 0.000 dB, envelope 1.000, wave 1.000; frames (384000), peak
(142067968) and rms_db (−31.24) unchanged; the largest sample difference is one output LSB (raw 256), on 30.8 % of
samples. Its capture starts 12 blocks earlier because the song load no longer waits out 6 ms job gaps. Within tolerance;
the hash moves `70372f48…` → `0b8279f4…`.

No case pointed to a change in the sound engine, a different note pattern, or a lost or extra note.

## Every case

"shift" is the measured change in blocks; the timing tolerance is the 2026-09-29 sweep's, per case.

| case | shift | Δrms dB | env | centroid % | timing tol Δrms / env / centroid | class | shifted identical |
|---|---|---|---|---|---|---|---|
| analog_osc | 50 | -0.000 | 1.0000 | -0.0 | 1.67 / 1.000 / 43.3 | within | yes |
| community/12 Metalliche | 50 | -0.036 | 0.9979 | +0.1 | 0.09 / 0.998 / 1.2 | within | yes |
| community/1994 Su | 50 | -0.000 | 1.0000 | -0.0 | 0.45 / 0.837 / 8.7 | within | yes |
| community/8 O'Clock | 50 | -0.111 | 0.9804 | +1.4 | 1.50 / 0.827 / 14.3 | within | yes |
| community/80's Ffunk | 50 | -0.001 | 1.0000 | +0.9 | 0.00 / 1.000 / 1.0 | within | yes |
| community/A Busted Sqb | 50 | +0.841 | 0.4744 | -6.8 | 1.41 / 0.480 / 6.4 | within | yes |
| community/A Fm Bass Funk | 50 | +0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| community/A Fm Floot | 50 | +0.196 | 0.9829 | -0.6 | 0.34 / 0.966 / 5.9 | within | yes |
| community/A Fm Pad | 50 | -0.202 | 0.9845 | +1.7 | 2.20 / 0.798 / 32.7 | within | yes |
| community/A Ring Bass | 50 | -0.000 | 1.0000 | +0.0 | 0.30 / 0.998 / 9.8 | within | yes |
| community/Aberration | 50 | +0.000 | 1.0000 | -0.0 | 0.32 / 0.986 / 8.3 | within | yes |
| community/Analohania | 50 | +0.000 | 1.0000 | +0.0 | 0.77 / 0.733 / 2.6 | within | yes |
| community/Apex Stab | 50 | +0.018 | 0.9999 | +2.0 | 0.33 / 0.930 / 59.1 | within | yes |
| community/BASS Simple Distorted | 50 | -0.564 | 0.9755 | +3.4 | 0.80 / 0.979 / 8.8 | within | yes |
| community/Backdraft Pad | 50 | +0.015 | 1.0000 | -0.3 | 3.66 / 0.895 / 4.6 | within | yes |
| community/Bad Zwirlz Su | 50 | +0.058 | 0.9771 | -2.0 | 0.50 / 0.987 / 0.7 | within | yes |
| community/Basedland | 50 | +0.031 | 0.9559 | +0.1 | 0.41 / 0.975 / 4.5 | within | yes |
| community/Basic Bass | 50 | +0.399 | 0.9823 | -2.7 | 0.25 / 0.971 / 1.9 | within | yes |
| community/BassGuitar | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| community/Bell Fm Synthwave | 50 | +0.000 | 1.0000 | -0.0 | 0.99 / 0.710 / 10.3 | within | yes |
| community/Bird | 50 | +0.000 | 1.0000 | +0.0 | 3.38 / 0.966 / 11.7 | within | yes |
| community/Blade Vang | 50 | +0.089 | 0.5848 | +3.6 | 4.20 / 0.593 / 57.4 | within | yes |
| community/Boomf | 50 | +0.002 | 0.9912 | +6.7 | 0.82 / 0.958 / 39.2 | within | yes |
| community/Brick Speedboat | 50 | -0.529 | 0.9810 | -0.0 | 0.46 / 0.740 / 14.5 | within | yes |
| community/Broadcast | 50 | -0.771 | 0.6431 | +8.4 | 0.48 / 0.429 / 7.6 | within | yes |
| community/Brzcrtch | 50 | -0.000 | 1.0000 | -0.0 | 0.08 / 0.955 / 17.7 | within | yes |
| community/Celestial Su | 50 | +0.121 | 0.9502 | -2.8 | 0.33 / 0.920 / 5.8 | within | yes |
| community/Classic Pad | 50 | +0.764 | 0.9488 | +11.3 | 0.41 / 0.925 / 12.9 | beyond | yes |
| community/Cornwall | 50 | -0.004 | 0.9999 | +0.2 | 22.73 / 0.329 / 2266.9 | within | yes |
| community/Crime Time | 50 | +0.659 | 0.9994 | -1.7 | 0.71 / 0.975 / 6.3 | within | yes |
| community/Crumbling Pad | 50 | -0.188 | 0.9694 | +3.0 | 3.58 / 0.046 / 102.6 | within | yes |
| community/D-303 | 50 | -0.000 | 1.0000 | +0.0 | 0.01 / 0.971 / 0.3 | within | yes |
| community/Desparate Synth | 50 | +0.230 | 0.8712 | -4.8 | 0.36 / 0.872 / 14.9 | within | yes |
| community/Distorted Bass | 50 | +0.000 | 1.0000 | +0.0 | 0.60 / 0.994 / 13.8 | within | yes |
| community/Drop | 50 | +0.000 | 1.0000 | -0.0 | 0.05 / 0.999 / 4.1 | within | yes |
| community/Dystopian Sky | 50 | -0.027 | 0.9785 | +9.2 | 1.59 / 0.805 / 17.0 | within | yes |
| community/Echoy Melody | 50 | -0.013 | 0.9872 | -6.2 | 0.38 / 0.996 / 3.1 | within | yes |
| community/Farvoice | 50 | -0.041 | 0.9999 | -0.3 | 0.29 / 1.000 / 1.2 | within | yes |
| community/Faulty Circuit | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.2 | within | yes |
| community/Fauxtar | 50 | -0.001 | 0.9992 | +0.4 | 0.46 / 0.973 / 13.8 | within | yes |
| community/Flea Circuits Su | 50 | +0.002 | 1.0000 | -0.6 | 0.00 / 1.000 / 1.6 | within | yes |
| community/Fm Hammer String | 50 | +0.000 | 1.0000 | -0.0 | 0.49 / 0.997 / 7.8 | within | yes |
| community/Fm Leadandbass | 50 | -0.021 | 1.0000 | -0.1 | 2.83 / 0.686 / 20.8 | within | yes |
| community/Fm Pads | 50 | +0.132 | 0.9880 | +0.0 | 2.10 / 0.982 / 8.4 | within | yes |
| community/Fm Strimgs | 50 | -0.091 | 0.9878 | +4.2 | 0.79 / 0.915 / 17.1 | within | yes |
| community/Frances2 | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| community/Fuzzring | 50 | -0.000 | 1.0000 | -0.0 | 0.53 / 0.952 / 23.5 | within | yes |
| community/Fw Landscape | 50 | -0.021 | 0.9998 | -2.3 | 0.04 / 1.000 / 4.7 | within | yes |
| community/Giant Robot Fm | 50 | -0.015 | 0.9786 | +9.2 | 0.58 / 0.965 / 41.0 | within | yes |
| community/Glass Desert | 50 | +0.329 | 0.9683 | -4.0 | 3.60 / 0.601 / 17.0 | within | yes |
| community/Glass Pluck | 50 | +0.000 | 1.0000 | -0.0 | 0.45 / 0.998 / 2.4 | within | yes |
| community/Glistening | 50 | +0.000 | 1.0000 | +0.0 | 1.11 / 0.946 / 10.4 | within | yes |
| community/Gong | 50 | +0.075 | 0.9702 | -0.0 | 0.13 / 0.973 / 0.9 | within | yes |
| community/Growler | 50 | +0.004 | 1.0000 | +0.4 | 0.07 / 0.951 / 2.2 | within | yes |
| community/Growlstrom | 50 | +0.000 | 1.0000 | -0.0 | 0.07 / 0.990 / 1.9 | within | yes |
| community/Heartspark | 50 | -0.000 | 1.0000 | -0.0 | 0.09 / 0.978 / 0.6 | within | yes |
| community/Hollow Dark | 50 | +0.013 | 0.9966 | -0.7 | 1.09 / 0.935 / 3.5 | within | yes |
| community/Hollowring | 50 | -0.655 | 0.9400 | +0.3 | 1.39 / 0.870 / 12.3 | within | yes |
| community/Infernal Ring | 50 | +0.000 | 1.0000 | +0.0 | 0.47 / 0.907 / 7.8 | within | yes |
| community/J6 Pad | 50 | +0.360 | 0.9989 | -4.6 | 0.23 / 0.935 / 3.5 | within | yes |
| community/Jehorn | 50 | -0.065 | 0.9995 | +4.3 | 0.42 / 0.981 / 61.7 | within | yes |
| community/Juno Style | 50 | -0.108 | 0.9996 | +1.0 | 2.42 / 0.960 / 2.9 | within | yes |
| community/Kaleidoscope | 50 | -0.365 | 0.9591 | +3.9 | 0.57 / 0.890 / 11.2 | within | yes |
| community/Livewire Arp | 50 | -0.001 | 1.0000 | +0.2 | 0.79 / 0.952 / 27.2 | within | yes |
| community/MPE 80'S Soundtrack | 50 | -0.366 | 0.9650 | -12.6 | 0.52 / 0.960 / 21.9 | within | yes |
| community/MPE Curvilinear | 50 | -0.074 | 0.9999 | -1.4 | 0.65 / 0.959 / 7.3 | within | yes |
| community/MPE Desert Strata | 50 | -0.022 | 0.9977 | -3.8 | 0.23 / 0.781 / 91.7 | within | yes |
| community/MPE FM Growler | 50 | +0.013 | 0.9995 | +1.5 | 4.55 / 0.926 / 2062.1 | within | yes |
| community/MPE Flute | 50 | -0.008 | 0.9539 | +0.0 | 2.33 / 0.971 / 5.3 | within | yes |
| community/MPE Haunted Music Box | 50 | -0.246 | 0.8698 | +1.3 | 0.17 / 0.843 / 2.8 | within | yes |
| community/MPE Rich Saw Bass | 50 | -0.000 | 1.0000 | -0.0 | 2.17 / 0.884 / 30.2 | within | yes |
| community/MPE Super Saw | 50 | +0.000 | 1.0000 | -0.0 | 0.60 / 0.671 / 4.9 | within | yes |
| community/MPE Wood Pluck | 50 | -0.000 | 1.0000 | +0.0 | 0.10 / 1.000 / 0.2 | within | yes |
| community/Mean Pluck | 50 | +1.123 | 0.9924 | +3.0 | 0.69 / 0.999 / 12.4 | within | yes |
| community/Modello Uno | 50 | +0.198 | 0.9842 | -3.9 | 0.11 / 0.993 / 3.4 | within | yes |
| community/Mouse Tail | 50 | -0.042 | 0.9968 | -2.4 | 0.04 / 0.999 / 4.6 | within | yes |
| community/Nilsish | 50 | +0.000 | 1.0000 | +0.0 | 0.73 / 0.954 / 11.4 | within | yes |
| community/Noisewobble | 50 | -0.181 | 0.0387 | +1.1 | 0.55 / -0.174 / 3.8 | within | yes |
| community/Nowhere Home | 50 | +0.795 | 0.8825 | -5.9 | 1.94 / 0.801 / 41.5 | within | yes |
| community/Obassity | 50 | +0.040 | 0.9929 | +0.8 | 0.05 / 0.995 / 1.0 | within | yes |
| community/Oimp | 50 | +0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| community/Organic 1 | 50 | -0.131 | 0.8077 | -0.3 | 0.44 / 0.778 / 1.8 | within | yes |
| community/Orgavolution | 50 | +0.065 | 0.9998 | -1.1 | 0.05 / 1.000 / 0.8 | within | yes |
| community/Overloading | 50 | +0.401 | 0.9223 | -15.7 | 0.61 / 0.941 / 10.6 | within | yes |
| community/Pakmahn Su | 50 | +0.000 | 1.0000 | +0.0 | 0.16 / 0.926 / 3.8 | within | yes |
| community/Pianism I | 50 | +0.021 | 0.9996 | -2.0 | 0.89 / 0.850 / 4.8 | within | yes |
| community/Pomp | 50 | -0.016 | 0.9999 | +1.3 | 0.01 / 1.000 / 2.2 | within | yes |
| community/Pusher | 50 | +0.026 | 0.9999 | -0.2 | 1.57 / 0.914 / 12.1 | within | yes |
| community/Rainy Wasteland | 50 | +0.178 | 0.9921 | -0.2 | 1.58 / 0.815 / 3.1 | within | yes |
| community/Richy Rich | 50 | -0.000 | 1.0000 | +0.0 | 1.40 / 0.578 / 22.8 | within | yes |
| community/Right On | 50 | +0.009 | 1.0000 | +0.2 | 0.23 / 0.833 / 5.9 | within | yes |
| community/Ringmod Arp | 50 | +0.000 | 1.0000 | -0.0 | 0.12 / 0.980 / 1.2 | within | yes |
| community/Ringmod Melody | 50 | -0.083 | 1.0000 | -0.4 | 0.05 / 0.999 / 0.3 | within | yes |
| community/Ringmod Pad | 50 | +0.025 | 0.9997 | +0.1 | 0.67 / 0.670 / 4.1 | within | yes |
| community/Rubberdemon | 50 | -0.556 | 0.9856 | +6.5 | 2.31 / 0.949 / 56.3 | within | yes |
| community/Runner Lead | 50 | +0.475 | 0.8515 | -14.3 | 1.39 / 0.775 / 23.5 | within | yes |
| community/Sad And Hollow | 50 | +0.042 | 0.9937 | +0.8 | 0.30 / 0.982 / 1.8 | within | yes |
| community/Saturnine | 50 | -0.000 | 1.0000 | +0.0 | 0.46 / 0.997 / 5.2 | within | yes |
| community/Sawborg | 50 | +0.342 | 0.9981 | -4.0 | 0.49 / 0.979 / 16.1 | within | yes |
| community/Schizophonyx Rm | 50 | +0.028 | 0.9248 | -1.4 | 0.17 / 0.973 / 0.5 | within | yes |
| community/Sense Of Doom | 50 | -0.036 | 0.9321 | -7.8 | 3.23 / 0.265 / 194.5 | within | yes |
| community/Sik Rm | 50 | +0.000 | 1.0000 | +0.0 | 0.03 / 0.999 / 55.9 | within | yes |
| community/Slogro | 50 | -0.048 | 0.9891 | +0.1 | 2.91 / 0.254 / 12.4 | within | yes |
| community/Slow Hula Hoop | 50 | -0.089 | 0.9965 | +4.4 | 4.19 / 0.616 / 10.3 | within | yes |
| community/Space Bass | 50 | -0.101 | 0.9947 | +0.1 | 0.04 / 0.998 / 0.4 | within | yes |
| community/Speed Lead | 50 | +0.476 | 0.9989 | +2.2 | 4.45 / 0.741 / 62.4 | within | yes |
| community/Spilled Soup | 50 | -0.000 | 1.0000 | -0.0 | 0.01 / 1.000 / 0.2 | within | yes |
| community/Super Square | 50 | -0.744 | 0.8397 | +18.4 | 0.68 / 0.828 / 16.2 | within | yes |
| community/Sync Squared | 50 | +0.000 | 1.0000 | -0.0 | 0.01 / 0.993 / 0.2 | within | yes |
| community/Synthetic 1 | 50 | +0.046 | 0.9999 | +0.5 | 1.13 / 0.917 / 11.8 | within | yes |
| community/Tension | 50 | -0.000 | 1.0000 | +0.0 | 0.85 / 0.988 / 6.9 | within | yes |
| community/Thirsty Machine | 50 | +0.041 | 0.9998 | -1.1 | 0.27 / 0.961 / 7.2 | within | yes |
| community/Threat | 50 | -0.000 | 1.0000 | -0.0 | 0.35 / 0.999 / 0.1 | within | yes |
| community/Throaty | 50 | +0.347 | 0.9999 | +5.6 | 0.20 / 1.000 / 2.4 | within | yes |
| community/Thunder | 50 | +0.000 | 1.0000 | +0.0 | 1.97 / 0.478 / 69.7 | within | yes |
| community/Trust Me It's Useful Noise | 50 | +1.121 | 0.8295 | +11.5 | 5.78 / 0.771 / 195.3 | within | yes |
| community/Ttty | 50 | -0.109 | 0.8408 | +1.7 | 0.49 / 0.833 / 5.1 | within | yes |
| community/UK Dub Siren | 50 | -0.000 | 1.0000 | +0.0 | 0.12 / 0.993 / 1.6 | within | yes |
| community/Undulating Drone | 50 | -0.605 | 0.9418 | +9.1 | 1.12 / 0.693 / 9.1 | within | yes |
| community/Wall Of Static | 50 | +0.050 | 0.8969 | -3.7 | 0.34 / 0.781 / 2.4 | within | yes |
| community/Warble Harp Lead | 50 | -0.000 | 1.0000 | -0.0 | 1.55 / 0.973 / 4.7 | within | yes |
| community/Warm DX7 Pad | 50 | +0.004 | 0.9835 | +3.5 | 0.77 / 0.890 / 11.5 | within | yes |
| community/Wavedash | 50 | +0.000 | 1.0000 | -0.0 | 0.01 / 1.000 / 2.0 | within | yes |
| community/Weather | 50 | -1.373 | 0.9283 | +56.2 | 3.81 / 0.422 / 212.5 | within | yes |
| community/Wind Whistle | 50 | +0.000 | 1.0000 | +0.0 | 0.84 / 0.900 / 1.3 | within | yes |
| community/Woodeep | 50 | +1.574 | 0.9235 | -2.9 | 1.45 / 0.901 / 17.6 | within | yes |
| community/Working Engine | 50 | -0.002 | 0.9066 | -0.1 | 5.27 / 0.501 / 74.4 | within | yes |
| community/Worrisome | 50 | -0.000 | 1.0000 | +0.0 | 2.56 / 0.907 / 22.8 | within | yes |
| community/Wub Sync Bass | 50 | -0.018 | 1.0000 | +0.1 | 0.01 / 1.000 / 0.1 | within | yes |
| env_shape | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/000 Rich Saw Bass | 50 | +0.000 | 1.0000 | -0.0 | 2.18 / 0.885 / 30.0 | within | yes |
| factory/001 Sync Bass | 50 | +0.151 | 0.9955 | +2.5 | 4.35 / 0.962 / 50.8 | within | yes |
| factory/002 Basic Square Bass | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/003 Synthwave Bass | 50 | -0.018 | 1.0000 | -3.1 | 5.86 / 0.954 / 39.5 | within | yes |
| factory/004 Dubby Bass | 50 | -0.093 | 0.9997 | -0.0 | 0.05 / 1.000 / 0.3 | within | yes |
| factory/005 Sweet Mono Bass | 50 | -0.000 | 1.0000 | -0.0 | 2.08 / 0.792 / 27.6 | within | yes |
| factory/006 Vaporwave Bass | 50 | -0.000 | 1.0000 | +0.0 | 2.72 / 0.798 / 70.6 | within | yes |
| factory/007 Detuned Saw Bass | 50 | +0.002 | 0.9987 | -0.4 | 1.38 / 0.891 / 4.1 | within | yes |
| factory/008 FM Rich Distorted Bass | 50 | -0.000 | 1.0000 | +0.0 | 4.73 / 0.865 / 35.1 | within | yes |
| factory/009 Hoover Bass | 50 | +0.000 | 1.0000 | +0.0 | 3.35 / 0.858 / 74.8 | within | yes |
| factory/010 Gravel Basscamp | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/011 Dubstep Bass | 50 | +0.000 | 1.0000 | +0.0 | 0.01 / 0.991 / 0.4 | within | yes |
| factory/012 Blunt Sync Bass | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/013 Trap Bass 1 | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/014 Trap Bass 2 | 50 | -0.000 | 1.0000 | -0.0 | 0.01 / 0.997 / 0.7 | within | yes |
| factory/015 Resonant Filter Bass | 50 | -0.000 | 1.0000 | -0.0 | 0.03 / 0.999 / 0.5 | within | yes |
| factory/016 Dark Saturated Bass | 50 | -0.011 | 0.9771 | -0.5 | 1.10 / 0.789 / 49.2 | within | yes |
| factory/017 Impact Saw Lead | 50 | -0.001 | 1.0000 | +0.0 | 1.41 / 0.882 / 30.0 | within | yes |
| factory/018 Rich Saw Lead | 50 | +0.008 | 0.9997 | -0.2 | 0.22 / 0.818 / 3.4 | within | yes |
| factory/019 Fizzy Strings | 50 | -0.026 | 0.9948 | -1.0 | 0.10 / 0.992 / 4.8 | within | yes |
| factory/020 Soft Saw Lead | 50 | +0.003 | 1.0000 | -0.1 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/021 80s TV Lead | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/022 Rich Filter LFO Lead | 50 | +0.003 | 0.9996 | +0.4 | 1.10 / 0.732 / 19.7 | within | yes |
| factory/023 Analog Mono Wow | 50 | +0.066 | 0.9810 | -0.4 | 4.20 / 0.751 / 63.7 | within | yes |
| factory/024 Warble Bass Pluck | 50 | +0.128 | 0.9604 | -2.1 | 0.22 / 0.970 / 2.8 | within | yes |
| factory/025 Soft Synth Organ | 50 | +0.036 | 0.9995 | -0.4 | 0.09 / 0.997 / 1.1 | within | yes |
| factory/026 PW Organ | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/027 PW Envelope | 50 | +0.000 | 1.0000 | -0.6 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/028 PWM | 50 | +0.000 | 1.0000 | -5.1 | 0.00 / 1.000 / 3.6 | within | yes |
| factory/029 Chiptune Trill | 50 | +0.000 | 1.0000 | -0.0 | 0.03 / 0.998 / 0.3 | within | yes |
| factory/030 Distant Porta | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/031 Nasal Choir | 50 | +0.003 | 1.0000 | -0.3 | 0.12 / 0.998 / 6.7 | within | yes |
| factory/032 Bandpass Choir | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/033 Rich Square | 50 | -0.000 | 1.0000 | -0.0 | 0.60 / 0.951 / 14.1 | within | yes |
| factory/034 Square Choir | 50 | +0.174 | 0.9644 | -1.9 | 0.22 / 0.982 / 0.5 | within | yes |
| factory/035 Bell Lead & Bass | 50 | -0.000 | 1.0000 | +0.0 | 2.03 / 0.654 / 29.0 | within | yes |
| factory/036 Analog Ambient Square | 50 | +0.053 | 0.9980 | -0.5 | 0.04 / 0.999 / 0.2 | within | yes |
| factory/037 Echo Chord | 50 | -0.000 | 1.0000 | +0.0 | 1.00 / 0.848 / 4.0 | within | yes |
| factory/038 Vapor Arp | 50 | +0.189 | 0.7917 | -1.8 | 0.53 / 0.732 / 2.3 | within | yes |
| factory/039 Detuned Retriggering Saws | 50 | +0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/040 Spacer Leader | 50 | +0.158 | 0.2239 | -0.6 | 0.17 / 0.219 / 0.6 | within | yes |
| factory/041 Zithar - Vibed | 50 | +0.358 | 0.6319 | -0.6 | 5.86 / 0.154 / 29.3 | within | yes |
| factory/042 High Triangle | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/043 Square Porta | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/044 8-Bit Lead | 50 | +0.000 | 1.0000 | +3.3 | 0.00 / 1.000 / 2.0 | within | yes |
| factory/045 Square Sync | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/046 Saw Sync | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/047 Basic Dirty Bass | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/048 Thin Pulse Bass | 50 | +0.000 | 0.9984 | -0.2 | 0.30 / 0.964 / 2.0 | within | yes |
| factory/049 Basic FM | 50 | -0.000 | 1.0000 | +0.0 | 0.14 / 0.998 / 5.9 | within | yes |
| factory/050 FM Basic Bass | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/051 FM Rich Brass | 50 | +0.003 | 0.9989 | -0.2 | 2.02 / 0.957 / 40.4 | within | yes |
| factory/052 Soft FM | 50 | -0.000 | 1.0000 | -0.0 | 1.21 / 0.700 / 24.7 | within | yes |
| factory/053 Detuned FM Horns | 50 | +0.000 | 1.0000 | +0.0 | 0.77 / 0.871 / 9.5 | within | yes |
| factory/054 Ghostly Sines | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/055 FM Theremin | 50 | -0.000 | 1.0000 | -0.0 | 0.04 / 0.949 / 13.4 | within | yes |
| factory/056 FM Bell Modulation | 50 | +0.039 | 0.9946 | +2.4 | 0.80 / -0.222 / 2.8 | within | yes |
| factory/057 FM Lead | 50 | -0.002 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/058 FM Rising Attack | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/059 Distorted Lead Guitar | 50 | +0.003 | 1.0000 | +0.2 | 0.00 / 1.000 / 0.2 | within | yes |
| factory/060 Bass Guitar | 50 | +0.009 | 0.9995 | +0.3 | 0.04 / 1.000 / 1.0 | within | yes |
| factory/061 Blown-Staccato-Panpipes | 50 | -0.005 | 0.9997 | +0.1 | 0.29 / 0.997 / 3.6 | within | yes |
| factory/062 Trumpet | 50 | +0.000 | 0.9999 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/063 Tuba | 50 | +0.000 | 1.0000 | -0.0 | 2.23 / 0.917 / 99.6 | within | yes |
| factory/064 Reeds-Flute-Oboe | 50 | -0.127 | 0.9982 | -0.0 | 0.54 / 0.939 / 5.5 | within | yes |
| factory/065 Cello | 50 | +0.001 | 0.9993 | -0.0 | 0.03 / 0.999 / 0.7 | within | yes |
| factory/066 Violin | 50 | +0.000 | 0.9999 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/067 Marimba | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/068 FM Bells 1 | 50 | +0.002 | 1.0000 | -0.1 | 0.00 / 1.000 / 0.2 | within | yes |
| factory/069 FM Bells 2 | 50 | +0.002 | 1.0000 | -0.1 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/070 Glockenspiel | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/071 Rhodes | 50 | -0.000 | 1.0000 | -0.0 | 3.23 / 0.916 / 43.4 | within | yes |
| factory/072 Kyoto Phono | 50 | +0.000 | 1.0000 | -0.0 | 0.04 / 1.000 / 0.2 | within | yes |
| factory/073 Piano | 50 | -0.000 | 1.0000 | +0.0 | 3.70 / 0.971 / 48.7 | within | yes |
| factory/074 Electric Piano | 50 | -0.000 | 1.0000 | -0.0 | 0.47 / 0.910 / 6.9 | within | yes |
| factory/075 Electric Piano With Strings | 50 | -0.381 | 0.7805 | +2.0 | 1.49 / 0.749 / 8.7 | within | yes |
| factory/076 Organ | 50 | +0.000 | 1.0000 | -0.0 | 0.29 / 0.870 / 4.7 | within | yes |
| factory/077 FM Perc-Organ | 50 | +0.690 | 0.7308 | +0.3 | 0.79 / 0.581 / 9.6 | within | yes |
| factory/078 House | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/079 Phased Arper | 50 | -0.353 | 0.9103 | +1.8 | 3.54 / 0.789 / 27.2 | within | yes |
| factory/080 House | 50 | -0.000 | 1.0000 | -0.0 | 0.01 / 1.000 / 0.5 | within | yes |
| factory/081 Xylophone Big Bass | 50 | +0.000 | 1.0000 | +0.0 | 2.19 / 0.871 / 62.8 | within | yes |
| factory/082 Short Sharp Delay | 50 | +0.040 | 0.9999 | -1.0 | 2.54 / 0.965 / 39.4 | within | yes |
| factory/083 Dark Chorus | 50 | +0.016 | 0.9953 | +0.7 | 0.23 / 0.967 / 10.2 | within | yes |
| factory/084 FM Narrow Band | 50 | -0.000 | 1.0000 | +0.0 | 0.40 / 0.989 / 8.2 | within | yes |
| factory/085 Deep Fizz | 50 | -0.003 | 0.9999 | -0.0 | 0.01 / 0.989 / 0.1 | within | yes |
| factory/086 Techno Organ | 50 | +0.036 | 0.9790 | -0.4 | 0.39 / 0.955 / 17.2 | within | yes |
| factory/087 Define Leader | 50 | +0.010 | 0.9508 | +0.5 | 0.03 / 0.971 / 2.0 | within | yes |
| factory/088 Yelp Chords | 50 | +0.002 | 0.9956 | -6.2 | 2.30 / 0.942 / 69.3 | within | yes |
| factory/089 Degraded Retro Lead | 50 | +0.007 | 0.9803 | +0.0 | 0.04 / 0.981 / 1.2 | within | yes |
| factory/090 FM Organ | 50 | +0.002 | 0.9999 | -0.1 | 0.01 / 1.000 / 0.2 | within | yes |
| factory/091 FM Ricochet | 50 | +0.001 | 0.9985 | -0.3 | 0.78 / 0.957 / 18.2 | within | yes |
| factory/092 Degraded Tremolo | 50 | -0.043 | 0.9979 | +0.1 | 2.12 / 0.701 / 28.5 | within | yes |
| factory/093 FM Distorted Bells | 50 | +0.000 | 1.0000 | +0.0 | 0.02 / 0.999 / 5.9 | within | yes |
| factory/094 Ambient Occlusion Lead | 50 | -0.117 | 0.8803 | +1.0 | 0.68 / 0.792 / 9.2 | within | yes |
| factory/095 Harsh FM Feedback | 50 | +0.000 | 1.0000 | +0.0 | 0.33 / 0.977 / 7.9 | within | yes |
| factory/096 FM Guitar Power Chord | 50 | -0.001 | 0.9999 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/097 Saturated Filter | 50 | -0.000 | 1.0000 | -0.0 | 0.06 / 0.999 / 2.9 | within | yes |
| factory/098 Saturated Sync | 50 | +0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.7 | within | yes |
| factory/099 Overdrive Reese Sync | 50 | +0.249 | 0.9155 | -1.3 | 0.30 / 0.915 / 3.0 | within | yes |
| factory/100 Noise Lead | 50 | +0.000 | 1.0000 | -0.0 | 0.13 / 0.999 / 70.7 | within | yes |
| factory/101 Atebit | 50 | -0.007 | 0.9954 | +0.0 | 0.02 / 0.992 / 0.2 | within | yes |
| factory/102 Harsh 5th | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/103 Sci-fi Chaos | 50 | +0.000 | 1.0000 | +0.0 | 0.09 / 1.000 / 0.8 | within | yes |
| factory/104 Alien Vomit | 50 | +0.000 | 1.0000 | -0.0 | 0.01 / 0.997 / 0.4 | within | yes |
| factory/105 Attack Bass | 50 | +0.122 | 0.9930 | -0.2 | 0.35 / 0.969 / 13.2 | within | yes |
| factory/106 Hang Drum | 50 | -0.576 | 0.9292 | -4.6 | 1.41 / 0.885 / 5.8 | within | yes |
| factory/107 FM LPG Percussion | 50 | +0.136 | 0.9998 | +0.3 | 8.10 / 0.613 / 4.5 | within | yes |
| factory/108 Robo Arp | 50 | +0.001 | 1.0000 | -0.0 | 1.69 / 0.835 / 1.6 | within | yes |
| factory/109 Talking Arp | 50 | +0.787 | 0.9135 | -11.3 | 1.32 / 0.911 / 20.7 | within | yes |
| factory/110 Crystalline Ringmod | 50 | -0.263 | 0.9974 | -10.5 | 0.16 / 0.999 / 7.0 | within | yes |
| factory/111 Satellite Drum | 50 | +0.635 | 0.9395 | -11.4 | 0.90 / 0.925 / 6.9 | within | yes |
| factory/112 Hard Tech Beat | 50 | -0.018 | 0.9983 | -0.9 | 0.05 / 0.992 / 4.6 | within | yes |
| factory/113 Bio Lab | 50 | +2.365 | 0.7992 | -42.3 | 4.21 / 0.683 / 69.0 | within | yes |
| factory/114 Sootheerio | 50 | +0.006 | 0.9932 | -0.1 | 0.03 / 0.951 / 0.2 | within | yes |
| factory/115 Sounds After Take-off | 50 | +0.775 | 0.9644 | -0.3 | 2.20 / 0.757 / 14.4 | within | yes |
| factory/116 Evolving Frequencies | 50 | -0.008 | 0.5986 | -1.3 | 0.81 / 0.582 / 30.9 | within | yes |
| factory/117 Belledy | 50 | +0.020 | 0.9501 | +2.1 | 0.17 / 0.969 / 0.5 | within | yes |
| factory/118 Small Bridge Pad | 50 | -0.000 | 1.0000 | +0.0 | 0.02 / 1.000 / 2.0 | within | yes |
| factory/119 Stars Of The Bin Pad | 50 | -0.000 | 1.0000 | -0.0 | 1.04 / 0.973 / 2.0 | within | yes |
| factory/120 High Harsh Pad | 50 | +0.327 | 0.9876 | -0.6 | 0.51 / 0.863 / 7.4 | within | yes |
| factory/121 Tiny Lights | 50 | -0.088 | 0.9991 | +2.8 | 0.84 / 0.870 / 9.1 | within | yes |
| factory/122 Majestic Synth Orchestra | 50 | +0.018 | 0.8867 | +0.2 | 0.48 / 0.718 / 21.2 | within | yes |
| factory/123 Space Dust | 50 | -0.506 | 0.8632 | +8.0 | 1.79 / 0.899 / 15.8 | within | yes |
| factory/124 Filter Modulation Pad | 50 | +0.000 | 1.0000 | -0.0 | 0.05 / 1.000 / 0.2 | within | yes |
| factory/125 Evolving Pad | 50 | -0.053 | 0.9582 | -0.2 | 0.04 / 0.977 / 1.0 | within | yes |
| factory/126 Dark FM Pad | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/127 Alien Larvae | 50 | +0.364 | 0.7949 | -3.9 | 0.34 / 0.816 / 9.1 | within | yes |
| factory/128 Lunar Landing | 50 | -0.277 | 0.9193 | +1.5 | 0.09 / 0.814 / 2.1 | beyond | yes |
| factory/129 Sci-fi Scenic | 50 | -0.469 | 0.9181 | +8.6 | 1.15 / 0.735 / 6.1 | within | yes |
| factory/130 Dark Strings | 50 | -0.001 | 0.9979 | -0.1 | 2.37 / 0.644 / 35.6 | within | yes |
| factory/131 Warm Strings | 50 | -0.178 | 0.9853 | -0.8 | 0.15 / 0.990 / 1.4 | within | yes |
| factory/132 Organ Strings | 50 | -0.001 | 0.9999 | +0.0 | 0.02 / 1.000 / 0.4 | within | yes |
| factory/133 80s Strings | 50 | +0.394 | 0.6502 | -2.9 | 0.60 / 0.288 / 20.0 | within | yes |
| factory/134 Melody String | 50 | -0.000 | 1.0000 | -0.0 | 1.38 / 0.588 / 25.4 | within | yes |
| factory/135 Soothing Growth Pad | 50 | +0.000 | 1.0000 | -0.0 | 5.55 / 0.303 / 27.5 | within | yes |
| factory/136 Synthwave Pad | 50 | +0.083 | 0.5144 | -4.5 | 1.30 / 0.520 / 17.6 | within | yes |
| factory/137 Epic Saw Modulation Pad | 50 | -1.002 | 0.4435 | +14.5 | 1.20 / 0.507 / 10.8 | beyond | yes |
| factory/138 Brassy Pad | 50 | -0.000 | 1.0000 | +0.0 | 1.40 / 0.815 / 19.9 | within | yes |
| factory/139 Detuned Saw Pad | 50 | -0.000 | 1.0000 | -0.0 | 2.26 / 0.948 / 26.4 | within | yes |
| factory/140 Slow Aural Swells | 50 | -0.097 | 0.7836 | +1.2 | 1.23 / 0.764 / 196.5 | within | yes |
| factory/141 Ringmod Pad | 50 | +0.000 | 1.0000 | +0.0 | 0.32 / 0.998 / 30.8 | within | yes |
| factory/142 Phaser | 50 | -0.275 | 0.9995 | +4.5 | 0.15 / 1.000 / 3.4 | within | yes |
| factory/143 Chillout Pad | 50 | +0.024 | 0.9483 | +0.5 | 0.08 / 0.962 / 1.0 | within | yes |
| factory/144 Sweep Chords | 50 | +0.087 | 0.9858 | -6.1 | 0.60 / 0.842 / 24.3 | within | yes |
| factory/145 Eerie High Pad | 50 | -0.004 | 0.9997 | -0.1 | 0.00 / 1.000 / 0.1 | within | yes |
| factory/146 Atmospheric Squares Pad | 50 | +0.004 | 1.0000 | -0.0 | 0.24 / 0.986 / 4.3 | within | yes |
| factory/147 Resonant Filter Pad | 50 | -0.032 | 0.9999 | -0.4 | 0.32 / 0.623 / 3.2 | within | yes |
| factory/148 Warm 5th Pad | 50 | +0.179 | 0.9281 | -6.6 | 2.98 / 0.763 / 30.6 | within | yes |
| factory/149 Cold 5th Pad | 50 | -0.034 | 0.9279 | -2.6 | 0.78 / 0.905 / 9.2 | within | yes |
| factory/150 Vaporwave Pad | 50 | +0.240 | 0.9394 | -5.2 | 0.15 / 0.962 / 2.5 | within | yes |
| factory/151 Radiant FM Pad | 50 | +0.000 | 1.0000 | -0.0 | 0.24 / 0.943 / 4.0 | within | yes |
| factory/152 Small Jet Pad | 50 | -0.000 | 1.0000 | -0.0 | 0.64 / 1.000 / 1.7 | within | yes |
| factory/153 FM Modulation Pad | 50 | +0.188 | 0.9671 | +7.3 | 0.59 / 0.961 / 9.8 | within | yes |
| factory/154 Rich FM Pad 1 | 50 | +0.000 | 1.0000 | +0.0 | 0.15 / 0.725 / 2.4 | within | yes |
| factory/155 Rich FM Pad 2 | 50 | +0.003 | 0.9991 | -0.1 | 0.91 / 0.938 / 18.9 | within | yes |
| factory/156 Rich FM Pad 3 | 50 | +0.013 | 0.9685 | +0.4 | 0.06 / 0.990 / 1.4 | within | yes |
| factory/157 Rich FM Pad 4 | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| factory/158 Tempo-Synced LFO | 50 | -0.132 | 0.8804 | -2.3 | 2.38 / 0.827 / 29.2 | within | yes |
| factory/159 80s Bass Rhythm | 50 | +0.235 | 0.9998 | -2.2 | 0.81 / 0.980 / 2.4 | within | yes |
| factory/160 Synthwave Bass Arp | 50 | +0.000 | 1.0000 | -0.0 | 2.68 / 0.716 / 54.0 | within | yes |
| factory/161 Synthwave Vibrato Arp | 50 | +0.232 | 0.9682 | -0.9 | 0.98 / 0.845 / 31.1 | within | yes |
| factory/162 Busy Arp | 50 | +1.069 | 0.8533 | +2.0 | 2.40 / 0.394 / 15.3 | within | yes |
| factory/163 Crisp Pop Arp | 50 | +0.015 | 0.9970 | +0.4 | 0.03 / 0.995 / 0.4 | within | yes |
| factory/164 Study Arp | 50 | +0.394 | 0.9127 | +6.8 | 0.35 / 0.906 / 7.0 | within | yes |
| factory/165 Acid Arp | 50 | +0.012 | 0.9880 | +0.4 | 1.07 / 0.883 / 89.3 | within | yes |
| factory/166 Harpsichord Cyborg | 50 | -0.046 | 0.9981 | +0.1 | 0.91 / 0.960 / 3.5 | within | yes |
| factory/167 FM Metallic Bass Arp | 50 | +0.000 | 1.0000 | +0.0 | 0.04 / 1.000 / 0.1 | within | yes |
| fm_2op | 50 | -0.000 | 1.0000 | +0.0 | 0.33 / 1.000 / 21.3 | within | yes |
| fm_feedback | 50 | +0.000 | 1.0000 | +0.0 | 0.66 / 1.000 / 14.5 | within | yes |
| input_mono | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| input_pitchshift | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| input_stereo | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| lfo_vibrato | 50 | +0.001 | 0.9999 | +0.4 | 0.00 / 1.000 / 0.2 | within | yes |
| lpf_24db_env2 | 50 | +0.000 | 1.0000 | +0.0 | 0.10 / 0.998 / 25.8 | within | yes |
| mono_legato | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.2 | within | yes |
| noise_only | 50 | -0.000 | 1.0000 | -0.0 | 0.06 / 0.998 / 0.7 | within | yes |
| pitch_bend | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 0.999 / 0.0 | within | yes |
| poly_chord | 50 | -0.000 | 1.0000 | -0.0 | 0.01 / 0.962 / 0.3 | within | yes |
| porta | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| ringmod | 50 | +0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.4 | within | yes |
| sample/linear | 61 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| sample/mono | 61 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| sample/reversed | 61 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| sample/smooth_sweep | 61 | +0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| sample/timestretch | 61 | +0.000 | 1.0000 | +0.0 | 3.37 / 0.739 / 69.4 | within | yes |
| saw_basic | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| sine_basic | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| square_basic | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| square_pw | 50 | -0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| square_pw_low | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.4 | within | yes |
| svf_band | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| triangle_basic | 50 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
| two_osc_detune | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 0.999 / 0.9 | within | yes |
| unison_spread | 50 | -0.000 | 1.0000 | -0.0 | 0.49 / 0.986 / 33.9 | within | yes |
| velocity_dynamics | 50 | +0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.3 | within | yes |
| wavefold_sine | 50 | -0.000 | 1.0000 | +0.0 | 0.00 / 1.000 / 0.0 | within | yes |
| wavetable/morph_lfo | 61 | -0.187 | 0.9830 | +1.3 | 0.12 / 0.996 / 0.1 | within | yes |
| wavetable/morph_note | 61 | -0.800 | 0.9831 | +32.8 | 1.33 / 0.990 / 47.2 | within | yes |
| wavetable/morph_sync | 61 | +0.121 | 0.9574 | +1.3 | 0.35 / 0.880 / 29.9 | within | yes |
| wavetable/scw | 61 | -0.000 | 1.0000 | +0.0 | 0.55 / 0.960 / 7.4 | within | yes |
| wavetable/scw_hi | 61 | -0.000 | 1.0000 | +0.0 | 0.27 / 0.992 / 6.8 | within | yes |
| wavetable/static | 61 | -0.109 | 0.9999 | +1.6 | 0.08 / 1.000 / 0.6 | within | yes |
| wavetable/static_lo | 61 | -0.106 | 0.9999 | +2.1 | 0.06 / 0.836 / 0.7 | within | yes |
| wavetable/sync | 61 | +0.068 | 0.9649 | -0.6 | 0.46 / 0.878 / 35.7 | within | yes |
| wavetable_basic | 61 | +0.000 | 1.0000 | -0.0 | 0.00 / 1.000 / 0.1 | within | yes |
