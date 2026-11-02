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
Thumb-2. The app's files are Thumb-2; dsp-next's own libraries are ARM. The only inline asm left is the fixed-point
VFP `vcvt`, which clang does not form by itself.

`tests/unit-arm/codegen` disassembles each operation built with the shipping flags (`-O2 -fno-inline-functions`, both
instruction sets) and fails on any other instruction sequence. `tests/unit-arm/spec/fixed_point_isa_spec.cpp`
compares every operation, and its portable version, against the real instruction under qemu.

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

App files build with `-fno-inline-functions`, which turns every `std::` helper they call (`std::bit_cast`,
`std::clamp`, `std::numeric_limits<>::max()`) into an out-of-line call. Code on the audio path uses builtins,
comparisons and `constexpr` constants instead.

### Why the high-word multiplies stay asm in `q31_ops.h`

`q31_ops.h`'s raw helpers, which the code not yet moved to the types still calls, keep their inline asm.

Clang has no intrinsic for `smmul`, so plain C means the idiom `(int32_t)(((int64_t)a * b) >> 32)`. Clang's loop
vectorizer widens loops of that idiom into NEON, which is 3–7× slower on the A9: `vmull.s32` + `vshrn.i64`, or a
`umull` per lane with values shuttled through `vmov.32`. Empty-asm barriers can stop it, but they cost 10–40% in
other kernels.

Measured on the device with a layout-stable bench (below): every rebuilt form of the helpers moved single kernels
by ±10–40%. The kernels move onto the typed operations one at a time instead, each benched.

`TODO.md` tracks the LLVM side: smmul-family intrinsics, the NEON cost model, and a scalar ×2 unroll that is 22–30%
faster on the A9.

## Measuring

Device numbers come from the DSP bench (`device_benchmarking.md`). Two images whose code is identical can still
differ by up to 10% if a layout change moves a hot loop across a fetch boundary. Comparing images that differ is
only valid with the benched code's functions 32-byte aligned: bench builds only, the bench sources plus
`deluge_dsp_fixed` and `deluge_effects`. Aligning every function in the image overflows SRAM.

With that alignment, a layout-only control image matches its base to a median of 0.02%. Put the forms being
compared into one image where possible.

## The edges

- **Buffers** are declared as `Q<F>` arrays by the code that owns them. A single audited raw `int32_t` view sits at
  the codec/DMA edge. There is no `may_alias` and no reinterpreting of buffers.
- **`scripts/check_fixed_point_raw.py`** counts each file's raw helper calls, DSP-instruction asm and `from_raw` /
  `.raw()` / `reinterpret_bits` uses outside `fixed_point.h` and `q31_ops.h`. `scripts/fixed_point_raw_allowlist.txt`
  pins those counts and may only shrink. Moving a file onto the typed operations is the unit of progress; rewrite the
  list afterwards with `--write`.
- **What the ratchet can't see:** bare `int32_t` audio arithmetic (`+` or `<<` on samples) looks like any other
  integer code, so reviews have to catch it.
- **Doc comments** still give the physical meaning where the type can't: cycles per sample, unity gain, a legacy
  quirk the code keeps.
