# One test tree, one registry, one runner

Status: approved 2026-09-28, with the §11 proposals taken as decided. Steps 1 to 3½ done, except 3½'s pin move,
which waits on the deluge-sdk change reaching its remote.
- Branch `feat/rung3-browser-listing`. Follows the testing audit of the same day (conversation) and the fixes it led to
  (`1d32a55a8` … `7bc73c7bf`).
- Lands after this branch merges into `next`, and before `feat/synth-golden-masters` is merged onto it, so the golden
  masters arrive in this structure instead of beside it (§9).
- Every step keeps the harness output identical to before it; the evidence for each step is an estate capture diffed
  against the one taken before the step (§10).

## 1. Why

The tests grew rung by rung, so they are organised by where each piece happened to be built, not by what it tests.

**Two unrelated entry points, neither complete.** `./dbt test` builds and runs the ctest suites (`tests/unit`,
`tests/spec*`). `./dbt harness run|ci` runs the 38 registry entries. Neither covers `tests/qemu`, the crates' own
`cargo test`s, the BSP host tests, or the standalone crates, so there is no command that runs everything, and CI lists
what it runs by hand. That is how `tests.yml` kept running two binaries no build had produced for months (removed in
`1d32a55a8`).

**"Harness" names how a test is built, not what level it tests.** The registry holds integration tests (the scenario
gates: the whole app booted on the Embassy host BSP, driven by real gestures), component tests against a reference
(`fs-differential`, `region-*-differential`), unit-scope stress tests (`snapshot-primitive-tsan`), a cross-architecture
parity check (`dsp-diff`) and fuzzers. "Run the integration tests" has no answer today.

**Scenario code is compiled into the firmware.** The 24 scenarios (57 files, about 24,000 lines) live in
`src/deluge/harness/`, and `src/deluge/CMakeLists.txt` globs every `.cpp` under `src/deluge/` into the app. The device
build compiles them: `staging_release_scenario.cpp` broke the RelWithDebInfo build today by calling a host-only knob.
Most scenarios are wrapped in `#ifdef DELUGE_HOST`, which is the workaround, not the design.

**Each gate is written out five times.** Adding the pattern-load gate took edits in five places:
- the scenario `.cpp` and `.h`
- a copy-pasted 60-line async task in `golden_vt_render/src/main.rs`, which has 24 of them in 3,214 lines, plus a
  mode flag and an `if/else` arm, 23 of each
- a copy-pasted driver script in `scripts/` (23 of them) that builds, runs `GOLDEN_SCENARIO=<name>` and checks the
  markers appeared
- a registry entry, whose README table is then regenerated

**Builds are each script's job, and one entry has none.** Each gate script runs its own `ninja` over
`build-embassy-hostapp` and then `cargo build`. The `golden-vt-render` entry runs only the cargo build, so after a C++
change it linked stale objects (the undefined-symbol failure during the marker rename). A full run rebuilds the same
tree 23 times.

**Build trees are scattered.** `build/tests`, `build-embassy-hostapp`, `target/harness`, `crates/target`,
`crates/target-tests-sim` and `sim/build` sit at four different depths, and the golden masters bring `build-sim` and
`build-sim-arm`.

**Pins drift.** ETL is pinned at 20.40.0 for the firmware and 20.39.4 for the tests. The CppSpec, argon and SIMDe
declarations are copied into up to 11 `CMakeLists.txt`, each marked "keep in sync". There are two mock trees
(`tests/32bit_unit_tests/mocks`, `tests/unit/mocks`) with an identical `mock_print.cpp` in each.

**`next` has no test CI.** `tests.yml` and `rust.yml` trigger on PRs to `main` and `release/**` only, and
`nightly-build.yml`'s cron is commented out, so 26 nightly-tier entries run only when someone runs them.

## 2. Goals and non-goals

Goals:
- Every test is listed in one registry, with its level, runner, tier and prerequisites.
- One command runs any selection of them, and CI calls that command rather than naming binaries.
- A test's build products are declared, built once per run, and always fresh.
- Adding a scenario gate means writing the scenario, a registry entry and, only if it needs one, a custom check.
- No test code is compiled into the firmware.
- The golden masters from `feat/synth-golden-masters` merge in as ordinary registry entries.

Non-goals:
- **Converging CppUTest and CppSpec.** Both stay; the runner treats both as ctest.
- **Changing what any test asserts.** This is structure only; every gate's marker lines stay byte-identical.
- **Moving the Embassy harness crates.** `golden_vt_render` and `lens1_vt_sim` `#[path]`-include BSP source and stay
  in `src/bsp/rust/harness/` (`harness/README.md` gives the reasons).
- **Folding the Cargo packages into one workspace.** The three constraints in `harness/README.md` still hold.

## 3. Layout

```
tests/
  registry.toml        every test (§4); moved from harness/registry.toml
  README.md            generated table, plus how to add a test
  support/
    cmake/             one file of FetchContent pins (ETL, CppUTest, CppSpec, argon, SIMDe) and the
                       create_specs_driver() helper, included by every test CMakeLists
    mocks/             the one mock tree
    data/              fixtures (today's tests/data)
  unit/                every ctest suite, one CMake tree: today's unit/ and each spec*/ as subdirectories
  unit-arm/            today's tests/qemu
  component/
    differential/      today's harness/rust/* (fs_differential, region_*_differential)
    stress/            today's harness/cpp/tsan
    dsp-diff/          today's harness/cpp/dsp_diff
    fuzz/              today's tests/fuzz; the crates' fuzz targets stay in crates/*/fuzz and are registered
  integration/
    scenarios/         today's src/deluge/harness/*.{cpp,h}
    checks/            the per-gate checks that are more than "these markers appeared" (§7)
    golden/            feat/synth-golden-masters' corpus and runner (§9)
crates/*/tests         unchanged: cargo's convention
src/bsp/rust/tests     unchanged; registered (§4)
src/bsp/rust/harness/  unchanged: golden_vt_render, lens1_vt_sim, preemptive_race_tsan
```

`harness/` goes away; `harness/README.md`'s rationale moves to `tests/README.md`. `tests/32bit_unit_tests/` goes away:
it holds only mocks, which move to `support/mocks/`.

**The rule for where a test lives:** its level, except where Cargo or `#[path]` forces otherwise, and then the registry
says so in its `path`.

## 4. The registry

`tests/registry.toml` keeps today's fields and adds three. A sketch:

```toml
[[test]]
name = "unit"
level = "unit"               # unit | component | integration
runner = "ctest"             # ctest | cargo | scenario | script
path = "tests/unit"
builds = ["test-unit"]       # §6
tier = "pr"                  # pr | nightly | manual (today's ci = "never" becomes manual)
requires = []
gates = "…"

[[test]]
name = "pattern-load"
level = "integration"
runner = "scenario"
scenario = "pattern_load"    # the name the scenario table registers (§7)
builds = ["host-app", "golden-vt-render"]
markers = ["PT-SETUP", "PT-LISTED", "PT-LOAD", "PT-PREVIEW", "PT-DISCARD", "PT-SUPERSEDE", "PT-TIER"]
result = "PT-RESULT failures=0"
check = "tests/integration/checks/pattern_load.py"   # optional (§7)
tier = "nightly"
requires = ["cargo", "deluge-sdk-sibling"]
gates = "…"
```

- `status`, `kind`, `gates`, `runtime` and `requires` keep their meaning; `ci` is renamed `tier`.
- Newly registered: `unit` (one entry per ctest label, or one entry for the whole tree, decided in step 1),
  `unit-arm`, `crates` (nextest over the workspace), `bsp-host` (`src/bsp/rust/tests`), each standalone crate, and
  `deluge_audio_format`'s fuzz target.
- The registry's own Python tests (`scripts/tasks/test_harness_registry.py`) move with it and gain a runner.

## 5. One command

```
./dbt test                              # tier pr: what a PR runs, about a minute
./dbt test --level unit
./dbt test --level integration --tier nightly
./dbt test pattern-load song-save-channel
./dbt test --list [--level …] [--tier …]
./dbt test --doctor                     # today's `harness doctor`: which prerequisites are missing
./dbt test --clean                      # drops build/test-* and build/cargo-*
./dbt test --ci-matrix --tier pr        # today's `harness ci`: JSON for a CI matrix
```

- `./dbt harness …` stays for one release as an alias that prints the new spelling.
- A test whose `requires` are missing is reported as **skipped, with the reason**, never silently omitted. With
  `--strict` (which CI passes) a skip is a failure, so a CI job that lacks a prerequisite goes red instead of green;
  that is the `snapshot-primitive-tsan` gap `1d32a55a8` fixed by hand.
- `scripts/estate_capture.sh` becomes `./dbt test --capture <label>`, with the same output under `.superpowers/estate/`.

**CI** (`tests.yml`, `rust.yml`) collapses to one job matrix from `./dbt test --ci-matrix --tier pr`, triggered on
PRs to `main`, `next` and `release/**`. `nightly-build.yml` runs `--tier nightly` on its cron, uncommented. The
harness-index job keeps checking the generated README table.

## 6. Build products

Each product is declared once, in `scripts/tasks/test_builds.py`, with its inputs, command and output tree:

| Product | Tree (was) | Built by |
|---|---|---|
| `test-unit` | `build/test-unit` (`build/tests`) | CMake from `tests/unit`, Ninja Multi-Config |
| `test-unit-arm` | `build/test-unit-arm` (ad hoc) | CMake from `tests/unit-arm`, arm-linux-gnueabihf |
| `rust-sim-lib` | `build/cargo-test-sim` (`crates/target-tests-sim`) | cargo, for the unit tree's Rust staticlib |
| `host-app` | `build/host-app` (`build-embassy-hostapp`) | CMake from `sim/` with `DELUGE_SIM_X64=ON`, then ninja over the app's object closure |
| `golden-vt-render` | `build/cargo-harness` (`target/harness`) | cargo; its `build.rs` reads `build/host-app` |
| `c-host` | `build/c-host` (`build-sim`) | CMake from `sim/`, the C-host `deluge_host` for the golden masters |
| `c-host-arm` | `build/c-host-arm` (`build-sim-arm`) | the same, cross-built, run under qemu |

- The runner computes the union of the selected tests' `builds`, orders it by dependency (`host-app` before
  `golden-vt-render`), and builds each product once. No gate script builds anything.
- `host-app` always runs its ninja step, which is a no-op when nothing changed. This removes the stale-objects trap
  structurally: nothing can link `build/host-app` without the runner having freshened it first.
- `golden_vt_render/build.rs` keeps its object-closure hash as a second line of defence.
- The old tree paths keep working for one release through `DELUGE_HOSTAPP_BUILD_DIR` and `CARGO_TARGET_DIR`
  defaults; memory notes and docs that name them are updated in the same step.

## 7. Scenario gates

### The scenario table (C++)

Every stepped scenario already exports the same five functions: `begin`, `complete`, `dispatch_step`, `step_done`
and `failures`. Each scenario instead registers them once:

```cpp
// tests/integration/scenarios/scenario_table.h
struct Scenario {
	const char* name;
	void (*begin)();
	bool (*complete)();
	void (*dispatchStep)();
	bool (*stepDone)();
	int32_t (*failures)();
};
// One line at the bottom of each scenario's .cpp:
DELUGE_REGISTER_SCENARIO(pattern_load, begin, complete, dispatchStep, stepDone, failures);
```

with one C entry point, `const Scenario* deluge_scenario_find(const char* name)`. The five per-scenario `extern "C"`
functions go away. Registration uses a static table rather than self-registering constructors, so it survives
`--gc-sections` and needs no static-initialisation order.

### One driver (Rust)

`golden_vt_render/src/main.rs` keeps one generic stepped driver: wait for boot and mount, look the scenario up, step
it with the pacing every gate uses today (1 virtual ms per step), and exit on its failure count. It replaces the 23
stepped tasks, their mode flags and the `if/else` chain; `main.rs` loses roughly 1,500 lines.

The recorder round-trip, lens and stem-render arms are not stepped the same way and keep their own tasks.

### Checks

Every driver script today does the same four things: build, run the binary, require each marker line, require
`<PREFIX>-RESULT failures=0`. The runner does those from the registry's `markers` and `result` fields.

What remains per gate is only the real logic: comparing a recorded hash, reading a count from one line and holding it
against another. That moves to `tests/integration/checks/<gate>.py`, which gets the run's output and returns
pass/fail with messages. By a first count, about half of the 23 scripts have such logic; the rest need no check file.

The FAIL and PASS lines the checks print keep their current text, so estate captures stay comparable.

## 8. Scenarios out of the firmware

- `src/deluge/CMakeLists.txt` stops globbing `harness/`, since it's moving to `tests/integration/scenarios/`.
- `sim/CMakeLists.txt`, the `host-app` product only, adds `tests/integration/scenarios/*.cpp` to the app library.
- The `#ifdef DELUGE_HOST` wrappers around whole scenarios go; the ones around individual host-only knobs go too.
- The few firmware-side hooks scenarios reach through (`*ForDiag()` accessors, `friend class …Probe`) stay, since
  they are part of the code under test's interface.

Verification: the RelWithDebInfo image's symbol table has no `deluge_scenario_*`, and its `.text` shrinks by the
scenarios' size.

## 8a. Card images in memory

The Embassy host harness mounts the same vendored `embedded-fatfs` the device mounts, over a block device that
mirrors the device's (the `SD_BUS` lock, the `sim_latency` queue), so its gates exercise the real storage stack:
free-cluster accounting (`TK-LEAK`, `RW-LEAK`), the recorder's deferred directory entry and cluster chains, card
re-insert and `sector_of()`, cluster-indexed streaming reads. A host directory in place of the card (the C-host's
`DELUGE_SD_ROOT`) would bypass exactly that, so the gates keep a FAT volume. What changes is where it lives and how
it is made.

**Today.** The backend is `deluge-sdk`'s `crates/deluge-bsp/src/sd.rs` `host` module: a file named by
`DELUGE_SD_IMAGE`, read and written with seek/read/write. golden_vt_render packs each fixture into a 2.5 GB sparse
file in the temp dir (FAT32 needs 65,525 clusters at the device's 32 KB cluster size) with mtools (`mformat`,
`mcopy`), after `scripts/golden_mixdown.sh reconstruct` for corpus songs, and caches it by a fingerprint of the
fixture tree. Consequences:
- 2.5 GB files in `/tmp`, which the runner has to clean up after each gate (`scenario_gate.py`).
- mtools as a prerequisite.
- State that leaks between runs: a cached image keeps what earlier runs wrote to it, which is why some served and
  `TIER-SYNC` counts differ between captures, and why a stray file from one gate's red runs changed another gate's
  listing counts.
- Integration gates cannot run in parallel: they share cached images.

**Change.**
1. **A sparse in-memory disk** in `deluge-sdk`'s host backend, selected by the embedding binary (an API
   golden_vt_render calls before boot, e.g. `deluge_bsp::sd::host::use_memory_disk(sectors)`, rather than another
   environment variable): a map from sector to 512 bytes, holding only sectors written, reading zeroes elsewhere.
   The volume keeps its 2.5 GB, 32 KB-cluster geometry; the process holds only what the run wrote (tens of MB).
2. **Images built in-process.** golden_vt_render formats the memory disk with `embedded-fatfs` and copies the
   fixture tree in, instead of packing a file with mtools. The fixture trees themselves are unchanged. Corpus songs
   still come from `golden_mixdown.sh reconstruct` until step 5 replaces that script.
3. **The file backend stays** for debugging and for the corpus gates: `DELUGE_SD_IMAGE` still names a file to use,
   and a new `GOLDEN_SD_DUMP=<path>` writes the memory disk out at exit, for inspecting what a run left on the card.
4. **The runner runs integration gates in parallel,** now that no run shares a disk with another (`--jobs N`,
   defaulting to the machine's cores for `--capture` and the nightly tier). Captures stay in registry order.

**Cross-repo.** Item 1 is a `deluge-sdk` change: commit it there, move `src/bsp/rust/Cargo.lock`'s pinned
revision, and update the nightly/PR prerequisite action's checkout (it reads the revision from the lock, so that
follows). The device build is unaffected: the memory disk is inside the `#[cfg(not(target_os = "none"))]` host
module.

**Verification.** A capture before and after must pass the same 32 gates. Marker values that today depend on what
a cached image held will change once, to what a fresh card gives; the step records that diff and each gate's new
values must then hold across two consecutive captures (which today they do not). `/tmp` must hold no
`deluge-streaming-scenario-*.img` after a full run, and mtools leaves the prerequisites of every gate but the
corpus ones.

## 9. The golden masters

`feat/synth-golden-masters` arrives as:
- `tests/integration/golden/`: `run.py`, `fixtures.json`, the corpus.
- Two registry entries, `golden-synth` (x86, `builds = ["c-host"]`) and `golden-synth-arm` (`builds = ["c-host-arm"]`,
  `requires = ["qemu-arm"]`), both `runner = "script"`.
- `run.py` stages a card **directory** and passes `DELUGE_SD_ROOT`: the C-host serves the card from a directory
  (`host_efatfs_passthrough.cpp`) and no longer reads `DELUGE_SD_IMAGE`, so as merged every fixture would boot with
  no preset and fail its silence check. Staging a directory also drops `run.py`'s mtools dependency.
- `tests/dsp` folds into `tests/unit/dsp` with one argon pin, and `tests/dsp_diff` into `component/dsp-diff`.
- The three boundary checkers (platform, DSP, synth) become one script with three allowlists, registered at
  `level = "unit"`, `runner = "script"`.

The re-baseline of all 336 fixtures happens once, after `run.py` works on this structure, with a per-fixture diff.

## 10. Steps

Each step is one or more commits, and ends with:
- `./dbt test --tier pr` green, and the full nightly selection green;
- an estate capture diffed against the one taken before the step: identical apart from what the step names;
- the RelWithDebInfo image building and linking.

1. **Registry schema and runner.** Done 2026-09-28.
   - [x] `ci` renamed `tier` (`never` became `manual`); `level` and `runner` on all 38 entries. `builds`, `markers`,
         `result` and `check` are added by the steps that consume them (2 and 3), so no field sits unread and
         unvalidated. ctest entries carry `source`, `tree`, `configure` and `ctest_args` instead of `build`/`run`;
         `env_default` sets a variable only where the caller has not.
   - [x] Registered: `unit` (the ctest tree), `unit-arm` (the qemu tree, run through `CMAKE_CROSSCOMPILING_EMULATOR`
         so it needs no binfmt registration), `crates` (nextest), `bsp-host` (`cargo test --locked`, which also
         guards the lock's deluge-alloc pin), `sample-convert`, `embedded-fatfs`, `block-device-adapters`,
         `block-device-driver`, `registry-selftest`, and `audio-format-fuzz` (manual). 48 entries.
   - [x] `./dbt test` implements §5. Tiers are cumulative: `--tier nightly` runs pr too. Named tests run whatever
         their tier, and a named test that skips fails. ctest runs with `--no-tests=error`, so a `-R` that matches
         nothing fails. `dbt harness` is an alias that prints the new spelling. `estate_capture.sh` wraps
         `--capture`, whose selection (gates above unit level, below manual) is exactly the old estate.
   - [x] CI: `tests.yml` is one matrix over `--ci-matrix --tier pr`, each job `--strict --junit`, on `main`, `next`
         and `release/**`; the prerequisites are one composite action (`.github/actions/test-prerequisites`) that
         also checks out deluge-sdk at the revision the lock pins. `rust.yml` keeps lint and the table check, and now
         also runs on PRs to `next`. `nightly-build.yml`'s cron is on, and its test jobs check out `next` (they
         checked out the default branch) and run the cumulative nightly selection. `test-results.yml` listens to
         `tests.yml` alone.
2. **Declared builds.** Done 2026-09-28.
   - [x] `scripts/tasks/test_builds.py` declares `host-app` (`build/host-app`) and `golden-vt-render` (into
         `build/cargo-harness`); 26 entries declare `builds = ["golden-vt-render"]` and `lens1-vt-sim`
         `["host-app"]`. The runner builds the union once, dependencies first, logs a capture's builds to
         `builds.log`, and reports a test whose product could not be built as failed (or skipped, when the product's
         own prerequisites are missing) instead of running it. The `build-embassy-hostapp` probe is gone: nothing
         needs it configured by hand.
   - [x] Trees: `build/test-unit` (was `build/tests`), `build/test-unit-arm`, `build/host-app` (was
         `build-embassy-hostapp`), `build/cargo-harness` (was `target/harness`). The three `build.rs` files that
         default a host-app tree (the BSP's, `golden_vt_render`'s, `lens1_vt_sim`'s) prefer `build/host-app` and fall
         back to `build-embassy-hostapp` where only that exists. `--clean` removes every tree the runner builds.
   - [x] The 25 gate scripts lost their `ninja` and `cargo build` block, `BUILD_DIR` and `NO_BUILD`; run by hand
         before a build, each says to run it through `./dbt test`. `golden_mixdown.sh`, the retired C-host rig,
         keeps its own.
   - Not moved: `crates/target` and `crates/target-tests-sim`, which the unit tree's CMake drives cargo into (a
     `rust-sim-lib` product would have to take that over from CMake); `build-embassy-hostapp-tsan`, which
     `preemptive_race_tsan/run.sh` builds with its own sanitizer flags; the C-host trees, which are step 5's.
3. **Scenario table and one driver.** Done 2026-09-28.
   - [x] `src/deluge/harness/scenario_table.{h,cpp}`: 22 stepped scenarios in one `constexpr` table, found by
         `deluge_scenario_find(name)`, with an optional `onAppInit` hook (clip_lifetime's CL-EPOCH0, which `boot_task`
         called by name). The five per-scenario functions keep C linkage: the table holds pointers to them across the
         C ABI. The recorder round-trip and stem-render arms are not stepped and keep their own tasks.
   - [x] One generic `run_stepped_scenario` task replaces the 22 per-scenario tasks, their 111 declarations and the
         mode flags; the card image a scenario needs is one `match` on its name. `main.rs`: 3,214 lines to 1,804.
         The four lifetime scenarios' early `hard_exit(0)` goes: `main` exits the same way once `DONE` is set.
   - [x] `runner = "scenario"` entries carry `scenario`, `markers`, `result` and an optional `check`;
         `scripts/tasks/scenario_gate.py` runs the binary and makes the standard checks, and removes the card images
         and `deluge-<gate>-card` dir a gate's runs leave in the temp dir. 25 gates run natively; 21 have a check
         module in `tests/integration/checks/` (placed at its final location ahead of step 4). The 24 gate scripts are
         deleted; `golden_mixdown.sh` and `golden_embassy_diff.sh` remain as `script` entries.
   - How the ports were proven equivalent: each gate's real runs were recorded once, then replayed through a fake
     binary under every mutation generated from the recording (exit code, each marker tag dropped, each integer field
     zeroed and bumped) through both the old script and the new check; all 23 ports agreed on every mutation
     (about 2,750 in all). Paths the replay cannot reach (clip_lifetime's rerun loop, save_roundtrip's second run
     alone, stem_export's hash) were checked by hand against both.
3½. **Card images in memory** (§8a).
   - [x] `deluge-sdk`: a sparse in-memory disk in `deluge-bsp`'s host `sd` module, chosen by `use_memory_disk`
         before the disk opens, written out by `dump_memory_disk`; the file backend unchanged (deluge-sdk a58edba,
         branch `feat/host-memory-disk`).
   - [ ] Pin that change in `src/bsp/rust/Cargo.toml` and `Cargo.lock` once it is on the remote. Local builds do not
         need it (golden_vt_render path-depends on the deluge-sdk working tree), but CI checks deluge-sdk out at the
         lock's revision, so golden-vt-render's gates fail in CI until then.
   - [x] golden_vt_render (`memory_card.rs`): formats the memory disk FAT32 and copies each fixture tree in with
         `embedded-fatfs`, in name order; `GOLDEN_SD_DUMP` added. `sd_image.rs`'s mtools packing stays for the
         entries that still pack a file image (fs-differential, lens1-vt-sim, preemptive-race-tsan,
         golden-embassy-diff); golden_vt_render only uses its fixture-tree lookup.
   - [x] `scenario_gate.py` removes only a gate's own scratch dir. The runner gains `-j/--jobs`; `--capture` defaults
         to one job per CPU, other runs to one. mtools left `requires` of golden-vt-render, audio-load-ab and
         song-preview-ab.
   - [x] Two consecutive serial captures agree on every marker value except the wall-clock `*Micros=` fields, and a
         parallel capture agrees with them to the same extent (32/32, 1:22). The one-time diff from the mtools cards
         is sample-claim's randomizer picking RD3 for RD1: the directory order is now name order.
4. **Layout.**
   - [ ] The moves in §3, one directory per commit; `support/cmake` replaces the copied pins, and ETL is pinned once.
   - [ ] §8: scenarios out of the firmware build.
5. **Golden masters** (with the synth-golden-masters merge): §9.

Steps 1 and 2 give most of the day-to-day benefit and could stop there if the rest has to wait.

## 11. Decisions (were open questions; Kate took each proposal)

- **ctest granularity in the registry.** One entry for the whole unit tree keeps the registry short; one entry per
  suite makes `./dbt test mem_guard_tests` work. Proposal: one entry for the tree, and pass-through
  `./dbt test unit -- -R <regex>` to ctest.
- **Python checks or shell checks.** The runner is Python already; checks as Python keep the logic testable.
  Some scripts are long bash; converting them is the bulk of step 3's work.
- **Where `preemptive_race_tsan` sits.** It is a whole-app stress run under TSan: integration-level scope with a
  sanitizer runner. Proposal: `level = "integration"`, `runner = "script"`, `tier = "manual"` as today, since it
  needs the song corpus.
- **The BSP host tests in CI.** `src/bsp/rust` needs the sibling `deluge-sdk` checkout. Either CI checks it out, or
  those tests move behind the `#[path]` shim crates the way `deluge_audio_loader_host` did. Proposal: CI checks it
  out; it already has to for anything that builds the BSP.
