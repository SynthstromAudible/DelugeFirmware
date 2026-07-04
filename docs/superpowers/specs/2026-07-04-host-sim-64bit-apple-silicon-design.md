# Host-sim 64-bit / Apple Silicon — design

Status: approved (design), pending spec review
Date: 2026-07-04

## Goal

Let the host-sim build and run natively as a 64-bit binary, so it works on Apple
Silicon macOS (which has no 32-bit compatibility). The immediate, testable target is
a clean 64-bit build on x86-64 Linux; native arm64 macOS follows once the 64-bit core
is proven.

## Background

The Deluge app is written for a 32-bit target and the host-sim defaults to `-m32` to
match the firmware's 32-bit pointer model exactly (the golden WAV captures are taken
against it). An experimental `DELUGE_SIM_X64=ON` mode already exists but does not build:
it fails with **80 "cast from pointer to smaller type loses information" errors**.

Investigation findings:

- All 80 errors are either **pointer arithmetic** performed via `(uint32_t)ptr` casts
  (e.g. `(uint32_t)clusterEndPos - (uint32_t)writePos`, whose result is a small byte
  count), or the **host-only** allocation-tracking hash table in
  `src/deluge/memory/alloc_metadata.cpp` (a `uint32_t address;` field + `uint32_t` key).
- There is **no pointer storage in shared firmware struct fields** — so going 64-bit does
  not break any layout shared with the firmware. The change is feasible without touching
  shared data representations.
- Errors are concentrated in ~15 files, led by `sample_low_level_reader.cpp` (19),
  `sample_recorder.cpp` (18), `audio_engine.cpp` (8), `wave_table.cpp` (7),
  `alloc_metadata.cpp` (6). Plus one stray "no matching function" and a link error.
- The Rust TLSF allocator already uses `usize` for addresses and documents 32/64-bit
  header sizing; the `x86_64-unknown-linux-gnu` Rust target is already wired for the X64 path.
- Apple Silicon is **aarch64**, which is *not* `__arm__`: the armv7 VFP `vcvt` asm in
  `fixedpoint.h` will not fire (it takes the portable path, already proven bit-exact to
  VFP under qemu), and argon uses native aarch64 NEON rather than SIMDe.

## Approach

**Mechanical `uintptr_t`, firmware-neutral (chosen).** Each flagged `(uint32_t)ptr`
becomes `(uintptr_t)ptr`; the arithmetic keeps its small-integer result and assigns into
the same `int32_t`/`uint32_t` byte-count as before. On the 32-bit firmware
`uintptr_t ≡ uint32_t`, so codegen is byte-identical — the firmware is unaffected. The
host-only `alloc_metadata` hash widens its key/field to `uintptr_t`.

Rejected alternatives: a pointer-int abstraction/typedef routed through a helper (more
robust against future regressions, but larger churn in shared DSP code for little near-term
gain); minimal-to-link (leaves real runtime data loss — UB — on 64-bit).

## Phase 1 — 64-bit-clean + measure (x86-64 Linux, testable now)

1. Fix all 80 truncations with `uintptr_t`; widen the `alloc_metadata` host hash to
   pointer width; resolve the stray "no matching function" and the link error.
2. `DELUGE_SIM_X64=ON` builds clean under **both clang and gcc**, and the `-m32` build,
   the GCC/clang default sims, and the ARM firmware all still build (no regression).
3. `deluge_render` / `deluge_host` **run** in 64-bit mode.
4. Run the **golden sweep in 64-bit** and diff against the 32-bit goldens. This is the
   feasibility verdict.

### Success criteria (Phase 1)

- `DELUGE_SIM_X64=ON` compiles with 0 errors (clang and gcc) and links.
- The 32-bit default sim, the ARM firmware, and the qemu specs are unchanged (byte-identical
  firmware; existing goldens still pass in 32-bit mode).
- `deluge_render` produces a valid stem in 64-bit mode.
- Golden sweep in 64-bit mode is run and its result characterised: either bit-identical to
  the 32-bit goldens, or a per-fixture account of what shifted and whether the cause is
  pointer/layout dependence (potentially fixable) versus fundamental.

### Decision gate

Bring the **replace `-m32` vs coexist** decision back to the user with the Phase-1 golden
data. Coexist keeps `-m32` as the CI/golden reference and adds 64-bit for macOS; replace
drops 32-bit and re-baselines goldens. Not decided now — decided on evidence.

## Phase 2 — Apple Silicon arm64 macOS (gated on Phase 1; user tests on the Mac)

Darwin platform layer, kept separate because it is untestable from the Linux dev box and
only worth doing once the 64-bit core is sound:

- Linker: `-dead_strip` instead of `LINKER:--gc-sections`; drop `-m32` / `-static-libgcc`.
- Toolchain: clang + libc++; Homebrew include/lib paths.
- Rust: `aarch64-apple-darwin` staticlib target.
- SIMD: argon native aarch64 NEON (no SIMDe on this path); confirm bit-parity of the NEON
  ops used.
- Mach-O specifics as they surface (section attributes, `--gc-sections` equivalents).

## Out of scope

- Changing any firmware behaviour or shared data layout.
- arm64 **Linux** as a distinct target (only insofar as it falls out of the 64-bit work).
- The replace-vs-coexist decision (deferred to the Phase-1 gate).
