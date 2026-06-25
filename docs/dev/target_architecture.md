# Deluge target architecture — modular library decomposition

> Companion to [`libdeluge_separation_plan.md`](libdeluge_separation_plan.md) and
> [`libdeluge_bsp_design.md`](libdeluge_bsp_design.md). Those docs put a C-ABI
> hardware boundary (`libdeluge`) *under* the application. This doc designs the
> decomposition of everything *above* that boundary: the long-term target where
> the application is a set of portable, individually testable libraries with
> enforced dependency rules — plus the runtime model (execution contexts,
> sharing, notifications, determinism) that the static decomposition needs to
> actually hold.
>
> Status: target architecture. Nothing here is implemented yet except where
> noted as "already exists." Baseline measurements are from 2026-06-11
> (`feat/libdeluge` branch).

---

## 1. Baseline audit: where the codebase stands

The application layer (`src/deluge`) is ~228k lines across ~1,000 files in
eleven directories that behave as one module: the include graph is a full
mesh. Measured cross-directory `#include` counts (rows include columns):

| from ↓ includes → | model | gui | storage | processing | dsp | hid | modulation | playback | io | util | memory |
|---|---|---|---|---|---|---|---|---|---|---|---|
| **gui**        | 561 | —   | 118 | 204 | 20 | 323 | 71 | 41 | 65 | 112 | 16 |
| **model**      | —   | 103 | 60  | 49  | 35 | 31  | 67 | 39 | 41 | 66  | 26 |
| **storage**    | 51  | 20  | —   | 23  | 2  | 12  | 10 | 1  | 18 | 34  | 13 |
| **processing** | 48  | 26  | 22  | —   | 13 | 11  | 26 | 9  | 5  | 27  | 6  |
| **modulation** | 38  | 11  | 10  | 9   | 0  | 3   | —  | 6  | 6  | 18  | 3  |
| **playback**   | 32  | 28  | 4   | 12  | 0  | 11  | 1  | —  | 6  | 5   | 1  |
| **io**         | 22  | 26  | 10  | 2   | 0  | 8   | 5  | 2  | —  | 9   | 2  |
| **dsp**        | 12  | 0   | 4   | 7   | —  | 0   | 2  | 2  | 3  | 19  | 6  |
| **hid**        | 13  | 52  | 4   | 10  | 0  | —   | 0  | 5  | 11 | 11  | 2  |
| **util**       | 1   | 7   | 2   | 4   | 0  | 8   | 1  | 0  | 6  | —   | 7  |

Downward arrows (gui → model) are healthy. The defining problems are the
*upward* arrows and the globals:

- **model → gui (103)**: `song.cpp` calls `uiNeedsRendering(&instrumentClipView, …)`,
  `display->displayError(…)`, `display->displayPopup("E248")` directly, and
  `song.h` includes `gui/menu_item/reverb/model.h` (the model stores a *menu*
  type). The data model knows about five concrete view singletons.
- **model ↔ storage**: serialization is member functions on model classes
  (39 model files touch `Serializer`), flowing through the global
  `storageManager`.
- **dsp → model/processing (19)**: almost entirely `TimeStretcher` reading
  `Sample`/`SampleCache`/`Cluster` directly, plus `Oscillator` → `wave_table`.
- **Global singletons**: `currentSong` referenced in 102 files, `AudioEngine::`
  in 100, `playbackHandler` in 71, `PadLEDs::` in 35. Counterweight:
  `ModelStack` already exists as an instance-threading pattern — the precedent
  for the fix.
- **util is not a leaf**: it includes gui/hid/io, so even the "pure" layer
  can't be extracted as-is.
- **Display swappability is 350 scattered branch sites**: `haveOLED()`/
  `have7SEG()` across 110 files, re-evaluated dynamically (and it must stay
  runtime-dynamic — `swapDisplayType()` is invoked live from boot config, a
  button combo, SysEx, and the emulated-display feature).
- **Allocator ↔ model entanglement**: the `Stealable` protocol has the memory
  manager calling back *into* model objects (clusters, sample caches) to
  reclaim them.
- **The GUI is 86k lines / 501 files, 327 of which are `menu_item`** — every
  menu item is a class conflating tree structure, behavior, and rendering.
- **Concurrency safety is ad-hoc discipline.** The UI mutates the model while
  the audio path reads it; safety rests on single-core cooperative scheduling
  plus hand-placed guards (`allowSomeUserActionsEvenWhenInCardRoutine`). There
  is no declared contract for which code may run in which context.
- **Undo is its own smeared subsystem**: `model/action` + `model/consequence`
  (the ActionLogger) is invoked ad-hoc from views and model code alike.
- **Params are a hand-partitioned byte** — see §7, which gets its own section
  because it is the deepest structural problem.

---

## 2. Decisions already made (context for everything below)

1. **The hardware boundary is the `libdeluge` C ABI** (already designed and
   first-drafted in `include/libdeluge/`). Everything in this doc sits above it.
2. **No ABI or schema parity with spark** (`~/GitHub/spark`). Spark is used as
   a *design reference* — its library taxonomy, command vocabularies, and
   component splits port by inspection — and the two projects may share
   file-format conformance corpora (real song files, golden renders). Nothing
   binary, nothing generated, no shared types.
3. **Dedicated 7-seg views are first-class**, not a degraded OLED fallback,
   and display type is swappable at runtime by replacing one presenter-set
   object (see §6.3).
4. **Grid views are a reusable, dimension-parameterized library** in the
   spark-grid mold (see §6.2).
5. **Views read the model directly and mutate via commands.** No query/snapshot
   messaging layer for now; the enforced discipline is *direction*: UI may call
   model accessors and issue mutations, model/engine never call UI — they emit
   notifications. A query layer can be retrofitted if a remote frontend ever
   becomes real.
6. **All model mutation flows through a command/undo gate** (`deluge-commands`,
   §4.4), which absorbs the existing Action/Consequence system. UI, MIDI
   follow/learn, the companion protocol, and test scripts all issue commands
   through the same gate; undo is recorded there, not in views.
7. **The runtime model is explicit** (§4): every public library API declares
   its execution context, task↔audio sharing uses immutable-swap or lock-free
   queues, notifications are a coalescing dirty-set, and deterministic replay
   is a maintained invariant with a flight recorder built on it.
8. **Cross-cutting policies are stated once** (§5): allocation, error
   handling, typed time units, and localization apply uniformly to all
   libraries rather than being re-decided per extraction.

---

## 3. The library map

Seventeen application libraries over `libdeluge`. Leaf-most at the bottom.

```
┌─────────────────────────────────────────────────────────────────────┐
│  deluge-app        composition root: owns every object that is a    │
│                    global today (song, engine, UI shell, allocator  │
│                    regions); implements the deluge_app_* callbacks; │
│                    wires notifications/commands; flight recorder;   │
│                    companion (SysEx) protocol; blits framebuffers   │
├──────────────────────────────────┬──────────────────────────────────┤
│              UI side             │            core side             │
│                                  │                                  │
│  deluge-ui-shell                 │  deluge-engine                   │
│  ┌──────────┬─────────┬───────┐  │  voices, patch compilation +     │
│  │ deluge-  │ deluge- │deluge-│  │  evaluation, sequencer/playback, │
│  │ grid-    │ views-  │views- │  │  routing, mixing, source gens    │
│  │ views    │ oled    │ 7seg  │  │  (LFO/env), metronome            │
│  ├──────────┼─────────┼───────┤  │                                  │
│  │ deluge-  │ deluge- │deluge-│  │  deluge-files     deluge-stream  │
│  │ gfx-grid │ gfx-oled│ gfx-  │  │  browse/load/save  cluster cache,│
│  │          │         │ 7seg  │  │  model⇄format      sample        │
│  ├──────────┴─────────┴───────┤  │  binding, presets, streaming     │
│  │      deluge-ui-tokens      │  │  settings store    (audio ctx)   │
│  └────────────────────────────┘  │  (task context)                  │
├──────────────────────────────────┴──────────────────────────────────┤
│  deluge-commands   the model-mutation gate: validated commands,     │
│                    Action/Consequence undo log, scriptable surface  │
├─────────────────────────────────────────────────────────────────────┤
│  deluge-model      songs, clips, notes, kits, scales, sample meta — │
│                    pure data + musical logic + notifications out    │
│  deluge-params     param identity/metadata registry, AutoParam      │
│                    automation, cable descriptions (see §7)          │
├─────────────────────────────────────────────────────────────────────┤
│  deluge-xml        deluge-dsp        deluge-alloc                   │
│  format tokenizer/ pure DSP over     GMA, regions, cache manager,   │
│  serializer (XML + buffers + a       Stealable interface            │
│  JSON dialects),   SampleSource                                     │
│  zero model        interface                                        │
│  knowledge                                                          │
├─────────────────────────────────────────────────────────────────────┤
│  deluge-base       fixed-point math, containers, strings, lookup    │
│                    tables, typed time units, error codes,           │
│                    diagnostics sink, context tracker, PRNG service, │
│                    feature-flag registry                            │
├─────────────────────────────────────────────────────────────────────┤
│  libdeluge (C ABI) ← already designed; BSPs implement it            │
└─────────────────────────────────────────────────────────────────────┘
```

### 3.1 Allowed-dependency table

Each library is a real CMake target; everything not listed is forbidden and
fails the layer check (§9). All libraries may use `deluge-base`.

| library | may depend on |
|---|---|
| `deluge-base` | (nothing; may use libdeluge clock/memory headers) |
| `deluge-xml` | base |
| `deluge-dsp` | base |
| `deluge-alloc` | base, libdeluge (memory regions, cache ops) |
| `deluge-params` | base |
| `deluge-model` | base, params, dsp (value types only), alloc (Stealable impl) |
| `deluge-commands` | base, model, params |
| `deluge-files` | base, xml, model, params, libdeluge (block device, storage-wait) |
| `deluge-stream` | base, alloc, model (sample metadata), libdeluge |
| `deluge-engine` | base, dsp, params, model, stream, libdeluge (audio, midi, cv, clock) |
| `deluge-ui-tokens` | base |
| `deluge-gfx-oled` | base, ui-tokens |
| `deluge-gfx-7seg` | base, ui-tokens |
| `deluge-gfx-grid` | base, ui-tokens |
| `deluge-grid-views` | base, ui-tokens, gfx-grid, model, params (read), commands (emit only) |
| `deluge-views-oled` | base, ui-tokens, gfx-oled, model, params (read-only) |
| `deluge-views-7seg` | base, ui-tokens, gfx-7seg, model, params (read-only) |
| `deluge-ui-shell` | all UI libs, model, params, commands (execute), engine (transport/command surface), files |
| `deluge-app` | everything; the only library that touches `libdeluge/app.h` |

Key prohibitions, stated explicitly because they reverse today's reality:

- **model/params/engine never include any UI library.** Model raises typed
  notifications into the coalescing dirty-set (§4.3); it *returns* `Error`
  codes instead of calling `display->displayError`. Deep-fault reporting
  (`"E248"`) goes to the diagnostics sink in `deluge-base`; the UI subscribes
  and decides presentation.
- **Views never mutate the model directly.** Grid views and screens *emit*
  command values; the shell executes them through `deluge-commands` (§4.4).
  The engine does not execute commands; transport requests reach it through
  its own task→audio queue (§4.2).
- **dsp never includes model/storage/engine.** Sample access goes through a
  `SampleSource` read interface defined in dsp and implemented by
  `deluge-stream`; wavetable data is passed in as buffers/descriptors.
- **gfx-* toolkits never include model or views.** They render into
  caller-provided buffers; no globals, no timers, no hardware.
- **No library except deluge-app includes BSP/HAL headers** (already enforced
  by `scripts/check_platform_boundary.py`; the same mechanism extends to all
  the rules above, §9).

The consumer-owned-interface pattern used for `SampleSource` (defined in dsp,
implemented by stream) and `Stealable` (defined in alloc, implemented by
stream) is the *standard* trick for breaking upward arrows: the lower library
declares the narrow interface it needs; the higher library implements it; the
app wires them.

### 3.2 Core-side library notes

**deluge-base** — purified `util/` plus `foundation/`: fixed-point
(`fixedpoint.h`), lookup tables, containers, `d_string`, error enum, `Finally`,
semver — plus the new cross-cutting services: the diagnostics sink (replacing
deep `display->` calls), **typed time units** (§5.3), the **execution-context
tracker** (§4.1), the **seeded PRNG service** (§4.5), and the **feature-flag
registry** (definitions + query; persistence lives in files, menus in shell).
The current `util` → gui/hid/io includes (22 total) get removed or the
offending code moved up. This library is what makes every other extraction
possible, so it goes first.

**deluge-xml** — the file-format library: tokenizer, writer, escaping, the
Deluge XML dialect and the JSON variant, `Serializer`/`Deserializer`
interfaces. Knows tags and syntax, never model types. Mirrors spark's
`deluge-xml` crate in scope; the two share a conformance corpus of real song
files (round-trip byte-stability tests).

**deluge-dsp** — filters, reverb, delay, oscillators, FFT, granular,
timestretch, compressor, dx. Operates on buffers and fixed-point types. The
two couplings to break: `TimeStretcher`'s direct reads of
`Sample`/`SampleCache`/`Cluster` (becomes the `SampleSource` interface), and
`Oscillator` → `wave_table` (wavetable frames passed as descriptors). The
existing x86-SIMDe vs ARM-NEON differential harness becomes this library's
test suite.

**deluge-alloc** — the GMA, memory regions, cache manager, object pools, and
the `Stealable` *interface*. Alloc defines the protocol; `deluge-stream`
implements it for clusters/sample caches; `deluge-model` objects stop knowing
the allocator exists beyond implementing the interface. Region descriptors
come from `libdeluge/memory.h`. Allocators are *injected* (§5.1); the global
`operator new` overrides in `memory/operators.cpp` become an app-level
composition detail, not something libraries rely on.

**deluge-model** — songs, clips, notes, kits, drums, instruments (as data),
scales, sample/wavetable *metadata*, sync. Pure data + musical logic. Four
subtractions define it: no serialization members (moves to `deluge-files`
binding code), no UI calls (becomes notifications), no direct allocator
protocol knowledge, and no undo bookkeeping (moves to `deluge-commands`).
`ModelStack` survives and becomes the standard way context is threaded (it
replaces `currentSong` lookups as call sites are converted).

**deluge-commands** — the model-mutation gate (§4.4): command types, command
execution (validate → mutate → record `Consequence`), and the ActionLogger
undo machinery extracted from `model/action` + `model/consequence`. Task
context only. Every mutation source — UI shell, MIDI follow/learn (routed by
the app), companion protocol, test scripts — goes through it, so undo,
permission checks ("not during card routine"), and notification raising
happen in exactly one place.

**deluge-files** — task-context storage: browsing, preset/song load/save,
favourites, firmware files, and the **device settings store** (today's
`flash_storage`) plus feature-flag persistence. Owns the **model ⇄ deluge-xml
binding layer**: free functions / serializer visitors that know "a Clip
writes these attributes," so neither model nor format library knows the
other. Also owns the legacy-name alias tables (§7.3).

**deluge-stream** — audio-context storage: cluster cache, sample streaming,
SD read scheduling, `Stealable` implementations. Split from `deluge-files`
because the two halves have opposite realtime contracts (blocking-allowed vs
audio-safe), which today share a directory and a god-object.

**deluge-engine** — voice allocation and rendering, patch compilation and
evaluation (§7), the sequencer/playback handler, arpeggiator runtime, audio
routing/mixing, source generators (LFOs, envelopes, random, sidechain),
metronome, live input. `AudioEngine::` (a namespace-as-singleton in 100 files)
becomes an engine instance owned by `deluge-app`. The sequencer could later
split out (`deluge-seq`) for deterministic golden-file testing in isolation;
not required initially.

### 3.3 What `processing/`, `modulation/`, `playback/`, `hid/`, `io/` dissolve into

Today's directories don't map 1:1 to target libraries; these dissolve:

| today | goes to |
|---|---|
| `processing/sound`, `engines`, `live`, `metronome`, `source` | engine |
| `processing/stem_export` | files (+ engine render hooks) |
| `modulation/params`, `automation` | params (data) + engine (evaluation) |
| `modulation/patch` | params (cable data) + engine (patcher) |
| `modulation/arpeggiator`, `lfo`, `envelope`, `sidechain` | engine (runtime) + model (config data) |
| `playback/` | engine (transport/sequencer) |
| `model/action`, `model/consequence` (undo) | commands |
| `hid/display/oled_canvas`, fonts | gfx-oled |
| `hid/display/numeric_layer` | gfx-7seg |
| `hid/led/pad_leds` animations, `gui/colour` | gfx-grid |
| `hid/buttons`, `encoders`, `matrix` semantics | ui-shell (input routing); raw events already come from libdeluge |
| `gui/l10n` | ui-tokens (string tables; every user-visible string is an l10n key, §5.4) |
| `io/midi` | engine (midi runtime) + model (learned bindings data) + files (device defs) |
| `io/debug` | base (diagnostics) |
| `storage/` | files + stream + xml (the `Serializer` machinery) |
| `storage/flash_storage` (device settings) | files |
| `storage/smsysex`, `hid/hid_sysex` (companion/SysEx protocol) | app (composition-level service; **external API** for community web tools — gets a named owner and a stability promise, like the file format) |
| runtime feature flags | base (definitions + query) + files (persistence) + ui-shell (menu) |

---

## 4. Runtime model

The dependency rules in §3 are necessary but not sufficient: most of the
firmware's historical fragility is *temporal* (who runs when, who mutates
what under whom). This section is the contract for that.

### 4.1 Execution contexts

Three contexts, the same taxonomy `libdeluge` already documents per function:

- `[audio]` — inside `deluge_app_render` or callable from it. No allocation,
  no locks, no blocking, no command execution.
- `[task]` — cooperative/task context (UI, files, commands).
- `[isr]` — interrupt context; app libraries never expose `[isr]` APIs
  (ISR→task hand-off happens below the libdeluge boundary or in deluge-app).

Rules:

1. **Every public library API declares its context** in its header
   (`[audio]`, `[task]`, or `[any]`), exactly as `libdeluge` headers do.
   Context is part of the signature for review purposes.
2. **A context tracker in `deluge-base`** (set by deluge-app on entry to the
   render callback / task slices) backs `ASSERT_CTX(audio)`-style debug
   assertions at API entry. Free in release builds; in sim and debug builds,
   calling a `[task]` API from the audio path is an immediate, attributable
   failure instead of a latent race.
3. Coarse library defaults: commands, files, xml binding = `[task]` only;
   engine evaluation = `[audio]`; stream is explicitly two-faced (request
   side `[task]`, delivery side `[audio]`); gfx toolkits are context-free
   pure functions; model *reads* are `[any]`, model *mutation* is `[task]`
   via commands only.

This rule set is chosen to survive the Embassy endgame: nothing in it assumes
cooperative scheduling, so introducing real preemption later changes the BSP,
not the application contracts.

### 4.2 Task ↔ audio sharing

Exactly three sanctioned mechanisms, in order of preference:

1. **Immutable snapshot + atomic swap** for configuration the audio path
   reads: compiled patch/routing tables (§7.2), routing topology, settings.
   Task context builds a new snapshot, publishes with a single pointer swap;
   the audio path reads a consistent version for the whole render block.
   (Spark's `ArcSwap` routing table is the reference shape; ours is an
   app-owned retire list — freed only after the audio path has moved on —
   since there's no GC.)
2. **Fixed-size lock-free SPSC queues** for flows: input events upward,
   transport requests / parameter writes downward to the engine, notification
   raises (§4.3). Sized at init, allocation-free at runtime.
3. **Documented cooperative sections** — the pragmatic bridge. Today the
   voice path reads model objects directly during render, which is safe only
   because scheduling is cooperative. Each such site gets marked
   (`// COOP-READ:`), counted by the ratchet (§9), and burned down by moving
   the data into mechanism 1 as the engine extraction proceeds. New code may
   not add them.

No locks in `[audio]`, ever. Mutexes between task-context subsystems are
allowed but discouraged in favor of single-ownership.

### 4.3 Notifications: a coalescing dirty-set, not an event stream

What the current `uiNeedsRendering(whichRows)` system gets right must be
preserved by construction: it *coalesces*. The notification channel is:

- A **fixed-size dirty structure** (bitsets + small enum sets keyed by
  subsystem: "clip N changed", "arrangement changed", "playback state",
  "load progress", …), not a callback list and not a queue of heap events.
- **Raise is allocation-free and `[any]`-safe** (atomic set-bit), so the
  engine and stream can raise from the audio path.
- **Drained once per UI tick by the shell**, which maps dirty entries to
  view invalidation and presenter updates. No synchronous callbacks out of
  the model — raising a notification during a mutation cannot re-enter the
  UI, by type, not by convention.
- Payload-bearing cases (error codes for display, progress) ride a small
  bounded queue with overwrite-oldest semantics; the dirty-set tells the UI
  *that* something changed, the model tells it *what* on re-read (decision
  §2.5: views read the model directly).

### 4.4 Commands and undo

`deluge-commands` is the single gate for model mutation:

- A **command** = validated intent (+ the `ModelStack` context it applies
  to). Execution: permission check → mutate model → record `Consequence`
  for undo → raise notifications. `[task]` only.
- The existing Action/Consequence machinery is **absorbed, not rewritten** —
  it already implements consequence-based undo well; what changes is that
  recording happens in one place instead of being sprinkled through views.
- All mutation sources converge here: UI shell, MIDI follow / learned CCs
  (routed by deluge-app from the midi runtime), the companion SysEx
  protocol, and **test scripts** — which makes "apply command list, assert
  model state" and the property test "apply + undo ⇒ identical model" the
  cheapest tests in the system.
- Continuous gestures (knob turns, note drags) use the existing
  action-coalescing semantics (one Action absorbing increments) so undo
  granularity stays musical, not per-tick.

### 4.5 Determinism, replay, and the flight recorder

**Invariant: same input log ⇒ same audio and same framebuffers.** Everything
entering the app already crosses defined boundaries (libdeluge events, audio
in, clock, block device); the disciplines that keep the invariant true:

- **One seeded PRNG service in `deluge-base`**; no `rand()`, no ad-hoc LCGs,
  no wall-clock reads outside the boundary. The seed is part of the log.
- Iteration order over containers that feed rendering is deterministic
  (no address-ordered maps in audible paths).
- The host sim runs the app against a recorded log instead of live I/O —
  the BSP already supports run/capture/inject.

On top of the invariant, the **flight recorder**: a small ring buffer in
deluge-app logging recent input events + clock deltas + seed, dumped on
`freezeWithError` (and on demand via the companion protocol). A community bug
report becomes a replayable sim session. This is the highest-leverage
debugging feature the architecture buys, and it costs only the discipline
above plus a few KB of RAM.

---

## 5. Cross-cutting policies

Stated once, applying to all seventeen libraries.

### 5.1 Allocation

- **`[audio]` APIs never allocate.** Enforced in debug/sim builds by an
  allocator guard armed during the render callback (allocation under guard =
  immediate failure).
- **Libraries receive allocators; they don't reach for globals.** The GMA is
  injected by deluge-app (the existing global `operator new` overrides become
  app composition, removed from library code). dsp and the gfx toolkits don't
  allocate at all — callers provide buffers. xml/files/commands/model
  allocate freely in `[task]`. engine and stream allocate only at
  instantiation/configuration time, never per-block.
- Fixed-capacity by default for engine-side structures (voice pools, queues),
  sized at init from board/config descriptors.

### 5.2 Errors

- **Exceptions never cross a public library API.** Internally a library may
  use what it likes; surfaces speak `Error` (the existing enum) or
  `Try`/expected-style returns (`util/try.h` formalized into base).
- Unrecoverable faults go to the diagnostics sink → freeze path with flight-
  recorder dump (§4.5); they do not render UI from deep code.
- The current mixed regime (Error returns + `exceptions.h` + scattered
  `display->displayError`) migrates to this policy as each library is
  extracted.

### 5.3 Typed time units

Sequencer ticks, samples/frames, milliseconds, and bars/beats are distinct
strong types in `deluge-base` with explicit conversion only through rate/
tempo objects. Bare `int32_t` time is the current normal and a standing bug
source at the model/engine/files seams. Introduced *during* extraction —
every signature is being touched anyway, so the typing is nearly free if done
then and prohibitively churny if done later.

### 5.4 Localization

Every user-visible string is an **l10n key resolved at presentation time**
(in the presenter sets / shell), never embedded English in core libraries.
String tables live in `deluge-ui-tokens` (today's `gui/l10n`). In particular,
`ParamInfo::display_name` (§7.2) is an l10n key — otherwise the param
registry bakes English into the engine layer.

---

## 6. UI architecture

Three presentation targets — grid, OLED, 7-seg — all the same shape:
**semantic commands/state in → small framebuffer out**, with all interaction
logic display-agnostic. Spark's `ui/` workspace is the direct reference
(`deluge-ui-toolkit`, `spark-grid`, `spark-display-oled`, `ui/tokens`).

### 6.1 Toolkits (`gfx-*`): pure drawing, no policy

- **gfx-oled** — `Canvas` (which already has the right API — lines, rects,
  circles, text layout, multi-line graphics) extracted from `hid/display/`,
  plus fonts, icons, and shared widgets (popup frame, scrollbar, menu list,
  VU). Renders into a caller-provided framebuffer. Its current bad deps
  (`flash_storage` settings read, `oled.h` globals, `gui/fonts`) are removed
  by injection. Settings like "menu line count" arrive as arguments.
- **gfx-7seg** — the existing `numeric_layer/` stack (layers, scrolling text,
  blink) extracted and de-globalized: a digit/dot framebuffer (4 digits ×
  segments + dots), a layer compositor, a scroller. This is genuinely a
  *toolkit*, same rank as Canvas.
- **gfx-grid** — `RGB`, palette, a `PadGrid` buffer type
  (dimension-parameterized, not `kDisplayWidth`-coded), layer compositing,
  and the animations currently fused into `PadLEDs::` (fade, flash, explode,
  zoom, smear-scroll — the same taxonomy as spark-grid's `animations/`).
  Animations are pure functions over PadGrid frames; the shell owns timing.
- **ui-tokens** — palette constants, font data, styling constants, and l10n
  string tables shared by all three toolkits (and available to external
  tooling that wants to render "Deluge look").

All three are golden-snapshot testable on the host: render → buffer → diff
(PNG for OLED/grid, 4-chars-plus-dot-bits text for 7-seg).

### 6.2 deluge-grid-views: the reusable grid UI library

The pad grid is the instrument's primary UI and gets its own library, in the
spark-grid mold (views + components + animations over a `GridView` trait).
What already exists half-formed and proves the fit:

- Every view already implements
  `renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth], …)`
  / `renderSidebar(…)` — framebuffer-out with row dirty-masks. It is just
  hard-coded to compile-time dimensions, buried in god-objects, and bypassed
  by direct `PadLEDs::` pokes.
- `gui/ui/keyboard/layout/` is already the component pattern: a
  `KeyboardLayout` base class with seven pluggable layouts. It is the
  template for the rest of the library.

Target shape:

```
GridView interface:
  render(GridDimensions, const Model&) -> PadGrid   (with dirty-row masking)
  handleCommand(GridCommand) -> ViewResult           (semantic, not raw pads)
  update(tick)                                       (animations, playhead)
```

- **Dimensions come from the libdeluge board descriptor** (main cols, sidebar
  cols, rows) — this is what makes the library serve future hardware, the
  host sim at any size, and tests without view changes. Keep `whichRows`
  dirty-masking; it's a real embedded optimization that costs the API nothing.
- **Views**: session grid, note clip (piano roll / folded), kit, arranger,
  automation bars, keyboard screens, browser/favourites, slicer,
  waveform-on-pads, performance.
- **Components** (shared between views, spark's `components/`): clip-row
  rendering, mute/audition sidebar columns, velocity bars, column controls,
  waveform display.
- Views hold their own navigation state (scroll/zoom/selection), read the
  model directly (decision §2.5), and **emit commands** (§4.4) upward —
  never mutate, never mid-render.

### 6.3 Display views: swappable OLED / 7-seg presenter sets

Today's 350 `haveOLED()`/`have7SEG()` branch sites become two **presenter
sets** behind one interface, selected by a single active-set pointer.
`swapDisplayType()` = flip the pointer, request re-present.

- **Screens emit semantic UI state**, a small typed vocabulary: *param value
  (name, value, range)*, *menu list (items, selection)*, *text/scroll text*,
  *popup (kind, text)*, *error code*, *progress/animation*, *browser entry*,
  plus a handful of screen-specific forms (note editor, sample marker). The
  vocabulary is derived by cataloguing what the existing 350 branch sites
  actually render on the 7-seg side — it is descriptive of this firmware's
  screens, **not** a general GUI framework. Too-thin and presenters bypass it;
  too-rich and it becomes retained-mode. The catalogue is the spec.
- **views-7seg** is mostly one generic interpreter of that vocabulary (a
  4-digit display can only show value / short text / scroll / dots / blink),
  with per-screen overrides only where information design genuinely differs.
- **views-oled** is mostly per-screen renderers over gfx-oled, sharing
  widgets.
- The `Display` god-interface dissolves: its notification half
  (`displayError`, loading animations, console) becomes UI events through the
  shell; its rendering half becomes the presenter sets.
- The emulated-display runtime feature falls out: run the 7-seg presenter
  set, draw its digit state with an OLED segment-glyph widget (written once,
  in gfx-oled).

### 6.4 deluge-ui-shell

The one UI library that talks to model/engine/files/commands. Owns: the
UI-mode/screen state machine (reifying `currentUIMode` globals), the view
stack, input routing (libdeluge control-surface events → semantic commands →
active grid-view / screen), command execution via `deluge-commands`, the
notification dirty-set drain (§4.3), the active presenter-set pointer,
indicator LEDs, popups and timers (`ui_timer_manager`), and the menu
*interpreter*.

**The menu system becomes data + interpreter.** Menu trees as declarative
structure; items bind to a param/setting handle (via the params registry, §7)
or a command; rendering is toolkit code ("draw a menu") fed through the
presenter sets. The 327 `menu_item` classes collapse into a small item-kind
taxonomy + tables. This also fixes the backwards `song.h` →
`gui/menu_item/reverb/model.h` dependency: the model keeps its own enum, the
menu reflects it.

**Full-UI integration test**: inject pad/button/encoder events, assert on
three small framebuffers. No hardware, no sim process, milliseconds per case.

---

## 7. The params/cables system

The deepest structural problem and the gate for new engines, new effects, and
CLAP. Gets its own design + migration plan.

### 7.1 What is wrong today (audit)

The current system conflates **identity, metadata, storage, and evaluation**,
all welded to a single hand-partitioned `uint8_t`:

1. One global byte namespace with magic offsets:
   `Local` → `Global` → placeholder `89` → `UNPATCHED_START = 90` →
   `STATIC_START = 162` → `PATCH_CABLE = 190`, guarded by collision
   `static_assert`s (`modulation/params/param.h`).
2. **Combination semantics encoded by enum position** — linear/volume/hybrid/
   exp determined by range comparisons (`Patcher::Config{firstParam,
   firstNonVolumeParam, firstHybridParam, …}`). Inserting a param above the
   wrong `FIRST_*` marker silently changes its math. Display names and file
   keys live in parallel switch statements with "ANY TIME YOU UPDATE THIS
   LIST!" comments.
3. **Positional, type-punned storage** — `ParamManager::summaries[0]` is an
   `UnpatchedParamSet` *or* a `MIDIParamCollection` depending on context,
   resolved by C-cast.
4. **Layout-trick evaluation** — the patcher fills `paramFinalValues` arrays
   addressed by enum arithmetic; `sound.h` carries a Dec-2024 comment
   documenting a sound-altering bug (#2660) "fixed" by `alignas(8)`,
   attributed to `offsetof` use in the patcher. The fragility has shipped.
5. **Fixed source list** — `PatchSource` enum (2 global LFOs, 4 envelopes,
   sidechain, MPE X/Y, …) with `uint32_t sourcesChanged` sized to it. Adding
   LFO3 touches enum, masks, file names, shortcuts, automation.
6. **Exactly one synth** — `Sound` *is* the engine; effects are hardcoded
   `ModControllableAudio` members. Runtime-discovered param spaces (CLAP, a
   second internal engine) have nowhere to exist.

### 7.2 Target design

Split the four concerns. Reference points: spark's modulation router
(plugin-local param spaces, mod routing as event remapping with
depth/polarity/combine, lock-free table swap) and CLAP's param model
(runtime-discovered `param_info`; **separate value and modulation planes**
(`PARAM_VALUE` vs `PARAM_MOD`); per-voice modulation via note IDs — which is
exactly the Deluge's patched-param concept, dynamically typed).

**Identity & metadata — per-module registries (`deluge-params`).**
A *module* (builtin synth, each effect block, arp, eventually a CLAP plugin
instance) exposes a queryable registry:

```
ParamInfo {
  local_id        // stable within the module
  file_key        // stable string for serialization
  display_name    // l10n key (§5.4), resolved at presentation
  default, value_mapping   // replaces "exp/linear by enum range"
  accumulation             // additive | multiplicative (replaces volume/hybrid tricks)
  flags: automatable | modulatable | polyphonic | global
}
SourceInfo { local_id, file_key, display_name, polarity, per_voice }
```

Global address = **(module instance, local id)** — hierarchical, not one
byte. Modules also *export* sources, killing the fixed `PatchSource` enum;
dirty-mask widths are derived at instantiation.

**State & automation — model side, re-keyed.** `AutoParam`'s node-vector
machinery is good and is *re-keyed, not rewritten*. The positional
`summaries[]` punning becomes typed collections keyed by module. The
interpolation/dirty-summary bitmask machinery is performance-critical and is
preserved as mechanism.

**Routing — data in `deluge-params`, compiled by the engine.**

```
Cable { source: (module, source_id), target: (module, param_id),
        depth (itself addressable as a param), polarity, combine }
```

Dense indices become a **runtime artifact**: at sound instantiation the
engine compiles registry + cable set into the dense slot arrays, mode-grouped
iteration ranges, and dirty bitmasks the patcher hot loop uses today.
Compiled tables are published to the audio path by snapshot-swap (§4.2). The
performance model is preserved; the *layout* is computed from metadata
instead of hand-maintained in enum order and `offsetof`. This structurally
eliminates the `alignas(8)` bug class.

**Evaluation — per-voice/per-sound patcher instances in `deluge-engine`.**
LFOs/envelopes/random/sidechain are engine-owned source generators. The
recent span-based `Patcher` refactor already points this way.

**Serialization — by stable `file_key` via the registry** (formalizing
`paramNameForFile`), with legacy alias tables in `deluge-files` mapping every
historical name/number. Old songs are sacred; a round-trip corpus enforces it.

**CLAP fit.** The internal module interface is deliberately CLAP-shaped:
separate value/mod planes, per-voice mod with note IDs, runtime `ParamInfo`.
The builtin synth becomes the first "plugin" of the system; a CLAP host is a
second implementer whose registry is populated from `clap_plugin_params` at
load. Honest scope note: `dlopen`-style loading is unrealistic on the RZ/A1L —
the firmware target is *statically linked CLAP-API plugins and a CLAP-shaped
param model*; real dynamic CLAP hosting applies to desktop/host builds.
(Open question §10.)

**Registry-driven UI.** A large fraction of the 327 `menu_item` classes are
per-param boilerplate; with `ParamInfo` available, param menus, mod-matrix
shortcuts, gold-knob assignment, and automation-lane naming become generic
code. This is where §6.4's menu collapse and §7 meet.

### 7.3 Params migration sequence (each step ships green)

1. **Characterization harness first.** Golden-render tests of patched sounds
   through the host sim (extending the DSP differential harness to the
   patcher) + serialization round-trip corpus of real songs. This refactor
   can change *sound*; #2660 proves regressions ship silently. Nothing moves
   before this exists.
2. **De-position the metadata** (no behavior change): one `constexpr
   ParamInfo` table indexed by the current enums; combination mode, display
   names, file keys all read from it; `Patcher::Config` ranges generated from
   the table. Kills "position encodes semantics" in place.
3. **Stable keys through serialization**: registry becomes the single source
   for file I/O; legacy alias table; corpus stays byte-identical.
4. **Hierarchical addresses**: `(module, param)` addressing introduced in
   model/UI/automation; current enums become the builtin module's local IDs;
   `ParamDescriptor` shrinks to a builtin-module encoding detail.
5. **Compiled dense slots**: patcher slot maps built at instantiation;
   `offsetof`/fixed-array addressing dies; remove the `alignas(8)` and prove
   stability via golden renders. (The risky step — hence behind the harness.)
6. **Source registry** + derived bitmask sizing (unlocks LFO3, per-engine
   sources).
7. **Module interface extraction**: builtin synth and each effect block
   wrapped as modules. This is the socket for new engines, new effects, and
   the CLAP host.

Steps 1–3 are low-risk and immediately valuable. Capacity note: builtin-engine
arrays (`paramFinalValues` etc.) may stay fixed-capacity per voice for
allocator friendliness; dynamic sizing is only needed for plugin modules.

---

## 8. Testing strategy (the point of all of this)

| layer | test style | exists today? |
|---|---|---|
| deluge-dsp | differential x86-SIMDe vs ARM-NEON; golden buffers | harness exists (`tests/`), becomes the library suite |
| deluge-xml + files | round-trip corpus of real songs; byte-stability; fuzz the tokenizer | corpus to be assembled |
| deluge-params/engine | golden renders of patched sounds; patcher unit tests; determinism tests | no |
| deluge-model | musical-logic unit tests (scales, sync, note ops); notification contract tests | no |
| deluge-commands | command-script tests (apply list, assert state); property: apply + undo ⇒ identical model | no |
| gfx-oled / gfx-grid | golden PNG snapshots | host sim renders OLED→PNG already; formalize |
| gfx-7seg | text snapshots (4 chars + dot bits) | no |
| grid-views / display views | state in → framebuffer snapshot out | no |
| ui-shell | event-script integration tests: inject pads/buttons/encoders, assert on three framebuffers | no |
| whole app | host-sim BSP (run, capture, inject); **replay**: recorded/flight-recorder logs ⇒ bit-identical audio + framebuffers; generated logs for soak/fuzz | sim exists (`sim/`, `build-sim*`); replay invariant per §4.5 |

The sim drops from "the only way to test anything" to the integration layer
of a pyramid. Debug/sim builds additionally arm the runtime enforcement
(§4.1 context assertions, §5.1 audio-allocation guard), so every test above
doubles as a concurrency/allocation-policy check.

---

## 9. Enforcement: how the mesh stays dead

The mesh regrows without mechanical enforcement. Four mechanisms, all from
day one of each extraction:

1. **Real CMake targets with `PRIVATE` dependencies.** A library that isn't
   declared a dependency isn't on the include path; violations fail to
   compile. (Not yet true for `src/deluge/*` directories, which share one
   target.)
2. **Layer-rule checker with a ratchet.** Extend
   `scripts/check_platform_boundary.py` into a general checker driven by the
   §3.1 allowed-dependency table, with a committed baseline of current
   violation counts per (from, to) pair. CI fails on *increase*; decreases
   update the baseline. This lets enforcement start *before* extraction
   finishes — the matrix in §1 is the initial baseline. The same ratchet
   tracks `COOP-READ` markers (§4.2) so the cooperative-section burn-down is
   visible and monotonic.
3. **Runtime enforcement in debug/sim builds**: execution-context assertions
   (§4.1) and the audio-path allocation guard (§5.1). The static rules keep
   the include graph honest; these keep the *temporal* contracts honest.
4. **Include hygiene**: IWYU or equivalent on extracted libraries, so
   transitive includes don't silently reintroduce coupling.

---

## 10. Open questions

1. **CLAP on-device vs host-side.** Is CLAP hosting intended on future,
   beefier hardware, or primarily desktop/spark-side with the firmware only
   sharing the param model? Affects whether plugin-module param spaces must
   live within firmware allocator constraints (fixed pools) or can assume
   heap freedom.
2. **Cable combine-mode as a feature.** Does per-cable add/multiply
   (spark-style) become user-facing in the new cable model, or stay internal
   metadata matching today's behavior exactly? (Affects file format and UI;
   the data model supports either.)
3. **Sequencer as its own library.** `deluge-seq` between model and engine
   would be the most valuable deterministic-test target; deferred until the
   engine extraction shows where the seam falls naturally.
4. **`deluge-params` vs `deluge-model` packaging.** Params could fold into
   model as a sub-target; kept separate above because engine needs params
   without most of model. Decide when the engine's true include set is
   visible.

---

## 11. Extraction sequencing (whole program)

Ordered leaf-first; every step leaves the firmware shippable. Steps marked ◆
are the params track (§7.3), which interleaves.

1. **`deluge-base`**: purify `util/` + `foundation/`; add diagnostics sink,
   typed time units, context tracker, PRNG service, feature-flag registry;
   stand up the layer-rule ratchet with the §1 matrix as baseline and the
   debug-build context/allocation guards.
2. **`deluge-xml`** + song-file corpus + round-trip tests.
3. **`deluge-dsp`**: define `SampleSource`; break the 19 upward includes;
   adopt the differential harness as its suite.
4. ◆ Params steps 1–3 (harness, de-positioning, stable keys).
5. **`deluge-gfx-oled`** (Canvas + fonts + widgets; ~5 bad includes — days,
   not weeks), then **`deluge-gfx-7seg`** (numeric_layer), then
   **`deluge-gfx-grid`** (palette + PadLEDs animations), **`ui-tokens`**
   (including l10n tables).
6. **Model → UI inversion**: the §4.3 dirty-set replaces the 103 model→gui
   includes; Error returns + diagnostics sink replace deep `display->`
   calls. (Long, mechanical; ratchet tracks progress.)
7. **`deluge-commands`**: extract Action/Consequence into the command gate;
   route UI mutations through it; MIDI follow/learn and companion-protocol
   writes follow. Command-script and apply+undo property tests land here.
8. **`deluge-alloc`** + **`deluge-stream`** split out of `storage/` +
   `memory/`; `Stealable` interface lands in alloc; allocator injection
   replaces global-new reliance in extracted libraries.
9. **`deluge-files`**: serialization binding moves off model member
   functions; `storageManager` global dissolves; settings store +
   feature-flag persistence move in.
10. ◆ Params steps 4–6 (hierarchical addresses, compiled slots with
    snapshot-swap publication, source registry).
11. **`deluge-model`** closes: no UI, no serializer, no allocator protocol,
    no undo bookkeeping; `ModelStack` threading replaces `currentSong` per
    call site.
12. **Menu system → data + interpreter** (registry-driven param items).
13. **Display presenter sets**: semantic-state vocabulary catalogued from the
    350 branch sites; views-oled/views-7seg stood up; `Display` god-interface
    dissolves; `swapDisplayType` becomes a pointer swap.
14. **`deluge-grid-views`**: extract render halves of the god-views behind
    `GridView`; components split out; keyboard layouts move in as-is.
15. **`deluge-ui-shell`**: mode state machine reified; input routing;
    notification drain; god-views' interaction halves become thin.
16. **`deluge-engine`**: `AudioEngine::` namespace → instance; sequencer and
    sources consolidated; `COOP-READ` burn-down via snapshot-swap; ◆ params
    step 7 (module interface) lands here.
17. **`deluge-app`**: composition root owns everything that was global;
    implements `deluge_app_*`; flight recorder + companion protocol wired;
    final state reached.

Steps 1–5 are independent-ish and parallelizable; 6 gates 7, 11, 13, 14;
10 gates 16.

### 11.1 Coexisting with a living fork

This refactor will run for a long time against active community feature PRs.
Survival tactics, applied to every extraction:

- **Move + façade.** Each extraction lands with forwarding headers at the old
  paths (`#pragma message` deprecation note + include of the new path), so
  in-flight PRs keep compiling. Façades carry a removal date and are deleted
  on schedule; the ratchet counts façade includes so the burn-down is
  visible.
- **Mechanical steps ship as codemod scripts** (committed under `scripts/`),
  so contributors can rerun the same rename/re-include transform on their
  own branches instead of hand-resolving conflicts.
- **Small, frequent landings.** No long-lived refactor branches; each step in
  §11 is itself a sequence of independently green PRs. The two big "long
  grinds" (model→UI inversion, `currentSong` threading) proceed file-by-file
  behind the ratchet, never as a flag-day.
- **Ratchet visible in PR CI.** A contributor who adds a layer violation or a
  `COOP-READ` sees it in their PR's checks with a pointer to this doc — the
  architecture teaches itself.
- **Size and performance budgets.** Flash/RAM budgets with link-map diffing
  in CI (17 libraries with injected interfaces can quietly cost vtables and
  code size a 3MB-class target feels), plus host-side benchmark smoke for the
  patcher/voice hot paths and on-target timing spot checks via RTT.
- **External APIs get stability promises**: the song file format (corpus-
  enforced) and the companion SysEx protocol (named owner in deluge-app,
  §3.3) are the two interfaces community tooling depends on; both are
  versioned and regression-tested.
