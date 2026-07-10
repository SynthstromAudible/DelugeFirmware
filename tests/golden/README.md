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

`song_roundtrip` (built by the host-sim) loads each song **headless** — no audio
engine, display hardware, USB, or SD; just the allocator + a stub display. The
firmware's load↔execution couplings are gated, not faked (AudioEngine yields via
`FileReader::callRoutines`; view selection cleared before save). It emits `gen1`;
the runner hashes it (SHA-256) and compares to `songs_fixtures.json`.

```sh
cmake -B build-sim -S sim -G Ninja
cmake --build build-sim --target song_roundtrip
export DELUGE_SONG_CORPUS=~/path/to/SONGS              # your songs dir (BYO, see below)
python3 tests/golden/songs_run.py                      # check fixtured songs (exit 1 on regression)
python3 tests/golden/songs_run.py --update             # discover+re-baseline (walks the corpus)
python3 tests/golden/songs_run.py --filter Bass        # subset (substring on path)
ctest --test-dir build-sim -R song_roundtrip           # same, via CTest
```

In check mode the runner iterates the **committed fixtures** (the contract) and
verifies each song that's present locally; a song that no longer settles to its
committed hash is a `REGRESS` (exit 1). Songs not present locally are `ABSENT`
(skipped). A driver exit means: `0` settled · `1` load error · `3` fixed-point
mismatch.

### Corpus is bring-your-own (out-of-repo)

Only the **hashes** (`songs_fixtures.json`) are committed; the song `.XML` files
stay out-of-repo (privacy/licensing). Point the runner at your songs via
`$DELUGE_SONG_CORPUS` or `--corpus` (default `tests/golden/songs/`, gitignored).
Fixtures are keyed by path **relative to the corpus root**, so anyone with the
same songs at the same relative layout can verify the exact same hashes.

When none of the fixtured songs are present, the check **SKIPs** (exit 77; CTest
reports *Skipped*) — so CI / contributors without the corpus stay green while the
gate still protects anyone who has the songs.

### The ARM-NEON oracle

x86 runs SIMDe; firmware runs NEON. Fixtures are only trustworthy if both agree
(serialization is mostly integer/string formatting, so divergence is unlikely —
but it's cheap insurance against width/format bugs):

```sh
cmake -B build-sim-arm -S sim -G Ninja -DCMAKE_TOOLCHAIN_FILE=$PWD/sim/arm-linux-toolchain.cmake
cmake --build build-sim-arm --target song_roundtrip
python3 tests/golden/songs_run.py --binary build-sim-arm/song_roundtrip \
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
