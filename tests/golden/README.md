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
python3 tests/golden/songs_run.py --update             # discover+re-baseline (walks the corpus)
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
`--update` (it walks the corpus, runs the driver, and writes a fixture for every
song that settles; songs that don't settle are skipped, not committed). Aim for
spread across formats (legacy tag-format **and** modern attribute-format),
firmware versions, and features (synth/kit/MIDI/CV, arrangement, automation,
scales). Re-baselining after an intended change is the same `--update`.

### Synthetic fixtures (committed, for rare structures)

Some structures never appear in a given BYO corpus (e.g. a **sync-scale clip**),
so a read for them would be golden-blind. For those, commit a small,
privacy-clean hand-authored song under `tests/golden/synthetic/`. Its fixture key
is prefixed `synthetic/` and resolves against this directory rather than the
corpus, so it is **always present** — it runs (and gives real coverage) even in
CI with no corpus, instead of *Skipped*. `--update` re-baselines these alongside
the corpus. To make one: author a minimal song that exercises the structure, run
it through the driver until it *settles* (exit 0), commit the emitted `gen1`, and
`--update` (or add its `gen1` SHA-256 under a `synthetic/<name>.XML` key).
