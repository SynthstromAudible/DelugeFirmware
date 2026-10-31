# The audible window's equivalence harness

The audible window is the one planned change of the Deluge instrument's sound: several changes land on a holding branch
and the golden masters are re-recorded once, at the end. This harness is the evidence for that re-recording. It renders
every golden and a set of real songs with their per-block parameters, and reports what a candidate build changed against
a reference taken at the window's base. Kate reads the report and accepts it before the goldens are re-recorded; the
harness passes or fails nothing about the sound itself.

## What it records

- **Audio:** each render's output, as `render_preset` keeps it (`RENDER_PRESET_KEEP`) and as `render_song` writes it.
- **Parameters:** a host build dumps every matrix pass's final values and each voice's source states to the file
  `RENDER_PARAM_DUMP` names (`src/deluge/processing/sound/param_dump.h`). Each record has the audio sample timer, the
  sound (numbered by first appearance), the scope and the voice's note-on serial, so two builds' records line up.
  Device builds compile the dump to nothing. The `param-dump` gate checks that dumping leaves a render byte-identical.

## The commands

`tools/audible_window/audible_window.py`:

| Command | Does |
|---|---|
| `render --out DIR [--filter a,b]` | Renders every `golden-synth` case (or those whose names hold a filter word) and the song set, 30 s each, into `DIR`: `presets/<case>.pcm` and `.npz`, `songs/<song>.pcm` and `.npz`, and `manifest.json` (the commit, what rendered, what failed). |
| `songs` | Prints the song set: `Glitchy_2`, then the corpus songs with the most patch cables, kit rows and automated params. |
| `compare REF NEW --report FILE` | Writes the report (`FILE`, and `FILE.json` beside it with every number). |
| `identity [REF]` | Compares the reference with itself; anything not identical means the harness or the snapshot is broken. |

The corpus is `DELUGE_SONG_CORPUS`, else `.superpowers/estate/corpus-upgraded/SONGS`. The analyser needs numpy, which
the toolchain's Python installs from `scripts/toolchain/requirements.txt`.

## The reference snapshot

Taken once, from the commit the window starts on:

```sh
python3 tools/audible_window/audible_window.py render --out .superpowers/estate/audible-window/reference/<commit>
./dbt test audible-window-identity
```

Retake it only if the window's base moves. Each candidate is then rendered to a directory of its own and compared:

```sh
python3 tools/audible_window/audible_window.py render --out <candidate dir>
python3 tools/audible_window/audible_window.py compare .superpowers/estate/audible-window/reference/<commit> \
    <candidate dir> --report <candidate dir>/report.md
```

## Reading the report

1. **Threshold flips** come first: branches the voice takes on one side of a threshold, so a tiny change can flip them.
   They are the slow-attack hold (an envelope's attack above 245632), the noise and FM modulator volume gates, a source
   cut off above 22.05 kHz, the sample-cache test over the pitch cables, and clipping.
2. **Changed renders**, loudest difference first: the null-test level (the RMS of the aligned difference), the onset
   shift, the RMS and peak changes, the envelope and waveform correlations and the spectral centroid shift.
3. **Moved parameters** per render (largest LSB change, the first time it differs, how many blocks differ) and a table
   across every render per parameter. A parameter can move without the audio changing.
4. **Voice lifetimes:** voices that start or end at another time.
5. **Missing** and **identical** renders.

Parameters and flips are compared at the records both builds have, matched by time and by order among records at the
same time, so a voice that ends earlier is reported once as a lifetime change.

## Accepting

When the report is accepted, the goldens are re-recorded with `RENDER_PRESET_UPDATE=1 ./dbt test golden-synth
golden-synth-arm`, and the commit names the report it was accepted from.
