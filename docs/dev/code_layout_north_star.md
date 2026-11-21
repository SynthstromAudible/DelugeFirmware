# Code layout — the north star

Where every piece of the firmware lives once the legacy tree is gone, and the rules that keep it there. The library
*boundaries* are `target_architecture.md` §3 and `target_architecture_north_star.md`; the synth's layers are
`synth_untangling_plan.md` Part 3 and `synth_l1_engine_architecture.md`. This document gives those libraries their
directories.

## Principles

1. **The tree shows the layers.** Directories group by layer, so where a file lives says what it may depend on. A
   reader finds a library by its namespace, and a checker enforces the layering by path.
2. **A path is a namespace.** `deluge::ui::shell` lives at `src/ui/shell/` and is included as `"ui/shell/…"`.
3. **What leaves `src/deluge` is rewritten, not moved.** Code extracted from the legacy tree is redesigned as modern,
   safe, idiomatic C++23 (or later) on the way out: no globals, sentinel ints, macro tables, C arrays with counts,
   out-params or legacy names. Behaviour stays faithful (goldens, parity), form does not. Each extraction is a
   rewrite task with its own spec and review. `src/deluge` is never renamed into a clean name; it shrinks to zero
   and is deleted.
4. **The tree says what a thing is, not what it is written in.** A Rust crate lives in its layer's directory beside
   the C++; a rewrite from one language to the other does not move it.
5. **Plugins are components, not internals.** The Deluge's synth is built from a voice-building library that two
   instruments share (the q31 *classic* and the float *modern*). The host reaches any plugin only through the
   plugin contract, so a second instrument, a foreign engine or an external `.clap` needs no host changes.

## The tree

```
include/libdeluge/        the C ABI to deluge-sdk: the one published interface
third_party/              unmodified vendor code: gsl, printf, the CLAP headers, foreign-engine sources
src/                      directories and CMakeLists.txt only
  base/                   deluge::base       leaf utilities, standard library only
    notify/               the model→UI dirty-set and notice queue
    alloc/                allocator (crate)
  serialization/          deluge::serialization  value tree, XML/JSON read and write, semver
  midi/                   deluge::midi       message vocabulary, device ids
  music/                  deluge::music      scales, keys, note lengths
  plugin/                 deluge::plugin     the host↔plugin contract, CLAP-shaped
    types/                the shared param, setting, sync and display vocabulary
  dsp/                    deluge::dsp        L0 kernels: core/, fixed/, float/
  synth/                  deluge::synth      the voice-building library, templated over a numeric backend
    sources/              L1 sources
    engines/              L1 engines and the engine API; foreign engines' adapters
    voice/                L2 voice kernel
    matrix/               the mod-matrix evaluator
  instruments/            deluge::instruments  one directory per instrument plugin (L3)
    classic/              the q31 instrument: Fixed backend, params, codec, legacy file format, sound FX chain,
                          clap/ (its CLAP export)
    modern/               the float instrument: float backend, voice pool, its matrix rule set, its own params
                          and format, clap/
  effects/                deluge::effects    built-in effects, for the instruments' FX sections and the host's chains
  note_devices/           deluge::note_devices  arpeggiator, transpose, chord
  project/                deluge::project    the project model
  commands/               deluge::commands   the mutation gate and undo
  host/                   deluge::host       the audio host
    sequencer/            transport, playback, clip launch
    chains/               track and master chains
    racks/                kit lanes
    budget/               voice budget and culling policy
    midi/                 MIDI runtime: follow, learn, routing
    clap/                 external .clap hosting
  storage/                deluge::storage
    files/                browse, load and save, the settings store
    stream/               sample streaming (crate)
    resource/             the resource manager (crate)
    sample/               sample readers, conversion and the sample-owned waveform peak index (crates)
    fs/                   the FAT filesystem (crate)
    block_device/         block-device driver and adapters (crates)
  ui/                     deluge::ui
    gfx/                  oled/, grid/, seg7/: pure drawing
    widgets/              core/, oled/, grid/: the immediate-mode toolkit
    views/                pure view models, e.g. waveform/ (scroll, zoom, columns, peaks, marker pins)
    controls/             gold-knob pages, takeover, follow, learned bindings
    l10n/                 string tables
    shell/                the UI runtime: scene stack, input lowering, tick, flush
    scenes/               scenes, services and the menu; bind/ is their read and write ports
  app/                    deluge::app        the composition root
    ports/                the host's implementations of the scenes' ports
    companion/            the SysEx companion protocol
  platform/
    device/               the binary linking the app to deluge-sdk
    host/                 the host BSP build and its compat headers
  deluge/                 LEGACY: shrinks to zero, then is deleted
tests/                    mirrors src/: a library at src/<path> is tested under tests/<level>/<path>
```

### Layers

Each layer depends only on the layers listed before it. `base` is open to every library.

| # | layer | libraries | never includes |
|---|---|---|---|
| 1 | vocabulary | `base`, `serialization`, `midi`, `music` | anything above |
| 2 | contract | `plugin` | anything above |
| 3 | audio | `dsp`, then `synth`, then the plugins: `instruments/*`, `effects`, `note_devices` | `project`, `host`, `storage`, `ui`, `app` |
| 4 | model | `project`, then `commands` | `host`, `storage`, `ui`, `app` |
| 5 | runtime | `host`, `storage` | `ui`, `app` |
| 6 | UI | `ui/gfx`, `ui/widgets`, `ui/views`, `ui/controls`, `ui/l10n`, `ui/shell`, `ui/scenes` | `app` |
| 7 | app | `app` | — |

Rules inside the layers:

- **`synth/` names no backend.** Its contracts are templates (`BasicSource<B>`, `BasicEngine<B>`, the voice, the
  matrix evaluator); `Fixed` belongs to `instruments/classic`, the float backend to `instruments/modern`. `synth/`
  includes only `dsp/`, `base/` and `plugin/types/`.
- **Only instruments include `synth/`, and no instrument includes another.** Shared pieces move down into `synth/` or
  `dsp/`; they are never borrowed sideways.
- **The host sees plugins through `plugin/` and each plugin's public headers only**, never `synth/`, `dsp/` or a
  plugin's `detail/`. Engines are loaded by an instrument, never by the host.
- **Foreign L1 engines** live in `synth/engines/<name>/` with their vendor sources in `third_party/`; each instrument
  takes them through its own backend (natively when the numerics match, through the float adapter when not).
  External `.clap` instruments and effects attach in `host/clap/`.
- **The UI's order:** `gfx` < `widgets` < `views` < `controls` < `shell` < `scenes`. `gfx/oled` and `gfx/grid`, and
  `widgets/oled` and `widgets/grid`, never include each other. `shell` knows no model; `scenes` reads and writes
  the model only through `scenes/bind`'s ports, which `app/ports/` implements.
- **Consumer-owned interfaces break upward arrows.** A lower library declares the narrow interface it needs and a
  higher one implements it (`dsp`'s `SampleSource`, the scenes' ports); `app` wires them.

### Where today's directories go

The libraries that are already clean move as they are; legacy directories are rewritten into their destinations.

| today | target |
|---|---|
| `src/foundation` | `base/` |
| `src/notify` | `base/notify/` |
| `src/shell/fixed_vector.h` | `base/` |
| `src/plugin-api` | `plugin/` |
| `src/deluge-project` | `project/` |
| `src/music-theory` | `music/` |
| `src/serialization`, `src/midi` | unchanged |
| `src/synth` | `synth/`, with every `Fixed` instantiation moving to `instruments/classic` |
| `src/instruments/deluge` | `instruments/classic/` |
| `src/effects` | `effects/` |
| `src/effects/host_chain` | `host/chains/` |
| `src/gfx` | `ui/gfx/` |
| `src/ui` | `ui/widgets/` |
| `src/shell`, `src/controls`, `src/scenes` | `ui/shell/`, `ui/controls/`, `ui/scenes/` |
| `crates/deluge_alloc` | `base/alloc/` |
| `crates/deluge_resource` | `storage/resource/` |
| `crates/deluge_sample_*` | `storage/stream/`, `storage/sample/` |
| `crates/embedded-fatfs` | `storage/fs/` |
| `crates/block-device-*` | `storage/block_device/` |
| `crates/deluge_rust` | split by what each module serves |
| `src/gsl`, `src/lib` | `third_party/` |
| `src/bsp/rust` | `platform/device/` |
| `sim` | `platform/host/` |
| `src/OSLikeStuff` | retires with the Embassy scheduler; what remains goes to `platform/` |
| `src/*.h`, `src/*.hpp` (board config and layout, `definitions*`, the NEON shim, `mem_functions.h`) | dissolved into their owners: `ui/shell/` profiles, `platform/`, `third_party/`, `base/` |

`sample-view` (on `feat/r2c2-waveforms`) is three things and lands in three places:

| file | target | why |
|---|---|---|
| `divide.h` | `base/divide.h`, beside `dsp-next/core/divide.h`'s reciprocal divide | the A9 has no integer divide; every layer needs the workaround |
| `peaks.h`, `view.h` | `ui/views/waveform/` | what the screen shows of a waveform: UI value types, owned by the consumer |
| `peak_cache.h` | `app/ports/waveforms/` | the host's cache behind the waveforms port, until the sample-owned peak index in `storage/sample/` replaces it |

The legacy tree's destinations:

| `src/deluge/…` | rewritten into |
|---|---|
| `model/` | `project/`; `commands/` (action, consequence); `host/sequencer/` (playback state) |
| `gui/` | `ui/scenes/`, `ui/l10n/`, `ui/gfx/` (colour); `app/ports/` (the shell host) |
| `storage/` | `storage/files/`, `serialization/` |
| `util/`, `memory/` | `base/` |
| `io/` | `midi/`, `host/midi/`, `base/` (debug log) |
| `modulation/` | `plugin/types/` (param vocabulary), `synth/` (patcher), `host/` (automation), `note_devices/` (arpeggiator) |
| `processing/` | `instruments/classic/` (`Sound`), `host/` (mixing, live input, metronome, stem export) |
| `playback/`, `sequence/`, `sync/`, `runtime/` | `host/sequencer/` |
| `hid/` | `ui/shell/` (input lowering), `ui/gfx/` |
| `dsp-next/` | `dsp/`, a plain move: it is new code |
| `dsp/` | `dsp/` and `synth/sources/`: the time stretcher is rewritten |
| `bench/`, `harness/` | `tests/device/bench/`, `tests/harness/` |

## Rules

1. **Directory = namespace segment, in snake_case.** No hyphenated directories; no directory whose namespace differs.
2. **One CMake target per library directory**, named for its path (`deluge_ui_shell`, `deluge_instruments_classic`).
   A library's public headers sit at its root; private ones in `detail/`, which no other library includes.
3. **Includes are bare paths from `src/`.** Vendor headers are system includes from `third_party/` (`<gsl/…>`,
   `<clap/…>`); the C ABI is `"libdeluge/…"`.
4. **One dependency table, one checker.** `scripts/layers.toml` lists every library and what it may include; one
   path-based checker reads it. Every library-boundary rule is a row in that table, and allowlists only shrink.
5. **No new top-level directories.** The checker refuses a `src/<dir>` the table does not name, and any file at the
   `src/` root.
6. **No include path resolves under both roots.** Legacy code includes relative to `src/deluge/`, which precedes
   `src/` on its include path, so a file at `src/X/y.h` shadows nothing only while `src/deluge/X/y.h` does not exist.
   The checker refuses any relative path present under both. The check is per file, so a new `src/storage/` can
   grow beside the legacy `storage/`.
7. **`src/deluge` only shrinks.** A ratchet on its file count refuses additions; code leaves it only by rewrite into
   its destination; only `app/` includes it.
8. **Rust crates live in their layer.** Each crate's directory holds its `Cargo.toml`; the root `Cargo.toml` is a
   workspace over `src/**`. A crate is named `deluge_<path>`, and C++ reaches it only through the C header in its
   own directory.
9. **Tests mirror sources** under `tests/<level>/`; the registry (`tests/registry.toml`) names them by path.
10. **C++23 or later outside `src/deluge`**, and every library has a README stating its rules.

## Getting there

**Step 1: the checker.** `layers.toml` and the single checker go in first, describing today's tree. They absorb the
library-boundary rules now spread over `check_library_boundary.py`, `check_layer_rules.py`, `check_gfx_layer.py`,
`check_synth_boundary.py`, `check_dsp_boundary.py` and `check_platform_boundary.py`, and their allowlists, and add
the top-level and shadowing checks, with today's directories grandfathered in. Nothing moves; the target table
becomes the enforced one.

**Step 2: the relocation pass.** Each clean library moves in its own commit: `git mv`, include rewrites, the
namespace rename its path calls for, and the CMake, checker, labeler and doc-hygiene tree updates. No behaviour
changes; every commit is gated on the goldens (bit-exact), the x86 virtual-clock parity diff (0 diffs) and the
nightly tier with both song corpora, and the pass ends with one emulator sweep against `main`.

| library | namespace rename |
|---|---|
| `foundation`, `notify` → `base/` | `deluge::notify` → `deluge::base::notify` |
| `plugin-api` → `plugin/` | none |
| `deluge-project` → `project/` | none |
| `music-theory` → `music/` | none |
| `instruments/deluge` → `instruments/classic/` | `deluge::instrument` → `deluge::instruments::classic` |
| `effects/host_chain` → `host/chains/` | `deluge::host_chain` → `deluge::host::chains` |
| `gfx` → `ui/gfx/` | `deluge::gfx` → `deluge::ui::gfx` |
| `ui` → `ui/widgets/` | `deluge::ui::{oled,grid}` → `deluge::ui::widgets::{oled,grid}` |
| `shell` → `ui/shell/` | none |
| `controls` → `ui/controls/` | `deluge::controls` → `deluge::ui::controls` |
| `scenes` → `ui/scenes/` | `deluge::scenes` → `deluge::ui::scenes` |
| `crates/*` → their layers | crate names to `deluge_<path>` |
| `gsl`, `lib` → `third_party/` | none |
| `bsp/rust` → `platform/device/`, `sim` → `platform/host/` | none |

`synth/`'s `Fixed` instantiations move to `instruments/classic` as part of the classic instrument's state work, not
in this pass: that is a design change, gated on the goldens like any other.

**Step 3: `dsp-next` → `dsp/`.** After the legacy `dsp/` is retired by the time-stretcher rewrite: until then both
trees hold `dsp/…` paths and rule 6 refuses the move.

**Step 4 onward: extraction lands in place.** Branches in flight are re-cut to the final paths before they merge
(`feat/r2c2-waveforms` as above, giving `app/` its first content); work started after step 2 uses only final paths.
The existing programs (the UI read interface, the model line, the synth's tracks, the storage lanes) extract from
`src/deluge` by rewrite into their destinations, and `instruments/modern/` is created at its final path.
`src/deluge` is deleted when it is empty; `app/` is the composition root from its first file, never a rename of the
legacy tree.

## Open

- The instruments' names, `classic` and `modern`, are working names.
- `crates/deluge_rust`'s modules each go where they serve; the split is decided when the crate is next touched.
- Whether `fixed_point` belongs in `base/` or stays in `dsp/core/`: `base/` if a non-audio library (the project's
  values, the knob math) needs it, as `target_architecture.md` §3.2 has it.
