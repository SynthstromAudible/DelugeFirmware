# dsp-next: the component architecture

The shape every `dsp-next` component takes, and the conventions every component
follows. `dsp-next` (`src/deluge/dsp-next/`) is the L0 DSP library: freestanding,
header-first C++23 that may include only `util/`, `definitions*`, Argon, the
standard library and its own headers (`scripts/check_dsp_boundary.py`). Related:
[synth_phase1_l0_plan.md](synth_phase1_l0_plan.md),
[synth_oscillator_plan.md](synth_oscillator_plan.md).

Every brief that moves or writes a component cites this document, and every
reviewer applies its [review checklist](#review-checklist).

## Prime directive: structural, not behavioural

Moving code into `dsp-next` changes **code organisation only**. Every step is
**bit-exact**, gated on the golden hashes (x86) and the ARM oracle. A component
takes the shape described here but carries the synth's exact arithmetic. Audible
improvements (analytic waves, a unified VCA arithmetic, better band-limiting,
per-voice noise) are separate, ear-validated, re-baselined steps. A commit that
moves code must not change a single output sample.

A component is not a transcribed class. A legacy class moved field for field,
with its verbs, callbacks and globals, is not a component, however bit-exact it
is. The shape (the concepts, the mixins, the settings convention, injected noise)
is what the review gates on.

## Layer 1: types (`core/sample.h`, `core/types.h`, `core/stereo_sample.h`)

```cpp
template <class T> using Sample = T;
template <class T> using Buffer = std::span<T>;        // writable block
template <class T> using Signal = std::span<const T>;  // read-only block
struct StereoSample { q31 l, r; };                     // one interleaved frame
```

- `sample.h` holds the scalars (`q31`, `f32`, `SampleType`, the conversions);
  `types.h` builds the block vocabulary on it.
- The backend namespaces carry concrete aliases. `fixed` is the bit-exact q31
  backend (`fixed::Sample`, `fixed::Buffer`, `fixed::Signal`,
  `fixed::StereoSignal`, `fixed::StereoBuffer`). `fp` is the float edge backend
  (`fp::Sample`, `fp::Buffer`, `fp::Signal`). The kernels live in the same
  namespaces (`fixed::sine`, `fixed::TableOscillator`, ...).
- The sample type at component interfaces is **`q31` (= `int32_t`)**, so app
  buffers pass straight in. `FixedPoint<31>` is a later, layout-compatible
  type-safety layer.
- `types.h` includes no Argon, so code that names only block types (the effects
  library, for one) needs no Argon on its include path. The SIMD stereo form,
  `StereoVector`, lives in `core/stereo_vector.h`.

## Layer 2: concepts, mixins and dispatch (`core/`)

### Concepts (`core/concepts.h`)

Each family has a block concept, a per-sample concept, and the composite of the two:

| Family | Per-sample (`sample_*`) | Block (`block_*`) |
|---|---|---|
| `generator` | `render() -> value_type` | `renderBlock(Buffer)` |
| `processor` | `render(value_type) -> value_type` | `renderBlock(Signal in, Buffer out)` |
| `mixer` | `render(a, b) -> value_type` | `renderBlock(Signal a, Signal b, Buffer out)` |

and two more:
- **`control_generator`**: `advance(std::size_t n) -> value_type` returns the
  value after `n` samples. It is for block-rate control signals, whose
  arithmetic is per block, so it has no per-sample `render`.

  `advance(n)` is reserved for these block-rate sources. A source whose
  arithmetic is per sample exposes `render()`. The one exception is
  `Phasor::advance()`, the per-sample phase step inside the oscillators, which
  keeps its name.
- **`converter`**: `render(input_type) -> output_type`, between numeric domains.

Every component exposes `value_type`. A stateful component is checked against its
concept by a `static_assert` next to its definition, together with
`!std::is_polymorphic_v<C>`.

The `sample_*` concepts take `value_type` (`q31`), so a SIMD component, whose
`render` takes and returns `Argon<q31>`, satisfies only the `block_*` concept and
is asserted against that. Vector-aware `sample_*` concepts are a follow-up.

### Mixins (`core/generator.h`, `core/processor.h`, `core/mixer.h`)

`Generator<D, T>`, `Processor<D, T>` and `Mixer<D, T>` are **non-virtual CRTP
mixins**: they add no vptr and no state. Each supplies `renderBlock` from the
component's `render`. The component writes only `render`, `[[gnu::always_inline]]`,
and the mixin picks the path from `render`'s signature. A component never names
a path.

| `render` takes and returns | Path | Fill |
|---|---|---|
| `Argon<T>` (arithmetic `T`) | SIMD | `argon::vectorize::for_each` |
| `StereoVector` (`T` is `StereoSample`) | SIMD stereo | `argon::vectorize::for_each_interleaved<q31, 2>`: de-interleave on load, re-interleave on store (`vld2`/`vst2`) |
| `T` (`q31`, `f32`, `StereoSample`) | scalar | a plain index loop over raw pointers |

**Everything per sample is `[[gnu::always_inline]]`.** The app's translation
units build with `-fno-inline-functions` (`CMakeLists.txt`), under which clang
marks every function definition `noinline` unless it is `always_inline`, and
ThinLTO keeps that. The mixins are header templates instantiated in those
translation units, so `dsp-next`'s own `-finline-functions` does not reach
them. The mixins' per-step lambdas are therefore `[[gnu::always_inline]]` and
their scalar path is a plain loop with no library calls in it, and every
function a component calls per sample or per vector (its `render`, the
`Phasor` accessors, `util::constexpr_for` and its lane lambdas, the noise
step) is `[[gnu::always_inline]]` as well. Code that runs once per block needs
no attribute.

**The tail rule.** `renderBlock` writes every sample of the block, whatever its
length. On the SIMD paths a block that is not a lane multiple ends with one
partial step:
- `Generator` calls `render()` once more and stores only the lanes inside the
  block, so `render()` runs exactly `ceil(size / lanes)` times and a generator's
  state advances by whole vectors, the partial one included.
- `Processor` and `Mixer` load the lanes past the end as zero, run them through
  `render` with the rest, and do not store them. A stateful SIMD processor or
  mixer therefore advances once on the partial step; an owner that keeps one
  across blocks accounts for that (the VCA, `Amplitude`, is built per block).

Argon's `for_each` and `for_each_interleaved` keep the partial step's data in the
vector registers on NEON: the tail is a predicated load and store, with no
transfer of lanes to core registers.

**In place.** Each step loads before it stores, so a processor's `in` and `out`,
or a mixer's `out` and either input, may be the same buffer. The app calls
`renderBlock(in, out)` with `in` and `out` the same buffer; every path gives the
same result in place as out of place.

**Writing `renderBlock` directly.** A component writes `renderBlock` itself only
when its arithmetic genuinely is not per sample: irregular sync windows
(`SyncedOscillator`, `WavetableSyncOscillator`), wavetable cycle chunks
(`WavetableOscillator`), and tape I/O staged per block (the delay). Such a
component satisfies only the `block_*` concept and follows the same tail and
in-place rules. Arithmetic that is per sample is a `render` under a mixin.

The crude oscillators and `CrudeAmplitude` are per-sample arithmetic that still
carries hand-written block loops; giving them a scalar `render` is a follow-up,
and they are not a precedent.

### Runtime selection (`core/as_block.h`)

`BlockGenerator<T>`, `BlockProcessor<T>` and `BlockMixer<T>` are the virtual
block interfaces, for a component chosen at runtime. **`AsBlock<C>` is the only
route to them**: it holds a concrete component by value and implements the
interface its `block_*` concept names. The block call is virtual; everything
inside it stays inlined, so the virtual cost is paid once per block (32 to 128
samples), never per sample.

No component inherits a virtual interface. The synth's oscillator shapes
(`src/deluge/synth/source/va_shapes.cpp`) instantiate components statically;
the oscillator registry is the first user of `AsBlock`.

## Settings, Coefficients, State

A stateful component has three parts:
- **`Settings`**: a plain value type holding what its owner sets (knob values,
  mode, rate).
- **`Coefficients coefficients(const Settings&)`**: a pure function, `constexpr`
  where the arithmetic allows (float arithmetic does not). It is specced on its
  own.
- **`State`**: the running memory.

The verbs:
- `configure(const Settings&)` stores `coefficients(settings)`.
- `render` reads the coefficients and mutates only the state.
- A value the owner needs back, such as a filter's gain compensation, is a field
  of `Coefficients`, read through `coefficients()`. There are no out-parameters.
- A mode that changes the per-sample arithmetic is a compile-time policy,
  dispatched once per block. It is never a per-sample branch on settings.
- `reset()` returns `State` to its value-initialised state, or to a seeded
  state the component documents, and never touches `Coefficients`. A quirk the
  arithmetic requires after a reset, such as a filter's post-reset sentinel
  value, is named in the code and documented on `reset()`.

This is a documented pattern, not a base class.

## Stereo

- **Frames.** A stereo block is a span of `StereoSample` frames
  (`fixed::StereoSignal`, `fixed::StereoBuffer`). `core/stereo_sample.h` asserts
  the layout (standard layout, `l` at offset 0, `r` at 4, size 8), so a block of
  frames is an interleaved array of q31, left first. That one assert covers both
  `as_frames` and the SIMD stereo path.
- **`as_frames`** (`core/stereo.h`) views an interleaved app buffer as frames:
  `size / 2` frames, an odd trailing sample outside the view. It takes any
  contiguous, sized range of q31. A const range gives a `StereoSignal`, a
  mutable one a `StereoBuffer`. A range that owns its storage must be an lvalue
  (or the range borrowed), since a view of a temporary would dangle.
- **`Stereo<P>`** (`core/stereo.h`) runs a mono q31 `sample_processor` on each
  channel: one `P` per channel, itself a `Processor<Stereo<P>, StereoSample>`.
  `render(frame)` renders the left channel completely and then the right, so a
  shared noise stream is drawn left then right on every frame, bit-exact.
  `configure(const P::Settings&)` sets both channels from one `Settings`; it is
  present only when `P` has `Settings` and `configure`.
- **Native stereo.** A component that is stereo by nature, such as the
  compressor or the delay, is written directly over `StereoSample`.
- **SIMD stereo.** A stereo component whose `render` takes and returns
  `StereoVector` (one `Argon<q31>` per channel, lane `i` of each the same frame)
  gets NEON's interleaved loads and stores from the mixins, so its arithmetic
  runs on whole channel vectors with no shuffles.

## Noise (`fixed/noise/`)

- **`CongruentialStep`** is the single definition of the synth's noise step,
  `x' = 69069·x + 1234567` (uint32, wrapping). The app's `CONG` macro
  (`util/waves.h`) delegates to it, so the app and L0 cannot diverge.
- **`SharedNoise`** is a stateless `sample_generator<q31>`. Its `render()`
  advances the synth's one shared stream (the app's `jcong`, through `CONG`) and
  returns the new state as q31. **It is the only place L0 touches shared
  state**: every consumer, app and component alike, must draw from the one
  stream in call order for the output to stay bit-exact.
- **`SeededNoise`** runs the same step on its own `uint32_t`. Specs use it, as
  does anything that wants a private stream.
- **Noise is a template parameter.** A component that draws noise takes it as a
  template parameter, so `SharedNoise` inlines to the same code as a `CONG` site
  and the component carries no pointer. The app spells the instantiation out
  once. The ladder filter, which draws through `CONG` directly, comes under this
  rule as `LowPassLadder<Noise>` when the filter family is rewritten.

## Slotting-ready rules

Lane-parallel SIMD processes independent streams, one per NEON lane: four voices'
filters in one `int32x4` pass. It is its own arc, after the component rewrites.
These rules keep that arc a matter of adding kernels rather than rewriting
components, and they bind every rewrite:

1. `Coefficients` and `State` are plain value structs, with no pointers and no
   hidden members. A lane kernel can then be built from N scalar ones (packed
   SoA) and can reuse the scalar coefficient function per lane.
2. Mode is a per-block policy (see [Settings, Coefficients, State](#settings-coefficients-state)).
   Lanes must agree on mode, never on settings.
3. Nothing is hidden: no globals, and noise is injected.
4. The scalar component is the reference. A lane kernel is a separate component,
   checked lane by lane against it.

### Target API for the lane arc

```cpp
template <class K> concept lane_kernel = /* K::Scalar (the scalar component), K::Coefficients<N>, K::State<N>
                                           (SoA packs), static Vec step(const Coefficients<N>&, State<N>&, Vec) */;

template <lane_kernel K, std::size_t N = 4>
class Lanes {
public:
	using value_type = LaneVector<q31, N>;
	void configure(std::size_t lane, const typename K::Scalar::Settings&); // scalar coefficients, packed
	void reset(std::size_t lane);
	void park(std::size_t lane);                                      // idle: frozen, output ignored
	value_type render(value_type in);
	void renderBlock(LaneSignal in, LaneBuffer out);                 // lane-interleaved chains
	void renderBlock(std::array<fixed::Signal, N> in, std::array<fixed::Buffer, N> out); // 4x4 transpose
	typename K::Scalar::State state(std::size_t lane) const;
};
```

How lanes behave:
- A `Lanes` instance serves one mode. The voice engine groups voices by mode,
  and each lane has its own settings.
- A noiseless stereo voice takes two lanes, one per channel.
- A kernel whose noise policy is `SharedNoise` cannot satisfy `lane_kernel`.
  Drawing for four voices at once would reorder the shared stream. Per-voice
  noise is a deliberate, ear-checked, re-baselined step, and it belongs to the
  lane arc.

NEON on the Cortex-A9 has a 64-bit datapath, so expect roughly 2× on
lane-parallel filter arithmetic, not 4×.

## Layer 3: oscillator components

Each oscillator is a **stateful generator** that owns its phase:

- `Phasor` (`core/phasor.h`): the phase accumulator (`advance()`, `seek()`,
  `setIncrement()`), the single home for phase state.
- `fixed::sine`: the sine kernel.
- `TableOscillator`: holds `std::span<const int16_t>` (magnitude from
  `.size()`); `render()` is the synth's exact table gather and interpolation.
  Backs the band-limited saw, square, triangle and analog waves; only the table
  differs.
- `BandLimitedTableOscillator`: a `TableOscillator` that picks its band from the
  phase increment.
- `PulseOscillator`: the PWM dual-square.
- The crude oscillators (`CrudeSaw`, `CrudeSquare`, `CrudeTriangle`, `CrudeSine`):
  the scalar analytic forms used at low pitch and as the CPU-dire fallback. Each
  takes its phase source as a template parameter: `Phasor` free-running, or
  `SyncedPhasor` for hard sync.
- `WavetableOscillator`: renders a `WaveTableData` view of the bands; the app's
  `WaveTable` owns loading and band management.

Cross-cutting pieces are components too, not per-type branches:
- `SyncedOscillator<Osc>` (`fixed/sync/`): hard sync as a generator that wraps a
  generator, taken by template parameter. It owns the resetter and seam state,
  restarts the wrapped oscillator at each resetter crossover, crossfades the
  seam, and fills each window to its exact length with the wrapped oscillator's
  `renderBlock`. `WavetableSyncOscillator` is the wavetable's form.
- Band selection: a band-limited table set (`BandlimitedTable`) picks its band
  from the phase increment. The choice between a crude and a band-limited
  oscillator depends on the band index and the app's quality measure
  (`AudioEngine::cpuDireness`), and stays with the app.

**Phase ownership.** The generators own phase; the voice drives them per block,
setting phase and increment and reading the phase back for the next block. The
phase advance is identical to the synth's, so this is bit-exact. Moving phase
state fully into the engine is later work.

## Layer 4: processors and mixers

- `Amplitude` (`fixed/vca/`): the VCA, a SIMD `Mixer`:
  `out = raw * amplitude_ramp + mix`, with `vqdmulhq` arithmetic.
  `CrudeAmplitude` is the crude paths' VCA, a separate component because its
  arithmetic (`multiply_accumulate`, rounded) differs. Unifying the two is an
  audible, re-baselined step.
- Mixing into the voice's buffer (unison and oscillator summing) is a `Mixer`.
- Filters, dynamics, delay and effects follow the same component model, under
  the settings convention and the slotting-ready rules.

## Bit-exactness: how the synth's math maps in

- Generator then VCA: `Oscillator.renderBlock(raw)` and then `Amplitude` over
  `raw` are the same operations reordered across a temporary buffer, so the
  output is identical. Raw output (no amplitude) is "don't run the `Amplitude`
  mixer".
- Kernels are the synth's exact arithmetic (`ShiftLeftLong<16>`,
  `MultiplyDoubleAddSaturateLong`, the crude analytic forms), never a cleaner
  equivalent.
- A generator fills exactly its `Buffer`, so no write lands past the block.
- Constants and arithmetic quirks the output depends on are named in the code
  (a named constant, or a comment on the quirk), so a reader can tell them from
  mistakes.

## Remaining work

- **The oscillator registry.** The `OscType` choice becomes a block-level
  registry composing generator, band selection, sync and `Amplitude` behind
  `AsBlock`.
- **Vector-aware `sample_*` concepts**, so SIMD components satisfy the composite
  concepts.
- **A storage-independent wavetable renderer.** `WaveTable::render` is pure DSP
  over in-memory bands. The renderer moves fully into `dsp-next` behind a
  band-data port (the band tables, magnitudes and kernels) that the app's
  `WaveTable` provides, with SD loading, band management and stealing staying in
  the app. It is gated on the wavetable golden fixtures (the NE10 int32 FFT is
  bit-exact x86 against ARM).
- **Audible steps** (separate, ear-validated, re-baselined): VCA-arithmetic
  unification, analytic-wave options, band-limiting and sync improvements,
  `FixedPoint<31>` types, full phase relocation into the engine, per-voice
  noise, the L1 `SourceEngine`/CLAP boundary.

## Review checklist

Every reviewer of every component change applies these, alongside the golden
gates:

- Does each stateful class satisfy its concept, by `static_assert` next to its
  definition (and `!std::is_polymorphic_v`)?
- Does it map field for field onto a legacy class? It should not: a component
  has `Settings`, `Coefficients` and `State`, not the legacy class's members and
  verbs.
- Are constants and bit-exactness quirks named?
- Are there callback sinks, raw-pointer heads, out-parameters, or globals other
  than `SharedNoise`?
- Is there a hand-written `renderBlock` where the arithmetic is per sample? It
  should be a `render` under a mixin (see
  [Writing `renderBlock` directly](#mixins-coregeneratorh-coreprocessorh-coremixerh)).
- Are there public verbs beyond `render`, `renderBlock`, `configure`,
  `coefficients`, `reset` and `advance`, other than accessors?
- Is there a per-sample branch on mode, where a per-block policy belongs?
- Are `Coefficients` and `State` anything other than plain value structs?
- Is noise read (through `CONG`, `jcong` or a held pointer) rather than
  injected as a template parameter?
- Are the mono and stereo paths duplicated, where `Stereo<P>` or a native
  stereo `render` would serve?
- Do comments paraphrase the legacy code, rather than state what the component
  does and why?
- Is every function on the per-sample path, the mixin glue included,
  `[[gnu::always_inline]]`? The app's translation units build with
  `-fno-inline-functions`, so anything else is an out-of-line call per sample
  or per vector in the shipping image.
