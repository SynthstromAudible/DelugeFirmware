# Golden data

What the tests compare with, one directory per consumer. The corpus runners that read and record part of it are
in `tests/integration/corpus/` (its README says how they run).

## Layout

One directory per consumer: what reads it, and which registry entry re-records it (`./dbt test --update <entry>`).
C++ specs name a golden by its path under `GOLDEN_ROOT` (`tests/support/cmake/golden.cmake`), e.g.
`ui/gfx/oled/icons.pbm`; Python checks by its repository path, and the corpus runners through `driver.GOLDEN`.

| Directory | Holds | Read by | Re-recorded by |
| --- | --- | --- | --- |
| `ui/gfx/` | the gfx primitives' OLED panels (`oled/`) and grid states (`grid/`) | the gfx specs | `unit` |
| `instrument/` | the instrument's answers about every sound of the engine × oscillator-kind matrix, hashed per sound and family (`record_reference.tsv`) | `tests/unit/instruments/classic/composed_state_spec.cpp` | `unit` |
| `ui/shell/` | the shell's popup and overlay panels | the shell specs | `unit` |
| `ui/widgets/` | the UI widgets' panels and grid states | the ui specs | `unit` |
| `ui/shell/<script>/` | each level-2 script's snapshots (`tests/ui-scripts/shell/`) | `expect_script_goldens`, in the unit specs and `tests/integration/ui/scenes` | `unit`, `ui-scenes-spec` |
| `ui/menu/`, `ui/scenes/` | the menu's and scenes' panels | `tests/integration/ui/scenes` | `ui-scenes-spec` |
| `ui/app/<script>/` | each level-3 app script's snapshots (`tests/ui-scripts/app/`) | `tests/integration/checks/ui_scripts.py` | `ui-scripts` |
| `ui/menu_oracle/` | upstream `main`'s menu answers (below) | `tests/integration/ui/scenes/support/oracle_table.h`, `oracle_files.h` | nothing: rows change by hand to follow `main` |
| `ui/audio_clip_oracle/` | legacy `AudioClipView`'s frames for each differential case (`<case>.txt`, format in `support/differential/frozen.h`), frozen from the live view before it was deleted | `tests/integration/ui/scenes/audio_clip_differential_spec.cpp` | nothing: frames change only to follow `main` (the spec's freezer example ran once, with `AUDIO_CLIP_ORACLE_OUT`) |
| `render/` | the synth golden masters' presets (`presets/`, made by `presets/make_corpus.py`) and their records (`presets.json`); the render-song generator (`songs/make_render_song.py`) and its records (`songs.json`); synthetic wavetables (`wavetable_pins/`); `compare_renders.py`, which compares two sets of renders | `render_preset.py`, `render_song.py`, `render_card_cache.py`, `streaming_margin.py`, `param_dump.py`, `tools/audible_window/compare.py`, `load_policy.card`, the dsp band-builder and FFT specs, `engine_settings_run.py`, `upgrade_run.py` | `golden-synth` (`presets.json`), each render-song entry (`songs.json`) |
| `songs/` | save(load(song)) hashes (`fixtures.json`) and the committed synthetic songs (`synthetic/`) | `songs_run.py`, `upgrade_run.py`, `model_decode_diff_run.py`, `model_equality_synthetic` | `song-roundtrip` |
| `presets/` | save(load(preset)) hashes (`fixtures.json`) | `presets_run.py`, `upgrade_run.py` | `preset-roundtrip` |
| `cable_decode/` | each file's decoded cables, fingerprinted (`fixtures.json`) | `cable_decode_run.py` | `cable-decode` |
| `param_record/` | each song's recorded lanes, their undo and redo, and its reload (`<song>.txt`) | `param_record_run.py` | `param-record-diff` |
| `audio_clip_edit/` | each song's audio-clip descriptor edits (`<song>.txt`) | `audio_clip_edit_run.py` | `audio-clip-edit-diff` |
| `arrangement_take/` | the arrangement takes' lanes and render (`RS_ARRANGE_TAKE.txt`) | `arrangement_take.py` | `arrangement-take` |
| `upgrade/` | the upgrader's legacy fixtures (`<group>/<name>.<kind>.XML`, each oracle beside its fixture, made by `make_fixtures.py`) and their hashes (`fixtures.json`, keyed by path under `tests/golden/`) | `upgrade_run.py` | `upgrade-differential` |
| `stem_export/` | the stem export's render hash and size (`<fixture>.sha256`) | `stem_export.py` | by hand only (`stem_export.py` says how) |

Record headers stay beside the specs that compile them; their entries list them in `baseline` too, and the loader
accepts any `tests/**/*_records.h` there by its name.

## Menu reference tables (`ui/menu_oracle/`)

Upstream `main`'s menu answers, one row per answer, which the menu's scenes specs check it against
(`tests/integration/ui/scenes/support/oracle_table.h` reads them; every spec that reads a table also checks it asked every
row). A row is `path<TAB>column<TAB>values…`: the path names what answered (a menu node by its child positions
under its root, a setting case, a shortcut cell), the column the question, the values the answer. `pixels/` holds
panels as PBM, `pads/` the main grid's pad images: eight lines from the top row (y = 7) down, each sixteen `r,g,b`
triples separated by spaces (`tests/integration/ui/scenes/support/oracle_files.h` reads both, and a spec that reads them checks it
read every file under its own prefix).

Fields are raw bytes: only tab, newline and backslash are escaped, so a glyph byte such as the flat sign (0x81 in
`menu_ranges.tsv`) is stored as it is and the files are not always valid UTF-8.

These are reference data, not recordings: nothing in the tree regenerates them. Change a row only to follow `main`,
reading its source with `git show origin/main:<path>`, and name that source in the commit.

The rows were frozen from `next`'s legacy menu at 16b2d012b, before it was deleted, and audited against `origin/main`
96c002803 by reading code, not by regenerating them (nothing can produce the rows from `main`): every one of the 86
`fix` and `feat` commits since the merge base 4cdf49002 that touches the menu items, `gui/l10n` or the runtime
settings was reviewed, the other 114 such commits were scanned for changes to titles, options, ranges, relevance and
learn, and a sample of items was checked against `main`'s source. Refactors were assumed to preserve behaviour where
their diffs changed no answer. Rows changed since the freeze followed `main` (`b0aee86a8`, `cb63476d1`). The rows hold
`main`'s answer except where they differ from `main` and the difference is accepted, each a row of
`tests/ui-scripts/parity/DIFFERENCES.md`: the sample repeat titles (D149), the delay's three types (D155), the
community features `main` has and `next` removed (D156) and the audio clip attack's write (D157).

Where an answer is a model value main's item left behind rather than text the item returned, the row is that value as
the port's `bindings::read` reads it after main's write: the first value of every `write/<v>` row, and the
`sweep/<path>` rows (each drum's value after main's affect-entire sweep). The second value of a `write/<v>` row is
main's own read of the item.
