# The synth's mod matrix runtime

The Deluge instrument's patch cables run as a compiled matrix program, evaluated once a block for the sound and once
for each voice. The arithmetic is the instrument's q31 patching, bit for bit, so it keeps its sound. This page
describes the runtime as built; the design and its reasons are in the mod-matrix specs.

## Where it lives

| File | What it holds |
|---|---|
| `src/synth/matrix/descriptors.h` | What each param and cable is: its scope (voice or sound), chain (multiply, add, depth), curve, depth shape and source conversion |
| `src/synth/matrix/program.h` | The compiled program: one `ScopeView` of tables per scope |
| `src/synth/matrix/compiler.{h,cpp}` | `compile_program` (the routing into a program, in one block sized to fit), `copy_program` (a clone) |
| `src/synth/matrix/matrix_block.h` | `MatrixBlock`: what a pass reads besides its program (each param's preset, each cable's depth) |
| `src/synth/matrix/matrix_fixed.h` | `MatrixFixed`: the q31 numeric core, scalar functions and their Argon four-lane forms |
| `src/synth/matrix/evaluator.{h,cpp}` | `Evaluator<B>`: a scope's pass over a numeric core `B` |

`PatchCableSet` compiles the program right after it compiles its routing and owns the block. `Sound::matrixBlock`
gathers a `MatrixBlock`; `Sound::matrixPass` and `Voice::matrix_pass` name what a pass reads and writes.

## The program

Each scope's `ScopeView` points into the program's block:

- **Polarity slots** (`slot_source`, `slot_conversion`): each distinct pair of a source and the way a cable reads it
  (as is, unipolar, aftertouch bipolar, aftertouch as a depth). Cables that read a source the same way share a slot.
- **Cable tables**, one entry per cable, in the routing's order: `cable_slot`, `cable_depth` (the cable's index into
  the block's depths), `cable_range` (the depth result that scales it, or `kNoRange`), `cable_mode` (its depth shape:
  plain, or one of the squared shapes some add-chain params give their cables; on the master pitch the shape also
  depends on whether the source is velocity).
- **Destinations**: depth destinations first, then param destinations, each with its run of cables, its chain, its
  curve, its param (or its depth result's index), whether it is a wavetable index, and the sources its cables read.
- `sources_patched` (the sources any cable reads) and `cabled` (one bit per param with cables).

## A pass

`Evaluator<B>::run(pass, block, sources_changed)` runs each block:

1. Return at once if no patched source changed.
2. Convert each polarity slot's source once.
3. Each depth destination one of whose sources changed: its cables' terms multiplied, through the linear curve, into
   the depth results.
4. Each param destination one of whose sources changed: its cables in order, each term the slot's value scaled by
   the cable's depth (shaped on an add chain), then by its depth result; a multiply chain starts from the preset and
   multiplies each term in, an add chain sums the terms, doubles them for a wavetable index and adds the preset.
   The curve for the param's kind gives the final value.

`run_initial` (a note's first patching, and the sound's whenever it re-patches every param) first computes every
param as if uncabled, then runs every destination as above, overwriting the cabled params. The volume, linear and
hybrid regions go four at a time, with any remainder of fewer than four through the scalar curve; the exp params go
one by one, skipping the cabled. `run_uncabled_param` sets one param from its preset alone.

The depth results are one array shared by every pass: a pass that skips a depth destination reads the last value any
pass wrote there. The audio tier's passes and the main tier's initial patchings both write it, unguarded.

## Exactness of the Argon forms

`MatrixFixed`'s four-lane forms equal its scalar functions lane for lane; `tests/dsp/matrix_exactness_spec.cpp` holds
them to that over operand corners and random operands, and over every param the four-lane curves compute, on x86,
under UBSan and on ARM under qemu.

- `smmul_by_doubling(a, b)` is `vqdmulh` then an arithmetic shift right by 1: floor(floor(2ab / 2^32) / 2) =
  floor(ab / 2^32), which is `smmul`, as long as the doubling does not saturate. NEON saturates only INT32_MIN times
  INT32_MIN. SIMDe's portable `vqdmulh` (what the host and spec builds run) saturates whenever its first operand is
  INT32_MIN, so the first operand is always one that never is: a param range (positive) or a neutral value (zero or
positive).
- `saturate_shift<bits, n>` is a clamp to `bits` signed bits then a shift left by `n`: `signed_saturate<bits>(v) << n`.
- `uncabled_multiply(term)` is `term & ~7`. An uncabled multiply chain is `multiply_step(kOne, term) - kOne` =
  `lshift_and_saturate<3>(smmul(2^29, term + 2^29)) - 2^29`. With the term in [-2^30, 2^30] (a preset times a
  range of at most 2^30), `smmul(2^29, x)` is `x >> 3`, the 29-bit clamp never bites, and the shifts drop the low
  three bits; 2^29 is a multiple of 8, so subtracting it back leaves the term with those bits cleared.
- The curves follow `getFinalParameterValueVolume`, `…Linear` and `…Hybrid` operation for operation; the volume
  curve's square is a wrapping 32-bit multiply in both.

## Speed

On the device (the DSP bench's `matrix_q31_*` cases: 14 cables, two of them depth cables, over 8 voices and the
sound), against the Patcher (the frozen reference in `tests/dsp/reference_patcher.cpp`, as an image of its own), in
cycles over 128 blocks:

| Case | Patcher | Matrix |
|---|---|---|
| Held note (`typical`) | 708,385 | 696,416 |
| Every source moving (`all`) | 902,810 | 864,483 |
| Every note starting (`initial`) | 1,823,155 | 1,799,989 |

(Medians of three interleaved runs on one device.)

The cable products stay scalar. A cable-major form that multiplied four cables at a time with NEON was measured
about 11 % slower: at a dozen cables, gathering each group's slots, depths and ranges into lanes and moving values
between the core and NEON registers cost more than the multiplies saved.

## The numeric core's contract

`Evaluator<B>` is a template over its numeric core; `evaluator.cpp` instantiates it for `MatrixFixed` alone. A core
provides `kOne`, `convert`, `depth`, `product`, `depth_product`, `multiply_step`, `add_step`, `preset_term`,
`double_saturating`, `depth_result` and `curve`, and for the uncabled bulk a `Lanes` type with `preset_terms`,
`uncabled_multiply`, `curve_volume`, `curve_linear` and `curve_hybrid`. A float core (planned, for the float
instrument plugin in `TODO.md`) would provide the same over `float` and `Argon<float>` and add its own explicit
instantiation; the program, the compiler and the pass order would stay as they are.
