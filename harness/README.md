# The harness estate

Rigs that verify the firmware but are not ctest unit specs: differentials,
virtual-time simulations, sanitizer stress runs, and fuzzers. Unit specs live in
`tests/` and run under `./dbt test`; everything here runs under `./dbt harness`.

    ./dbt harness list              # what exists, and what you can run right now
    ./dbt harness run <name>        # build + run one
    ./dbt harness doctor            # what is missing, and how to get it

**The table below is generated.** Edit `registry.toml`, then regenerate:

    ./dbt harness list --markdown

<!-- BEGIN GENERATED TABLE -->
| Harness | Kind | Status | Gates | CI | Runtime |
| --- | --- | --- | --- | --- | --- |
| fs-differential | differential | gate | embedded-fatfs must agree with the reference FAT implementation on every filesystem operation | pr | ~1m |
| audio-loadcheck | regression | gate | AudioFileManager::proposeLoadCandidates's candidate order/count (the alternate-dir-vs-regular-path policy a real load and the async loader share) and the load invariants (no half-built cache entry, no leaked AudioFile reason) must hold -- this is what caught the dropped-bare-name-candidate regression this entry exists to pin. Also gates the libdeluge/audio_load.h C ABI's build-ownership contract: deluge_audio_load_abandon() must actually release the adopted resource-manager chunk, not just free its wrapper, or repeated begin/abandon churn (as a browser cursor move that never lands would produce) exhausts the chunk table. | pr | ~5m |
| region-fill-differential | differential | gate | the Rust cluster fill must stay byte-identical to the C++ convert/stitch reference | pr | ~1m |
| region-read-differential | differential | gate | the sample range-reader must resolve the same frames as the chunk-payload oracle | pr | ~1m |
| region-source-differential | differential | gate | the region cursor must resolve a chunk's payload, not its header, over real streamed backing | pr | ~1m |
| golden-vt-render | simulation | gate | the SampleRecorder record-then-read-back round trip must be byte-exact on the Embassy host BSP (the ex-deluge_recorder_roundtrip oracle, converged onto Embassy) | nightly | ~5m |
| audio-load-ab | differential | gate | the async Rust audio loader and the synchronous C++ card path must load the SAME file the SAME way -- identical chosen path (regular vs alternate dir), identical byteDepth/numChannels/audioDataStartPosBytes/audioDataLengthBytes and the rest of the parsed geometry, identical bytes read back through the Sample's own stream, and the identical Error identity on a failure. Also pins, per leg, that no load leaves a half-built zero-geometry object or a leaked reason behind, and that the async leg genuinely went through the collector (at least one load deferred) rather than being answered synchronously. | nightly | ~5m |
| lens1-vt-sim | simulation | investigative | measures streaming-underrun margin against modeled SD latency; deterministic virtual time | never | ~25m |
| preemptive-race-tsan | sanitizer | gate | sustained playback-while-record must report no new data race under ThreadSanitizer | never | ~30m |
| snapshot-primitive-tsan | sanitizer | gate | Published<T> and SpscRing<E,N> must be race-free under concurrent stress | pr | ~3m |
| dsp-diff | differential | gate | the ARM/NEON DSP fast paths must be byte-identical to the portable host fallback | nightly | ~5m |
| golden-mixdown | differential | unavailable | retired on this branch only, not obsolete: this script's `check`/`update`/`padsweep` verbs drive the C-host `deluge_render` CMake target, which was deleted from sim/CMakeLists.txt on this branch when the sample-streaming golden moved onto the async Embassy renderer (see golden-embassy-diff); running `check`/`update`/`padsweep` now fails at the cmake build step (no such target), and only `reconstruct` -- a fixture-prep helper the Rust harnesses call internally, not a standalone gate -- still works here. The rig remains current on `feat/synth-golden-masters`, where `deluge_render` (sim/CMakeLists.txt) and the `tests/golden/` harness it belongs to still live. Do not delete `scripts/golden_mixdown.sh`: another branch depends on it. | never | n/a — does not build |
| golden-embassy-diff | differential | gate | the Embassy host BSP's mixdown must match the C-host golden renderer's for the same fixture | never | ~20m |
| alloc-fuzz | fuzz | unavailable | the TLSF allocator must not corrupt its free lists under arbitrary alloc/free sequences -- UNRUNNABLE as it stands: `cargo build`/`cargo fuzz run` on this package fails with cargo error "current package believes it's in a workspace when it's not", reproduced on stable, 1.85, and nightly cargo. This is NOT the `exclude` in crates/Cargo.toml failing to take effect -- `cargo metadata --manifest-path crates/Cargo.toml` exits 0 and does not list this package, so the exclude works. The real cause is that `deluge_alloc/fuzz` is nested inside `deluge_alloc/`, a real workspace member; cargo's directory walk finds that ancestor and attributes the fuzz package to the crates/ workspace regardless of the exclude, then errors because it isn't a member. The standard cargo-fuzz remedy is an empty `[workspace]` table in crates/deluge_alloc/fuzz/Cargo.toml, which this registry deliberately has NOT applied -- `cargo metadata` succeeding is not evidence the fuzzer builds or runs, and this registry exists to stop us recording things as working that we have not run. Next step for whoever picks this up: add the empty `[workspace]` table, then actually run `cargo +nightly fuzz run ops` (or `slab`) before flipping status off `unavailable`. The brief's `run` target name ("alloc") was also wrong -- the real targets, per fuzz_targets/, are "ops" and "slab". | never | unbounded |
| memmove-fuzz | fuzz | unavailable | the hand-written memmove must match libc semantics on arbitrary overlapping ranges -- UNRUNNABLE as it stands: this is a standalone CMake project (its own top-level CMakeLists.txt, no ctest/enable_testing wiring at all, so the brief's `ctest -R memmove_fuzz` draft could never have worked). It hard-codes ARM/NEON flags (-mfpu=neon -mfloat-abi=hard) plus -fsanitize=fuzzer, so the default host GCC toolchain rejects all three flags outright; switching to clang with an arm-linux-gnueabihf sysroot compiles but fails to LINK -- libclang_rt.fuzzer.a and friends are not present for the arm-unknown-linux-gnueabihf target on this machine. No requires key in the closed vocabulary covers a libFuzzer-capable ARM clang + compiler-rt; needs a toolchain this repo does not otherwise depend on. | never | n/a — does not link |
<!-- END GENERATED TABLE -->

## Where things live, and why

Placement follows coupling to the BSP source tree, not language or purpose.

- **`harness/rust/`** — harnesses whose subject is a `crates/` library or
  `cc`-compiled application C++, rather than the BSP itself.

  One of these, `fs_differential`, does reach into the BSP: its
  `tests/efatfs_core.rs` `#[path]`-includes `src/bsp/rust/src/efatfs_core.rs` so
  the host test drives the exact device module. That single reach does not move
  it, because the rule turns on what a package *is*, not on whether any line
  touches the BSP — its six-file library is BSP-free and its subject is the
  vendored `embedded-fatfs` stack in `crates/`. Contrast `lens1_vt_sim` and
  `golden_vt_render` below, which `#[path]`-include twenty-odd BSP files each:
  being a second compilation of the BSP is their whole identity.

  The cost of that exception is a four-level `#[path]` reach that breaks if the
  BSP tree moves, plus a `rustfmt.toml` in that package excluding the borrowed
  file from its formatting run (the two crates are on different editions and
  disagree about import order). Both are recorded here so the next person sees
  the exception rather than rediscovering it.
- **`harness/cpp/`** — standalone C++ rigs with their own `run.sh`, deliberately
  not ctest-driven.
- **`src/bsp/rust/harness/`** — harnesses that `#[path]`-include BSP source, so
  a change to a shared file is picked up by both the firmware and the harness.
  `lens1_vt_sim` and `golden_vt_render` are second *compilations* of BSP source,
  not copies of it; that is the whole point, and it is why they cannot move out.

Three constraints keep these as separate Cargo packages rather than one
workspace, and they are worth knowing before adding a tenth:

1. `[patch.crates-io]` is workspace-global. Only `src/bsp/rust/Cargo.toml`
   carries the embassy fork patch; a workspace spanning it and the host clock
   harnesses would force the fork onto them silently.
2. `embassy-time`'s `std` and `mock-driver` features both emit
   `#[no_mangle] _embassy_time_now`. Cargo unifies features across all targets
   of one package, so two time drivers cannot share a package — and
   `cargo build --workspace` cannot span them either.
3. Feature unification makes a shared workspace build a hazard even where the
   symbols do not collide.

## Build artifacts

`./dbt harness run` points every Rust harness at one shared
`target/harness/` directory. Cargo namespaces artifacts by fingerprint, so
packages with different `[patch]` sections coexist there safely — but cargo
takes an exclusive lock per target directory, so two harness runs at once will
serialize rather than run in parallel.

Running `cargo` by hand inside a harness directory bypasses this and creates a
local `target/`. That is what produced the 26 GB across 13 directories this
layout replaced. Prefer `./dbt harness run`; `./dbt harness clean` drops the
shared tree.

## Adding a harness

Add a `[[harness]]` block to `registry.toml` and regenerate the table. `gates`
and `status` are mandatory: a harness that cannot state the regression it
catches is one nobody can later decide to delete. That is not hypothetical —
two crates whose own headers declared them throwaway survived months because
nothing recorded that fact anywhere a reader would look.
