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
is. The shape (the concepts, kernels and their wrappers, the settings convention,
injected noise) is what the review gates on.

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
  arithmetic is per block, so it has no per-sample `render`. `Control<K>`
  ([Wrappers](#controlk-corecontrolh)) satisfies it.

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

**New components are kernels.** Every new component, and every component a
rewrite moves, is written as a kernel and wrapped by `Mono`, `Stereo` or
`Control` (see [Kernels](#kernels-corekernelh-coremodeh)). The mixins described
here remain for the existing oscillators and `Amplitude` until they move onto
kernels (see [Remaining work](#remaining-work)); no new component uses them. The
in-place and inlining rules below bind the wrappers in the same way, and so does
the tail rule, with one difference: a wrapper running a kernel that declares
`kPartialStepAdvancesByCount` advances its state on the partial step by the
active-lane count, not by a whole vector (see
[The partial step](#the-partial-step)).

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

**Writing `renderBlock` directly.** The existing components that write
`renderBlock` themselves do so because their arithmetic is not per sample:
irregular sync windows (`SyncedOscillator`, `WavetableSyncOscillator`) and
wavetable cycle chunks (`WavetableOscillator`). Such a component satisfies only
the `block_*` concept and follows the same tail and in-place rules. In new code,
arithmetic that is per sample or per vector is a kernel's `step`, and
arithmetic that reads a whole block, shapes it and writes it back (the delay's
tape I/O) is a staged kernel's `process`; neither is a hand-written
`renderBlock`.

The crude oscillators and `CrudeAmplitude` are per-sample arithmetic that still
carries hand-written block loops; they move onto kernels with the rest of the
mixin components, and they are not a precedent.

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

## Kernels (`core/kernel.h`, `core/mode.h`)

A **kernel** is a stateless type holding a component's arithmetic. It owns no
data and runs in no loop of its own: the wrappers ([Mono, Stereo and
Control](#wrappers)) are the only loops, and they own the `Coefficients` and
`State`. Every new component, and every stateful piece of one, is written as a
kernel and wrapped: per-sample or per-vector arithmetic as a `step`,
block-staged arithmetic as a `process`, a block-rate signal as an `advance`.
This is a documented pattern checked by the `kernel` concepts, not a base
class.

### The parts

- **`value_type`**: a block's sample (`q31` or `StereoSample`).
- **`step_type`**: what `step` takes and returns. It is `value_type` for a
  scalar kernel, `Argon<q31>` for a vector kernel over q31 blocks, and
  `StereoVector` for a vector kernel over stereo frames. These are the pairs the
  wrappers have loops for; any other pair fails the concept. A control kernel
  sets `step_type` to `value_type`: its block is its step.
- **`Settings`**: a plain value type holding what the owner sets (knob values,
  mode, rate).
- **`Coefficients`**: what the arithmetic reads, computed by the pure
  `static Coefficients coefficients(const Settings&)` (`constexpr` where the
  arithmetic allows; float arithmetic does not). It is specced on its own. A
  value the owner needs back, such as a filter's gain compensation, is a field
  of `Coefficients`, read through the wrapper's `coefficients()`; there are no
  out-parameters.
- **`State`**: the running memory. `State{}` is the reset state.
- **`Block`** (optional): per-block values that depend on the coefficients and
  the state, such as a ramp's start and increment. A kernel that declares a
  `Block` must declare a `prepare`.
- **`Mode`** and **`Modes`** (optional): `Modes` is a `ModeList<...>` of the
  enumerators the kernel's `step` is instantiated for, and `Coefficients`
  carries the chosen one in a `mode` member of that enumeration. A moded
  kernel's `step` (or `process`) is a template on the mode. All modes share one
  `State`. A default-constructed wrapper holds `Coefficients{}` and dispatches
  on its `mode` at the first block, so `Coefficients{}` must be a constant
  expression naming a listed mode; the concept checks it. An enumeration member
  defaults to its zero enumerator, so a kernel that does not list that one gives
  `mode` a default member initialiser.

`Coefficients`, `State` and `Block` are plain value structs:
default-constructible and trivially copyable, with no pointers and no hidden
members.

### `coefficients()` and `prepare()`

`coefficients(settings)` is pure and runs on `configure`, not per block.

`prepare` is optional and runs once per block:
```cpp
static Block prepare(const Coefficients&, State&, std::size_t n);
```
It is detected by name, so a `prepare` of any other shape (a template on the
mode, a non-static member, a mismatched overload) fails the concept instead of
being skipped. The signature is matched by converting `&K::prepare` to a pointer
to that exact function type, so a template whose parameters deduce to it is
accepted, and a `prepare` taking any other type is rejected, never converted. `n` is the block's length and may be 0: an empty block still prepares.
The wrapper runs `prepare` on the wrapper's own state, before it takes the
local copies the loop runs on, so the loop's state stays in registers.

### The five forms

A kernel takes exactly one form; a class providing two satisfies none.

| Form | Concept | Provides |
|---|---|---|
| Generator | `generator_kernel` | `step(c, b, s) -> step_type` |
| Processor | `processor_kernel` | `step(c, b, s, x) -> step_type` |
| Mixer | `mixer_kernel` | `step(c, b, s, a, b2) -> step_type` |
| Staged | `staged_kernel` | `process(c, s, Signal in, Buffer out)` |
| Control | `control_kernel` | `advance(c, s, n) -> value_type` |

`c` is the `Coefficients`, `b` the `Block` and `s` the `State`. **`process` is
for block-staged arithmetic only**: arithmetic that reads a whole block, shapes
it, then writes the whole block (the delay's tape I/O). Arithmetic that is per
sample or per vector is a `step`. A staged kernel has no `Block`, so it declares
no `prepare`; its per-block work is `process` itself. A staged kernel may be
moded, with `process<M>`.

**`process` works in place.** The app renders with `in` and `out` the same
buffer, and every wrapper path gives the same result in place as out of place,
so `process` must too. The wrapper's loops get this from loading each step
before storing it; a `process` that reads a sample after writing one it has not
yet read (a reversal written front to back, a look-ahead) stages what it still
needs before overwriting it.

A moded kernel's `step` or `process` is checked at the first mode of its
`Modes` only; a step ill-formed at another mode is reported where the wrapper
instantiates it.

### The control form

A **control kernel** is a block-rate signal: an envelope or a duck that the
owner reads once per block and applies as one value. Its arithmetic is per
block, so the block is its step.

```cpp
static value_type advance(const Coefficients&, State&, std::size_t n);
```

- **`advance` moves the signal on by a whole block** of `n` samples and returns
  its value after the block. `n` may be 0: an empty block still makes one call.
- **No `prepare`, no `Block`.** The per-block work is `advance` itself, so a
  control kernel that declares a `prepare` or a `Block` fails the concept.
- **A scalar step.** `step_type` is `value_type`, and there is no
  `kPartialStepAdvancesByCount`, having no vector to end partially.
- **Moded `advance`.** A moded control kernel's `advance` is a template on the
  mode, `template <Mode M> static value_type advance(...)`, with that signature
  at every listed mode. All modes share one `State`.
- **The signature is exact.** `advance` is matched like `prepare` and `reset`:
  its address is converted to a pointer to that function type, so a function
  template whose parameters deduce to exactly that type is accepted, and an
  `advance` taking its count as any other type (an `int`, or a `uint32_t` where
  that is not `std::size_t`) is rejected, never converted.
- **Events are functions on `State`.** What happens between blocks (a hit, a
  stop) is a function taking the kernel's `State` (and optionally its
  `Coefficients` first), run on the wrapper's state through `Control::on` (see
  [`Control<K>`](#controlk-corecontrolh)).

`fixed::DuckEnvelope` (`fixed/dynamics/duck_envelope.h`) is the reference control
kernel: the sidechain's duck, with `advance`, the events `hit`, `hit_at` and
`stop`, and its quirks named where they are carried. Two hits arriving before a
block combine by `combine_hit_strengths`, which `fixed/dynamics/hit_strength.h`
holds as the one home of that rule. The header is free of the kernel core, so a
caller that only combines hits needs no argon.

A zero rate is a `DuckEnvelope` stage that never ends: the stage holds at its
start, and placing a late hit divides by no zero. The owner's settings give one,
not only an envelope never configured: the sidechain reads a song file's
attack or release of 0 as 0, and a long rate synced at the whole level resolves
to 0 at a slow tempo.

**What not to copy from it.** `DuckEnvelope`'s `Settings` are rates the owner
has already resolved only because tempo sync needs `engine::SyncLevel`, which
dsp-next must not see. A kernel whose mapping from params to rates is pure does
that mapping in `coefficients()`.

### Modes

A mode that changes the per-sample arithmetic is a compile-time policy, chosen
once per block, never a per-sample branch on settings. `with_mode<List>(mode,
body)` compares the runtime mode with each listed mode in turn and calls `body`
once, instantiated for the match, so each mode's loop is compiled separately and
branch-free. A mode outside the list is a precondition violation.

### The `reset` quirk hook

A wrapper's `reset()` returns the state to `State{}`. A kernel whose arithmetic
needs something else after a reset, such as a filter's post-reset sentinel
value, declares `static void reset(State&)`, and the wrapper calls it instead.
The quirk is named in the code and documented on that function. `reset()` never
touches `Coefficients`. Like `prepare`, `reset` is detected by name, so a
`reset` of any other shape (a non-static member, one returning a `State`) fails
the concept instead of being skipped.

### Composition

A kernel composes by nesting. A kernel built from others holds their
`Coefficients`, `State` and `Block` as members of its own, and is itself an
ordinary kernel that `Mono`, `Stereo` or `Control` wraps. There is no
inheritance and no runtime composition. It reaches its parts through the same
calls the wrappers (`Mono`, `Stereo` and `Control`) make, in
`deluge::dsp::kernel_ops` (`core/kernel.h`):

- **`Coefficients`**: its `coefficients()` calls each part's and stores the
  results as members.
- **`Block` and `prepare`**: its `Block` has a `kernel_block_t<Part>` member per
  part, and its `prepare` fills each with
  `kernel_ops::prepare_block<Part>(c.part, s.part, n)`, which calls the part's
  `prepare` when it declares one and gives an `EmptyBlock` when it does not. A
  composite whose parts all lack a `prepare` declares neither.
- **`step`**: threads the value through
  `kernel_ops::step_at<Part, M>(c.part, b.part, s.part, x)` for each part, which
  calls `step<M>` on a moded part and the plain `step` on one without modes
  (pass `kernel_ops::NoMode::NONE` there). A staged or control part is called
  through `kernel_ops::process_at` or `kernel_ops::advance_at` the same way.
- **`Modes`**: a composite dispatches once per block on its own `mode`, so its
  `Modes` must cover every combination of its parts' modes that it runs. With
  one moded part it reuses that part's `Mode` and `Modes` and copies the part's
  mode into its own `Coefficients::mode`; with several, its `Modes` is the
  product of theirs, each of its modes naming one mode per part, and its `step`
  maps its mode to each part's. Keep products small: a part with many modes
  stays its own component.

`SeriesPoles` in `tests/dsp/mono_spec.cpp` is the reference: a moded one-pole
and a one-pole with a `Block`, in series, checked against a hand loop over the
parts. `Chain` fusion, which generalises this, is a later step (see
[Remaining work](#remaining-work)).

### The domain rules

1. **`prepare` is the only scalar per-block work.** Whatever must be computed
   once per block (a ramp's increment, a smoothed coefficient) is computed in
   `prepare`, in whatever arithmetic it needs, and handed to `step` in `Block`.
2. **`Block` holds values in the step's domain.** A vector kernel's `Block`
   holds vectors (already broadcast), so `step` does no scalar-to-vector
   transfer per step.
3. **A composed kernel stays in one domain per step.** A `step` is all scalar
   or all vector; it does not drop to core registers and back inside the loop.

### The partial step

A vector loop over a block that is not a lane multiple ends with one partial
step. Its lanes past the end load as zero and are never stored. By default a
kernel's state advances by a whole vector on that step, which is today's
`Amplitude` behaviour. A vector kernel whose state persists across blocks and
must advance by exactly the samples rendered declares
```cpp
static constexpr bool kPartialStepAdvancesByCount = true;
```
and provides the counted overload, taking an `ActiveLanes{n}` tag after the
other arguments: `step(c, b, s, ActiveLanes)` for a generator,
`step(c, b, s, x, ActiveLanes)` for a processor. The wrapper calls it on the
partial step only, with `n` the number of live leading lanes, 1 up to one less
than the vector's lanes. `ActiveLanes` is a tag rather than an integer so a
counted processor step never reads as a mixer step. A scalar, mixer or staged
kernel has no counted partial step, so declaring the flag on one is a compile
error.

The partial step is told apart by type, which holds on NEON and SIMDe. On
Helium (MVE) every step is predicated, so whole steps would also arrive counted,
with `n` equal to the lane count; a counted kernel must be correct for that.

## Wrappers

### `Mono<K>` (`core/mono.h`)

`Mono<K>` is a kernel as a mono component: one `Coefficients`, one `State`. It
is a generator, processor or mixer to match `K`'s step arity (`render` and
`renderBlock`), and a `block_processor` only for a staged kernel. It is the only
loop a kernel runs in. A control kernel is rejected at the class: it has no
per-sample loop, so a `Mono` of one would be inert.

- **Loop selection** is by `step_type`: an index loop over raw pointers for
  `step_type == value_type`; `argon::vectorize::for_each<q31>` for `Argon<q31>`;
  `argon::vectorize::for_each_interleaved<q31, 2>` for `StereoVector`
  (de-interleave on load, re-interleave on store).
- **Once per block**, `renderBlock` calls `prepare` (when declared), then
  dispatches on `coefficients().mode` through `with_mode` (when moded), then
  runs the one loop. A mode change set by `configure` takes effect at the next
  block, and `State` carries across it untouched.
- **A staged kernel** has no loop: `renderBlock` calls `process` once (or
  `process<M>` through `with_mode`). `staged_kernel` rejects a staged kernel
  that declares `prepare`, since `process` takes no `Block` and the `prepare`
  would be skipped silently.
- **In place.** Each step loads before it stores, so the input and output may be
  the same buffer, on every loop path.
- **Local copies.** The loop runs on local copies of the coefficients and state,
  written back after the block, so they stay in registers even though the output
  may alias the component.
- `render(...)` is a one-sample block: one `prepare` and one dispatch per call.
  A vector kernel renders it as the partial step of a block of one. It builds
  one-sample spans and runs a whole block's glue, several out-of-line
  `std::span` calls per call under the shipping `-fno-inline-functions`, so it
  is a convenience for tests and control-rate values, not for per-sample use in
  shipping code. A component renders blocks; a kernel that needs another kernel
  per sample nests it (see [Composition](#composition)). The same holds for
  `Stereo<K>::render`.

### `Stereo<K>` (`core/stereo.h`)

`Stereo<K>` runs a mono q31 processor kernel on both channels of a
`StereoSample` block. It is a plain class, not a mixin, that satisfies the
`processor` concept with `value_type` `StereoSample`: it writes `render` and
`renderBlock` itself.

- **Shared coefficients.** `configure` calls `K::coefficients` once, and both
  channels step with the result.
- **A state per channel.** `left()` and `right()` are the two states, carried
  across blocks. `reset()` resets both.
- **Left then right.** Each frame steps the left channel into a local, then the
  right, then stores the frame. A kernel drawing from a stream shared by the two
  channels (noise) draws for left before right on every frame, which is the
  order the legacy per-frame code draws in.
- **Once per channel per block**, `prepare` runs for each channel (a `Block` may
  depend on that channel's state), left first, then right, so a `prepare` that
  draws from a shared noise stream draws for left before right on every block.
  Then one mode dispatch covers the single loop over the frames.
- It is constrained on `processor_kernel<K>` at the class, so a generator,
  mixer or control kernel is a compile error, not an inert class. A kernel has
  one form, so the constraint needs no clause for control kernels.

### `Control<K>` (`core/control.h`)

`Control<K>` is a control kernel as a block-rate control signal: one
`Coefficients`, one `State`, one `advance` per block. It satisfies
`control_generator`. It has no loop and no local copies: the kernel's `advance`
is the block.

- **Configure every block.** `configure(settings)` computes the coefficients
  from the owner's settings and leaves the state alone. It is cheap and meant to
  be called before every block, so a knob or tempo change applies from the next
  block's first sample while the signal's position carries over. `coefficients()`
  and `state()` read back what the wrapper holds; `reset()` returns to `State{}`,
  or to the kernel's `static void reset(State&)` quirk when it declares one.
- **Advance once per block.** `advance(n)` makes one call to the kernel's
  `advance`, through `kernel_ops::advance_at`, dispatching a moded kernel's
  mode once through `with_mode`, and returns the signal's value after the
  block. `n` may be 0. It is `[[nodiscard]]`: the value is what the owner
  applies to the block.
- **Events.** `on<&K::event>(args...)` runs a function taking the kernel's
  `State` (and optionally its `Coefficients` first) on the wrapper's own state
  and returns what it returns, a reference included. The event is one of two
  shapes:
  - `event(const Coefficients&, State&, args...)`, given the configured
    coefficients;
  - `event(State&, args...)`, given the state alone.

  The first shape is tried first, so the coefficients are passed exactly when
  the event takes them. The state is taken by non-const reference. An event
  taking it by value would change a copy, and one taking it by const reference
  could not change it, so both fail to compile rather than silently doing
  nothing.
- **Unconfigured.** A default-constructed `Control` holds `Coefficients{}`; it
  can be advanced and used with events before any configure, and a kernel
  defines what that does (`DuckEnvelope` returns no duck).
- **Always inline.** Every member is `[[gnu::always_inline]]`, the
  default constructor included, so nothing sits between the owner's block and the
  kernel's arithmetic under `-fno-inline-functions`.

### The shared glue

`Mono`, `Stereo` and `Control` call a kernel only through `kernel_ops`
(`core/kernel.h`): `prepare_block`, `dispatch_mode`, then `step_at`,
`process_at` or `advance_at` for the kernel's form. So the `prepare`, mode
dispatch and form call exist once, and a composed kernel calls its parts
through the same functions. Each passes the coefficients as a const reference,
so the call resolves to the form's own signature even when a kernel overloads
it on non-const `Coefficients`.

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
- **`Stereo<K>`** (`core/stereo.h`) runs a mono q31 processor kernel on each
  channel: one `State` per channel and one shared `Coefficients`. It is a plain
  class satisfying the `processor` concept over `StereoSample`. Each frame steps
  the left channel and then the right, so a shared noise stream is drawn left
  then right on every frame, bit-exact. `configure(const K::Settings&)` sets both channels from one
  `Settings`. See [Wrappers](#wrappers).
- **Native stereo.** A component that is stereo by nature, such as the
  compressor or the delay, is a kernel whose `value_type` is `StereoSample`,
  wrapped by `Mono`.
- **SIMD stereo.** A stereo kernel whose `step_type` is `StereoVector` (one
  `Argon<q31>` per channel, lane `i` of each the same frame) gets NEON's
  interleaved loads and stores from `Mono`'s loop, so its arithmetic runs on
  whole channel vectors with no shuffles. The existing mixin components get the
  same from the mixins when their `render` takes and returns `StereoVector`.

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
2. Mode is a per-block policy (see [Kernels](#kernels-corekernelh-coremodeh)).
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
- Mixing into the voice's buffer (unison and oscillator summing) is a mixer.
- Filters, dynamics, delay and effects are kernels wrapped by `Mono`, `Stereo`
  or `Control`, under the settings convention and the slotting-ready rules.

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

- **The mixin→kernel move.** The oscillators and `Amplitude`, and the crude
  oscillators and `CrudeAmplitude` with their hand-written loops, move onto
  kernels wrapped by `Mono`. The `Generator`, `Processor` and `Mixer` mixins go
  once nothing uses them.
- **The rest of the kernel form.** Not implemented yet: `U` (the unroll factor
  of the wrappers' loops; stateful vector kernels opt in with `split`/`merge`,
  checked bit-exact against U = 1), `Chain` (fused composition of kernels with
  matching step types; `Modes` is the product of the parts') and `Lanes<K, N>`
  (lane-parallel recursive kernels, N = 4/8/12). `U` and `N` are chosen from
  device cycle-per-sample measurements.
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

## Measuring

The choices of `U`, `Lanes` and fusion rest on device cycle counts, and the
build can take them. What the measurements have shown so far, and the rules for
kernels that follow from them, are in [a9_dsp_performance.md](a9_dsp_performance.md). `DELUGE_DSP_BENCH` (CMake option, off by default) compiles
the benchmarks in `src/deluge/bench` into the device image and runs them once at
boot: the BSP calls `deluge_app_bench()` after the static constructors and
before `deluge_app_init()`, and the bench masks IRQs for its run. A build with the option off
contains no bench code beyond the hook, which returns false, and a host never calls it.

A bench image ends parked and dark; it never starts the app. When the hook
returns true, the BSP skips `deluge_app_init()`, waits for the PIC handshake,
turns off every pad, sidebar pad, button LED and gold-knob ring, clears the OLED
and sends it DISPLAY OFF, prints `bench: parked, pads/LEDs/OLED off`, and parks.
The PIC and the OLED controller keep their state across a CPU reset, so without
this they would show whatever the previous boot left for as long as the device
stays powered.

**Build and run.**

```sh
./dbt configure -DDELUGE_DSP_BENCH=ON
./dbt build RelWithDebInfo
tools/dsp_bench.py --timeout 180          # flashes, captures RTT, writes the table
./dbt configure -DDELUGE_DSP_BENCH=OFF    # the option sticks in the CMake cache
./dbt build RelWithDebInfo
```

`dsp_bench.py` runs `deluge_jlink.py`, which loads the RAM image and starts it
itself, so there is no probe to attach beforehand. The bench runs before
`deluge_app_init()`, so its lines (about 6 KB) are the first large write into the
empty 16 KB RTT buffer and arrive whole; it is the boot lines after them that may
be dropped while the buffer is full. `dsp_bench.py` writes
`.superpowers/estate/bench/<date>-<sha>.md` (the table) and `.json` (the records
and the run's metadata, the `BENCH-INFO` values and the sanity result among it).
It exits 0 on success; 2 for an incomplete or malformed capture (no table is
written); 3 when the probe script fails (its own code is in the message); and 4
when the run is complete but fails the sanity check below (the table is written,
marked with a `**SANITY CHECK FAILED**` banner). `--from-capture FILE`
re-tabulates a saved capture. It runs the probe until the parked line, so the
device it leaves running is dark; a capture with `BENCH-DONE` but no parked line
is still written, with a warning that the device may still be lit, and the JSON
metadata's `parked` says which.

**What is measured.** Each case is a component, built as the voice builds it and
run on the SRAM buffers the voice uses, and each is timed at block sizes
n = 32, 64 and 128:

1. `setup(n)` configures the component and fills its inputs;
2. blocks run untimed until at least 512 samples and at least four blocks have
   run (16 blocks at n = 32, 8 at 64, 4 at 128), so the component's code and
   tables are resident at every size and it is in the state its comment
   documents;
3. K = 64 consecutive blocks run between two counter readings;
4. the difference, modulo 2^32, less the calibration's difference at the same n
   (each counter floored at zero), is the case's result.

The calibration (`case=calibration`) is the same loop and the same indirect call
over a component whose block does nothing, so subtracting it removes the cost of
reading the counters and of the call. It is printed raw, so the table shows what
was subtracted. The image starts with cold caches, so before the first block
size the runner times the calibration once more and discards it; every printed
calibration, the first size's included, is measured warm.

The calibration removes the trampoline and the counter reads only. `cps` still
includes each case's per-block glue: the loads of the spans its setup took, the
call into the component's `renderBlock` (`advance` for the duck), and, for
`synced_pulse` and `duck_envelope`, the construction and `configure` they run
every block, as the voice and the sidechain do. That glue costs about the same in every block, so it adds roughly
constant / n to `cps` and inflates the small-n rows most.

**The lines.** Each measurement is one RTT line:

```
BENCH case=<name> n=<block> k=<blocks> cycles=<u32> cps=<x.xx> l1d_refill=<u32> inst=<u32> dc_stall=<u32> issue_empty=<u32> neon=<u32>
```

ending with `BENCH-DONE cases=<count>`, which counts every `BENCH` line before it
(the calibration lines included). `BENCH-INFO <key>=<value>` lines before the
cases are metadata and are not counted. The fields:

| Field | Meaning |
| --- | --- |
| `cycles` | CPU cycles over the K timed blocks (PMCCNTR, undivided, 32 bits) |
| `cps` | cycles per sample, cycles / (K * n), two decimals, rounded half up |
| `l1d_refill` | L1 data cache refills (event 0x03) |
| `inst` | instructions through the renaming stage (0x68; includes a few renamed down a mispredicted path) |
| `dc_stall` | cycles stalled on a data cache dependency (0x61) |
| `issue_empty` | cycles the issue stage dispatched nothing (0x66) |
| `neon` | NEON instructions through the renaming stage (0x74) |

The events are the Cortex-A9 PMU's (Cortex-A9 TRM, DDI 0388I, Tables 11.5 and
11.6), one counter each, so they count together without multiplexing. The table
`dsp_bench.py` writes shows `cps` as printed and the event counts per sample.

**Sanity.** `calibration_loop` runs a loop of known length, so its `inst` lies in
[1024 * n, 1024 * n + 2048] and its `cps` is at least 32.00 (the arithmetic is in
the case's comment in `cases.h`). A result outside that means the event
programming, not the component, is at fault: settle it before trusting any row.
`dsp_bench.py` checks both at every n, and checks that `dbgauthstatus` has SNE
(bit 6) set; on a failure it still writes the table, marked, and exits 4.

**Counters that read zero.** The A9 counts events only where non-invasive debug
is permitted. In Secure state that needs both DBGEN or NIDEN high, and SPIDEN or
SPNIDEN high (ARMv7-A ARM, DDI 0406C.b, Part C, "Non-invasive debug
authentication", the table "v7 Debug non-invasive debug authentication, with
Security Extensions"; Cortex-A9 TRM, 10.8, Table 10.12). Without them the event
counters do not count while PMCCNTR still does, because PMCR.DP is 0 (DDI 0406C.b, "Effects of non-invasive debug
authentication on the Performance Monitors"). The signature is a plausible
`cycles` beside every event at zero, `inst` of `calibration_loop` first. The
bench prints the register that settles it before the cases:

```
BENCH-INFO dbgauthstatus=0x........
```

DBGAUTHSTATUS is read with `mrc p14, 0, <Rt>, c7, c14, 6` (DDI 0406C.b,
"DBGAUTHSTATUS, Authentication Status register"; Cortex-A9 TRM, Table 10.9). Bit 6
(SNE) is set when non-invasive debug is permitted in Secure PL1 modes, that is
(DBGEN or NIDEN) and (SPIDEN or SPNIDEN), and bit 2 (NSNE) when it is permitted
in Non-secure state. The bench prints a plain `bench: reading debug
authentication` line just before the read, so a run that stops there shows it. With bit 6 clear the events do
not count in Secure state, and it is the board's authentication signals, not the
firmware, that have to change.

**Using the numbers.** They inform: which `U` a wrapper unrolls by, which lane
count a recursive kernel takes, and which kernels are worth fusing. They never
gate: no test depends on them. A table is evidence for a performance claim with
its sha and date. A codegen claim uses the exact shipping flags
(`-fno-inline-functions`, LTO), because a host build differs.

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
- Is a block-rate signal a control kernel, and are its events functions on its
  State?
- Is there a hand-written `renderBlock` (or a new mixin `render`) where a kernel
  belongs? Per-sample or per-vector arithmetic is a kernel's `step`, and
  block-staged arithmetic (the delay's tape I/O) a staged kernel's `process`,
  wrapped by `Mono` or `Stereo`, and a block-rate signal a control kernel's
  `advance`, wrapped by `Control` (see
  [Kernels](#kernels-corekernelh-coremodeh)).
- Are there public verbs beyond `render`, `renderBlock`, `configure`,
  `coefficients`, `reset`, `advance` and `on`, other than accessors?
- Is there a per-sample branch on mode, where a per-block policy belongs?
- Are `Coefficients` and `State` anything other than plain value structs?
- Is noise read (through `CONG`, `jcong` or a held pointer) rather than
  injected as a template parameter?
- Are the mono and stereo paths duplicated, where `Stereo<K>`, or a native
  stereo kernel (`value_type` `StereoSample`) wrapped by `Mono`, would serve?
- Do comments paraphrase the legacy code, rather than state what the component
  does and why?
- Does each stateful piece have a kernel, wrapped by `Mono`, `Stereo` or
  `Control`, rather than hand-written state and loops?
- Is `step` free of per-sample mode branches? A mode is a `Modes` entry.
- Is `process` used only for block-staged arithmetic?
- Does each step stay in one domain, with `prepare` the only scalar per-block
  work and `Block` holding values in the step's domain?
- Are a kernel's `prepare` and `step` `[[gnu::always_inline]]`? App translation
  units build with `-fno-inline-functions`, so an out-of-line `prepare` is one
  call per channel per block. A control kernel's `advance`, called once per
  block, may be out of line; `DuckEnvelope::advance` is.
- Is there a structured binding over an Argon `std::array` on a per-step path?
  Under those flags `std::get` is out of line. An interleaved stereo load goes
  through `detail::toStereo` (`core/stereo_vector.h`), a `__builtin_bit_cast`,
  as `Mono` and the mixins do; any other array is read through `data()`.
- Is every function on the per-sample path, the wrapper and mixin glue included,
  `[[gnu::always_inline]]`? The app's translation units build with
  `-fno-inline-functions`, so anything else is an out-of-line call per sample
  or per vector in the shipping image.
- Does a performance claim cite a bench table, or the exact shipping-flags
  disassembly?
