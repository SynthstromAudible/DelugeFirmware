# Fixed-point formats and operations

The Deluge instrument computes in fixed point. `src/deluge/dsp-next/core/fixed_point.h` gives each value a type
that carries its format, and gives each operation a name that states its overflow and rounding. This page covers the
notation, how to choose an operation, the Cortex-A9 instructions behind them, and the rules at the edges.

The float instrument does not use these types.

## Formats

| Type | Storage | Meaning |
|---|---|---|
| `Q<F>` | `int32_t` | signed, `F` fractional bits (0–31): the value is `raw / 2^F` |
| `UQ<F>` | `uint32_t` | unsigned, `F` fractional bits (0–32) |
| `Q16<F>` | `int16_t` | signed 16-bit sample data and tables |
| `Q16x2<F>` | `uint32_t` | two `Q16<F>` packed low/high, for the SIMD32 instructions |
| `Q64<F>` / `UQ64<F>` | `int64_t` / `uint64_t` | exact intermediates only, not long-lived state |

The formats the code uses today include:
- `Q<31>`: audio samples, in [−1, 1).
- `Q<30>`: the high word of a Q31 × Q31 product.
- `Q<27>`: volumes with unity at 2^27.
- `UQ<32>`: oscillator phase and phase increment, where 2^32 is one cycle.
- Signed Q8.24: sample-playback phase increment.
- `Q<24>`: the DX7 engine.

`one()` is 2^F, except where 2^F doesn't fit: `Q<31>::one()` is INT32_MAX, which is one minus one LSB, today's
`kOneQ31`. `q<F>(double)` builds a constant at compile time, rounding to nearest. A constant out of range doesn't
compile.

The types have no arithmetic operators and no implicit conversions. The only way in is `T::from_raw(storage)` and
the only way out is `.raw()`.

## Choosing an operation

**The multiply's result format comes from its inputs.** `mul(Q<a>, Q<b>)` is the high word of the product, a
`Q<a+b−32>`. So a Q31 × Q31 multiply gives Q30 (`smmul` halves the scale), and the type says so.

**Reformat and scale are different operations, even though both compile to a shift:**
- **Reformat** (`as_*`) keeps the value and changes the format. `as_wrap<Q<31>>(mul(a, b))` turns the Q30 product
  back into Q31.
- **Scale** (`scale_*`) keeps the format and changes the value. `scale_shl_sat<4>(x)` is a gain of ×16.

Calling a widening reformat to narrow, or a narrowing one to widen, doesn't compile.

**Overflow policy belongs to the operation, not the type.** The same value is often wrapped in one place and
saturated in another.

| Suffix | Policy | When it is right |
|---|---|---|
| `_wrap` | modulo 2^bits, computed in the unsigned storage (defined behaviour) | phases; sums whose result fits even when a partial sum doesn't; multiply-accumulate chains (`smmla`) |
| `_sat` | clamp to [min, max] | buses, outputs, stores, parameter sums |
| `_clamp_then_shift` | clamp to 32−n bits, then shift; ceiling 2^31 − 2^n | today's `lshift_and_saturate<n>` |
| `_floor` / `_round` | toward −∞ / half up | narrowing reformats and right-shift scales |

**There are two saturating Q31 multiplies, and they differ:**
- `mul_doubling_sat` is `vqdmulh`: `sat(2ab >> 32)`.
- `as_sat<Q<31>>(mul(a, b))` is `sat(smmul << 1)`.

They differ in the lowest bit on about half of all inputs. Pick the one whose bits the code needs.

**Reading bits as another type** (an unsigned increment read as signed, say) is
`reinterpret_bits<To>(x)`. Every use says why in a comment.

## The instructions

Every operation is plain C or an ACLE intrinsic that clang 23 compiles to the instruction below, in ARM and in
Thumb-2. The app's files are Thumb-2; dsp-next's own libraries are ARM. Two kinds of inline asm remain: the
high-word multiplies (below) and the fixed-point VFP `vcvt`, which clang does not form by itself.

`tests/unit-arm/codegen` disassembles each operation in both instruction sets, built with `-O2
-fno-inline-functions`, which inlines only always_inline functions: stricter than either of the image's inliners. It fails on any other instruction sequence, on a
wrong immediate where one matters, and on any call. It also reports, without failing, which loop shapes the compiler
auto-vectorizes; the multiply loops stay scalar. `tests/unit-arm/spec/fixed_point_isa_spec.cpp` compares every operation, and every portable form,
against the real instruction under qemu.

| Operation | A9 instruction |
|---|---|
| `add_wrap`, `sub_wrap` | `add`, `sub` |
| `add_sat`, `sub_sat`, `neg_sat`; `as_sat` by one bit | `qadd`, `qsub`; `qadd x, x` |
| `add_double_sat`, `sub_double_sat` | `qdadd`, `qdsub` |
| `clamp_bits<N>`, `clamp_unsigned_bits<N>` | `ssat`, `usat` (a right shift before folds in: `ssat …, asr #s`) |
| `mul`, `mul_r`, `mla`, `mla_r`, `mls`, `mls_r` | `smmul`, `smmulr`, `smmla`, `smmlar`, `smmls`, `smmlsr` |
| `mul_wide`, `mla_wide`, `mul(UQ, UQ)` | `smull`, `smlal`, `umull` |
| `mul(Q, Q16)`, `mul_top`, `mla(…, Q16)` | `smulwb`, `smulwt`, `smlawb` |
| `dot`, `dot_diff`, `dot_acc`, `dot_wide_acc` | `smuad`, `smusd`, `smlad`, `smlald` |
| `Q16x2` `add_sat`, `sub_sat`, `halving_add` | `qadd16`, `qsub16`, `shadd16` |
| `from_float<F>`, `to_float` | `vcvt.s32.f32 #F`, `vcvt.f32.s32 #F` |

The run-time `clamp_bits(x, n)`, saturating narrows, the scalar `mul_doubling_sat`, `scale_shl_sat` by more than one
bit and `from_float_round` have no single instruction. The gate holds them to no calls and an instruction budget.

The operations are written with builtins, comparisons and `constexpr` constants rather than `std::` helpers, so that
they compile to their instruction under any inliner, the probes' included.

### The high-word multiplies are asm

`src/deluge/dsp-next/core/high_word_asm.h` holds `smmul`, `smmulr`, `smmla`, `smmlar`, `smmls` and `smmlsr` as inline
asm, the one implementation behind both `q31_ops.h`'s raw helpers and, at run time on ARM, `fixed_point.h`'s `mul`
family. Moving a kernel from the helpers to the types therefore leaves its instructions as they were. The portable C
bodies are the constant-evaluated and host forms.

Clang has no intrinsic for `smmul`, so plain C means the idiom `(int32_t)(((int64_t)a * b) >> 32)`. Clang's loop
vectorizer widens loops of that idiom into NEON, which is 3–7× slower on the A9: `vmull.s32` + `vshrn.i64`, or a
`umull` per lane with values shuttled through `vmov.32`. Empty-asm barriers can stop it, but they cost 10–40% in
other kernels. The asm is opaque to the vectorizer.

Measured on the device with a layout-stable bench (below): every plain-C form of the helpers moved single kernels
by ±10–40%.

`TODO.md` tracks the LLVM side, where the asm would give way to an intrinsic: smmul-family intrinsics, the NEON cost model, and a scalar ×2 unroll that is 22–30%
faster on the A9.

## Lane packs

`src/deluge/dsp-next/core/fixed_point_lanes.h` gives the 32-bit arithmetic above on `LanePack<Q<F>, N>` (and
`UQ`): the sums, clamps, multiplies, reformats and scales. The header lists what isn't lifted yet: the 64-bit narrows,
the Q16 forms and the float conversions. `LanePack` itself gives the comparisons. Each operation applies its scalar
operation lane by lane, so a kernel's step written once over its value type reads the same for one stream as for N:
- A scalar operand broadcasts, as a coefficient every lane shares does: `mul(pack, gain)`.
- The result's format is the scalar operation's: two `Q<31>` lanes multiplied give two `Q<30>` lanes. An operation
  exists for a pack only where its scalar operation exists for the lane types.
- Every pack must be the same width, and a pack of `Fixed` has no arithmetic operators, as a `Fixed` has none.
- A comparison gives a `LanePack<bool, N>`, which `select` reads.
- `from_raw<T>(v)` and `to_raw(v)` convert a raw scalar or `LanePack` of raw lanes and its typed counterpart, so a
  step whose interface is raw types its body once for both: `OnePole` and `ShiftTracker` do.

The lanes are scalar, not NEON: a two-lane `mul` is two `smmul`, independent chains the A9 overlaps. The codegen gate
holds the two-lane multiply, multiply-accumulate and saturating add and shift to one instruction per lane. It reports a
two-lane comparison, which clang's SLP vectorizer pairs into `vcgt.s32`. NEON kernels use the vectors below.

## Vectors

`src/deluge/dsp-next/core/fixed_point_neon.h` holds N lanes of one format in one NEON register, with Argon as the
storage: `FixedVec<Lane, N>`. As with the scalars, a vector has no arithmetic operators, and each operation has the
name of the scalar operation with the same per-lane meaning:

| Alias | Lanes | Register |
|---|---|---|
| `QVec<F>` | four `Q<F>` | `Argon<int32_t>` (int32x4_t) |
| `UQVec<F>` | four `UQ<F>`; `UQVec<32>` is four phases | `Argon<uint32_t>` |
| `Q16Vec<F>` | eight `Q16<F>` | `Argon<int16_t>` (int16x8_t) |
| `Q16Half<F>` | four `Q16<F>`, a half register | `ArgonHalf<int16_t>` (int16x4_t) |
| `Q16x2Vec<F>` | four `Q16x2<F>` pairs, one per 32-bit lane | `Argon<uint32_t>` |

| Operation | Formats | Instruction |
|---|---|---|
| `add_wrap`, `sub_wrap` | any vector of `Q`, `UQ` or `Q16` lanes | `vadd`, `vsub` |
| `mul_doubling_sat` | `QVec<A>` × `QVec<B>` or `Q<B>` → `QVec<A+B−31>`; `Q16Vec<A>` × `Q16<B>` → `Q16Vec<A+B−15>` | `vqdmulh` |
| `mul_doubling_r_sat` | `QVec<A>` × `QVec<B>` → `QVec<A+B−31>` | `vqrdmulh.s32` |
| `scale_shl_wrap<N>`, `scale_shr_floor<N>` | `QVec<F>` → `QVec<F>` | `vshl.i32`, `vshr.s32` |
| `mul_doubling_sat_lane<I>` | `QVec<A>` × lane I of `QVec<B>` → `QVec<A+B−31>`; the other lanes are not read | `vqdmulh.s32` by `d[x]` |
| `add_scale_shr_floor<N>` | acc + x / 2^N, `QVec<F>` | `vsra.s32` |
| `as_floor<QVec<T>>` | `QVec<F>` → `QVec<T>`, T < F | `vshr.s32 #(F−T)` |
| `reinterpret_bits<QVec<T>>` | `UQVec<F>` → `QVec<T>`, the same bits | none |
| `sum_wrap` | the four lanes of `QVec<F>` → `Q<F>`, wrapping | `vadd.i32`, `vpadd.i32`, `vmov` |
| `lo()`, `hi()` | each pair's halves: `Q16x2Vec<F>` → `Q16Half<F>`; a register's halves: `Q16Vec<F>` → `Q16Half<F>` | `vmovn.i32`, `vshrn.i32 #16`; none |
| `from_q16` | `Q16Half<F>` → `QVec<F+16>`, exact | `vshll.i16 #16` |
| `mul_doubling_wide_sat` | sat(2 × a × b): `Q16Half<A>` × `Q16Half<B>` → `QVec<A+B+1>` | `vqdmull.s16` |
| `mla_doubling_wide_sat` | sat(acc + sat(2 × a × b)), the same formats | `vqdmlal.s16` |
| `mla_wide` | acc + a × b, wrapping: `Q16Half<A>` × `Q16Half<B>` → `QVec<A+B>` | `vmlal.s16` |

A kernel keeps `Argon` at its interface and converts with `from_raw` and `raw()`, so its callers are unchanged.
Helpers private to one family of kernels may pass the vectors between themselves: the windowed sinc's `SincWindow`
holds `Q16Vec<15>`, for its blend, its dot product and their two window loaders.
`lane(i)` reads one lane through memory, for tests. The lane spec (`tests/dsp/fixed_point_neon_spec.cpp`) checks each
operation lane by lane against the scalar operation: on the x86 host through SIMDe, and under qemu on the A9's own
instructions.

## Where the types are used

- **Phase:** `Phasor` and `SyncedPhasor` hold, take and return `UQ<32>`; a step is `add_wrap`. The crude oscillators
  forward their phase source's type. The table, pulse and wavetable oscillators and the synth's sources keep raw
  `uint32_t` phase interfaces and convert where they meet a phasor; table and gather positions stay integer.
- **Table oscillators:** the table, pulse and sine oscillators read `{entry, next}` Q15 pairs as `Q16x2Vec<15>` and
  interpolate with `mla_doubling_wide_sat`: the entry widened to Q31 plus the doubled product of a Q15 slope and a Q15
  weight. The pulse interpolates square A with negated weights (its polarity flip) and ring-modulates with
  `scale_shl_wrap<1>(mul_doubling_r_sat(a, b))`. `sine_fm_operator` stays an integer lerp (Q15 × Q16 is Q31).
- **Supersaw:** the phases are `UQVec<32>` and each saw is its phase read as Q31; the side saws are summed at
  2^−kShift scale (`add_scale_shr_floor`, `vsra`), and both gains are Q29, the side gain times 2^kShift, so the mix is
  Q29.
- **Gain:** a `Ramp` is a `Q<31>` gain and slope; `scale_shl_wrap<N>(ramp)` is the per-wave doubling. The crude VCA
  (`CrudeAmplitude`) takes the high word of sample × gain with `mla_r`, so its product is Q30. The band-limited VCA
  (`Amplitude`) holds its Q31 amplitude at Q30 (`QVec<30>`) and takes the doubled high word with `mul_doubling_sat`,
  so its product is Q30 too, summed into a mix read as Q30. Each wave sets the amplitude: the band-limited saw, square
  and pulse pass `scale_shl_wrap<1>` of the voice's amplitude.
- **Frames:** `StereoSample`'s `+` and `+=` wrap. `panned_mono` and `panned_stereo` take `Q<30>` amplitudes, unity at
  2^30, and keep `q31_ops.h`'s multiply until the voice kernel moves.
- **Curves:** `instant_tan` and `release_rate` blend Q28 and Q31 table entries at Q31 weights; the square and linear
  param curves take a Q29 combination and read the neutral as Q31, so each product is reformatted back to the
  neutral's units; `exp_curve`'s blend is Q30 of 2^fraction. `quick_log` stays integer (2^25 per octave).
- **Hard sync:** the phases and increments are `UQ<32>`, read as Q31 for the signed multiplies; the resetter
  reciprocal is `Q<16>`, 1 / the resetter increment read as Q31, so an overshoot over the increment is a fraction of
  a sample at Q15.
- **VCA and mix:** the crude VCA's amplitude is Q31 into a mix read as Q30; `SendAndVolume`'s volumes and gains are
  Q27 (2^27 unity) with Q30 pan amplitudes; the track monitor's amplitude is Q30 (2^30 unity); the dry/wet blend is
  Q30 reformatted to Q31.
- **Shapers:** `tanh<D>` saturates with `scale_shl_clamp_then_shift`; the run-time drive keeps
  `lshift_and_saturate_unknown`. The wavefolder's gain is Q24. The sample-rate reducer's positions and rates are Q22,
  its grabs Q30. Table positions and bit-depth masks stay integer.
- **Interpolation:** the bilinear table interpolator's rows are Q16 and its result Q15 of the table's units; the 1-D
  lerps stay integer. The stutter blend's Q16 strengths are reformatted to Q30. The windowed sinc blends Q15 kernel
  rows (`add_wrap`, `mul_doubling_sat`, `sub_wrap`) and weights a Q15 window into a Q30 sample (`mla_wide`, then
  `sum_wrap`); the wavetable morph weights the Q30 cycle difference by the morph read as Q31 (`mla_r`), so a morphing
  wavetable's sample is Q29, where a single-cycle wavetable's, the sinc's own, is Q30. The sample interpolator's linear
  path stays an integer lerp, Q30.
- **Filters:** the one-pole's step rounds (x − z) × m to Q30 and doubles it back to Q31 (`mul_r_as_wrap`); a ladder
  reads its feedback by a Q30 amount, z × amount at Q31 (`OnePole::feedback`) or at Q29 to sum four
  (`feedback_unshifted`). `curve_frequency` gives tan(f) at Q28 and 1 / (1 + tan(f)) and the moveability at Q31, the
  reciprocal saturated at cutoff 0. The low-pass ladder's resonance as fed back, feedbacks and normaliser are Q30 and
  its loop gains Q26; the 12dB ladder's 2m − 1 is m read as Q30 less one, and the last section's 1 / (1 + tan(f)) is
  read as Q30, twice its value, as the ladder is tuned. The high-pass ladder's loop reciprocal is Q28 and its output
  normaliser 1 / 2R at Q25; the morph's nudge, m + 8x × morph, reads m as Q30 for one `mla_r`. The SVF's rate is
  fc × 1.125 (an eighth as a `UQ<32>` multiplier), its passes run on the Q30 scaled input read as Q31, and the blend
  is three times its Q30 sum read as Q31. The shelf pair's gains are Q29 (one at a flat knob); its bass step is the
  Q30 product read as Q31, half the treble's. The reciprocals are double divisions and stay raw, as do `quick_log`'s
  octaves and the table positions. The high-pass ladder places its saturator with `q31_ops.h`'s asm saturate: the
  builtin a typed saturate compiles to is sunk out of the loop, and the rescheduled loop ran slower on the A9.
- **Buffers and frame fields** stay raw `q31` until the kernels that read them use the named operations; each
  kernel's interface stays raw and converts at its entry and exit.

## Measuring

Device numbers come from the DSP bench (`device_benchmarking.md`). Two images whose code is identical can still
differ by up to 10% if a layout change moves a hot loop across a fetch boundary. Comparing images that differ is
only valid with the benched code's functions 32-byte aligned: bench builds only, the bench sources plus
`deluge_dsp_fixed` and `deluge_effects`. Aligning every function in the image overflows SRAM.

With that alignment, a layout-only control image matches its base to a median of 0.02%. Put the forms being
compared into one image where possible.

A change meant to keep the machine code is checked with `tools/compare_codegen.py BASE.elf NEW.elf`. It disassembles
both images and compares them function by function, with everything the layout sets masked: addresses, pool offsets,
trap fill and ThinLTO's renamed locals. `--diff NAME` prints a function's masked diff. ThinLTO's decisions move with
any source change, so an empty result is not the gate. Each changed function is classified:
- Scheduling or register allocation outside a per-sample loop is recorded in the commit.
- A changed per-sample loop, a new call or new NEON is benched.

## The edges

- **Buffers** are declared as `Q<F>` arrays by the code that owns them. A single audited raw `int32_t` view sits at
  the codec/DMA edge. There is no `may_alias` and no reinterpreting of buffers.
- **`scripts/check_fixed_point_raw.py`** counts two things per file outside `fixed_point.h` and `q31_ops.h`:
  - raw sites, meaning raw helper calls and DSP-instruction asm;
  - boundary crossings, meaning `from_raw`, `.raw()`, `->raw()` and `reinterpret_bits`.

  `scripts/fixed_point_raw_allowlist.txt` pins the raw counts, and they may only shrink. A count below its entry also
  fails until `--write` records it, so freed headroom can't be spent again. Boundary crossings are recorded but not
  gated: while interfaces still take raw `q31`, moving code onto the typed operations adds them. Moving a file onto
  the typed operations is the unit of progress.
- **What the ratchet can't see:** bare `int32_t` audio arithmetic (`+` or `<<` on samples) looks like any other
  integer code, so reviews have to catch it.
- **Doc comments** still give the physical meaning where the type can't: cycles per sample, unity gain, a legacy
  quirk the code keeps.
