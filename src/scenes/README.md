# deluge_scenes

The first UI layer allowed to include the model (spec: `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md`,
spec E of the UI stack redesign). Task 1 is the skeleton only: the library target, the shared
CppSpec CMake module, the l10n bridge, and `scenes::deluge_profile()`. Menu nodes, bindings,
context resolution and `MenuScene` are later tasks in the same plan
(`docs/superpowers/plans/2026-08-27-ui-scenes-menus-1.md`).

Rules:
1. `src/scenes/**` may include `scenes/**`, `shell/**`, `ui/**`, `gfx/**`, `notify/**`,
   `libdeluge/**`, the standard library, and -- being the first model-aware layer --
   `model/**`, `modulation/**`, `playback/**`, `processing/**`, `storage/**`, `gui/l10n/**`,
   `definitions_cxx.hpp`.
   Never `gui/` otherwise, never `hid/`. `model/model_stack.h` is narrower still: only
   `menu/context.cpp` and `menu/bindings.cpp` may include it, and `menu::model_stack_for()` is
   the single entry point everything else uses to reach a `ModelStack` (task-3 ruling 6).
2. `snake_case` functions/variables, `PascalCase` types, `k` constants; namespace
   `deluge::scenes`.
3. No heap in library code; no wall clock (later tasks: `Scene::next_timer()` and
   `RepaintGate::request_repaint_after` are how a blink stays alive).

## Link direction (ruling Q4)

`deluge_scenes` links `deluge_app` (an `OBJECT` library) PUBLIC, alongside `deluge_ui_shell` --
that's the model-aware layer this library exists to be. **Nothing links `deluge_scenes` yet**;
that starts once a later task needs a real entry point (a registered scene, a menu). Until then
its only consumer is its own spec executable, `deluge_scenes_spec` (`sim/CMakeLists.txt`).

`add_subdirectory(scenes)` in `src/CMakeLists.txt` and `sim/CMakeLists.txt` both come **after**
`add_subdirectory(deluge)`, since `deluge_scenes` needs `deluge_app` to already exist to link
against it.

## The l10n bridge (`l10n_bridge.h`, ruling Q3)

`src/shell` has no l10n of its own -- a board profile supplies `BoardProfile::text`/`error_text`,
or leaves them null (`ui::shell::deluge_profile()` does exactly that,
`src/shell/profiles/deluge.cpp:143-144`, deliberately: the `l10n::String` table these resolve
against belongs to `gui/l10n/`, which `src/shell` may not include). `src/scenes` is the layer
allowed to include `gui/l10n/`, so it supplies the bridge:

| function | purpose |
|---|---|
| `text_lookup(uint16_t)` | `l10n::getView(static_cast<l10n::String>(string_id))`. Matches `ui::shell::TextLookup`. |
| `error_lookup(int16_t)` | A hand-transcribed copy of `getErrorMessage(Error)`'s switch (`src/deluge/hid/display/display.cpp:7`) -- `src/scenes` may not include `hid/`, so this cannot call the original. Matches `ui::shell::ErrorLookup`. |

## The scenes profile (`profile.h`, ruling Q3)

`scenes::deluge_profile()` is `ui::shell::deluge_profile()` field-for-field, with `.text` and
`.error_text` filled in from the bridge above -- a function-local `static const BoardProfile`
copy-initialised from the shell's own profile. `BoardProfile` is spans and function pointers, so
the copy is cheap and owns nothing; `profile_spec.cpp` (in `sim/scenes_spec/`) is the drift guard
that asserts every field *other* than the two hooks stays byte-identical to the shell's copy (spans
compared by `.data()`/`.size()`, not by value) -- this is what the two-profile split costs, and the
one place it would silently break.

`deluge_scenes` and every scenes spec pass `scenes::deluge_profile()` to `Shell`/
`RunOptions::profile`, never `ui::shell::deluge_profile()` directly -- the shell's own copy exists
so `src/shell`'s specs don't need the l10n bridge, not because it's the profile applications use.

## Testing: two spec trees (ruling Q5)

`src/scenes` is model-aware, so its specs need the real `deluge_app` -- not `tests/`'s
mock display/print doubles, which define symbols `deluge_app` also defines. Two spec locations,
different reasons:

- **`tests/spec/scenes/`** -- pure parts that need no model at all (a later task's node builders,
  node ids, validator). Built into `tests/`'s `all_specs`, same as every other layer's specs.
- **`sim/scenes_spec/`** -- model-backed specs (`l10n_bridge_spec.cpp`, `profile_spec.cpp` today),
  built into their own executable, `deluge_scenes_spec`, in the `sim/` project: the one place a
  spec can link `deluge_app`, the host BSP, and `deluge_scenes` together. `cmake/deluge_specs.cmake`
  (factored out of `tests/spec/CMakeLists.txt` this task, so both projects share one copy) supplies
  the CppSpec fetch and `create_specs_driver()`; `deluge_scenes_spec_support` is the mock-free
  support library (`tests/harness/ui_script.cpp`, `snapshot.cpp`) a spec that drives a script needs.
  Run with `ctest --test-dir build-sim -R scenes` after
  `cmake --build build-sim --target deluge_scenes_spec`.

Nothing under `src/scenes` depends on either spec tree existing; both are testing infrastructure
looking in, the same relationship `tests/spec/shell/` has to `src/shell`.
