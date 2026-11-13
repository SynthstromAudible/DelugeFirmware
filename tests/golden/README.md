# Golden harnesses (host)

## Song serialization round-trip (`songs_run.py`)

Regression net for the **project-model extraction** (Phase 0 — see
[docs/dev/project_model_extraction.md](../../docs/dev/project_model_extraction.md)).
It pins the *serialization behaviour* of `Song` so any accidental change to what
gets written to disk during the extraction is caught.

### What it checks — the canonical fixed point

`Song::writeToFile` does not reproduce the input byte-for-byte (it upgrades
legacy → modern format, fills defaults, normalises order). So the invariant is a
fixed point, not "output == input":

```
gen1 = save(load(original))
gen2 = save(load(gen1))
require gen1 == gen2            # the driver asserts this ("settled")
fixture = sha256(gen1)          # committed in songs_fixtures.json
```

The committed hash is the regression anchor: a refactor that changes serialized
output breaks it and must be justified + re-baselined.

### How it runs

`song_roundtrip` is a host driver (tests/integration/scenarios/drivers/): golden_vt_render runs it on the
booted app as the `song_roundtrip` scenario, one song per run, from the state the driver's own process once
started in (no current song, the random streams at their seeds, the settings unread). The song is read from the
host file and loaded through the Value tree beside the booted one; view selection is cleared before save. It
emits `gen1`; the runner hashes it (SHA-256) and compares to `songs_fixtures.json`.

```sh
./dbt test -n song-roundtrip                           # build golden_vt_render
export DELUGE_SONG_CORPUS=~/path/to/SONGS              # your songs dir (BYO, see below)
python3 tests/golden/songs_run.py                      # check fixtured songs (exit 1 on regression)
./dbt test --update song-roundtrip                    # re-record (builds first; tests/README.md)
python3 tests/golden/songs_run.py --filter Bass        # subset (substring on path)
./dbt test song-roundtrip                              # same, as the registry gate
```

In check mode the runner iterates the **committed fixtures** (the contract) and
verifies each song that's present locally; a song that no longer settles to its
committed hash is a `REGRESS` (exit 1). Songs not present locally are `ABSENT`
(skipped). A driver exit means: `0` settled · `1` load error · `3` fixed-point
mismatch.

### Corpus is bring-your-own (out-of-repo)

Only the **hashes** (`songs_fixtures.json`) are committed; the song `.XML` files
stay out-of-repo (privacy/licensing). Point the runner at your songs via
`$DELUGE_SONG_CORPUS` or `--corpus` (default `$DELUGE_BACKUP/SONGS`, else `~/Deluge Backup/SONGS`).
Fixtures are keyed by path **relative to the corpus root**, so anyone with the
same songs at the same relative layout can verify the exact same hashes.

Fixtured songs that are not present are *ABSENT* and skipped, so CI and contributors
without the corpus still check the committed synthetic songs, while the gate protects
anyone who has the rest.

### The ARM-NEON oracle

x86 runs SIMDe; firmware runs NEON. Fixtures are only trustworthy if both agree
(serialization is mostly integer/string formatting, so divergence is unlikely —
but it's cheap insurance against width/format bugs):

```sh
./dbt test -n golden-synth-arm                         # the qemu-arm build of golden_vt_render
python3 tests/golden/songs_run.py --binary <that build's golden_vt_render> \
    --wrapper 'qemu-arm -L /usr/arm-linux-gnueabihf -cpu cortex-a9'
```

### Adding coverage

`songs_fixtures.json` maps each settled song (path relative to the corpus root)
to its `gen1` SHA-256. To add coverage: put songs in your corpus dir and run
`./dbt test --update song-roundtrip` (it walks the corpus, runs the driver, and writes a fixture for every
song that settles; songs that don't settle are skipped, a fixtured song your corpus lacks keeps its fixture, and a
fixtured song that no longer settles fails the update). Aim for
spread across formats (legacy tag-format **and** modern attribute-format),
firmware versions, and features (synth/kit/MIDI/CV, arrangement, automation,
scales). Re-recording after an intended change is the same command.

### Synthetic fixtures (committed, for rare structures)

Some structures never appear in a given BYO corpus (e.g. a **sync-scale clip**),
so a read for them would be golden-blind. For those, commit a small,
privacy-clean hand-authored song under `tests/golden/synthetic/`. Its fixture key
is prefixed `synthetic/` and resolves against this directory rather than the
corpus, so it is **always present** — it runs (and gives real coverage) even in
CI with no corpus, instead of *Skipped*. `./dbt test --update song-roundtrip` re-records these alongside
the corpus. To make one: author a minimal song that exercises the structure, run
it through the driver until it *settles* (exit 0), commit the emitted `gen1`, and
re-record it with `./dbt test --update song-roundtrip` (or add its `gen1` SHA-256 under a `synthetic/<name>.XML` key).

## Menu reference tables (`ui/menu_oracle/`)

Upstream `main`'s menu answers, one row per answer, which the ported menu's scenes specs check it against
(`sim/scenes_spec/support/oracle_table.h` reads them; every spec that reads a table also checks it asked every
row). A row is `path<TAB>column<TAB>values…`: the path names what answered (a menu node by its child positions
under its root, a setting case, a shortcut cell), the column the question, the values the answer. `pixels/` holds
panels as PBM, `pads/` the main grid's pad images: eight lines from the top row (y = 7) down, each sixteen `r,g,b`
triples separated by spaces (`sim/scenes_spec/support/oracle_files.h` reads both, and a spec that reads them checks it
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
