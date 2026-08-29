# Scenes and menus, part 1 (`deluge_scenes` foundation + `MenuScene`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create the `deluge_scenes` library and land spec E's first migration step — the declarative C++ menu tree (`Node` literals), one interpreter (`MenuScene`), the bindings that read/write the model through `ModelStack`, the shortcut tables, the tree validator, and the host tool that dumps the tree to JSON — so `gui/menu_item/**` (24k lines) can be retired once spec F's shell-as-guest routes `soundEditor` to `MenuScene`.

**Architecture:** `src/scenes/` is the first layer allowed to include the model. `menu/` holds pure data (`Node`, `Binding`, `ParamRef`, builders, the node-id registry) plus `bindings.cpp` (the only file that touches `ModelStack`/`AutoParam`) and `context.cpp` (the port of `SoundEditor::setup`'s context resolution). `MenuScene` renders the current node through the toolkit (`list`/`columns`/value widgets/meters) on the `Main` OLED surface, paints only blinking shortcut and patching-source pads on the grid as a non-dimming overlay, and applies input to the focused node's `Binding`. Tables are `inline constexpr Node` literals in `menu/nodes/*.cpp`, one file per legacy root; custom items are ≤10 small classes behind `CustomItem`. Tests run in a new `deluge_scenes_spec` executable inside the `sim/` project (the only host build that links the real model), plus the pure parts in `tests/spec`.

**Tech Stack:** C++26, CMake, CppSpec, `deluge_ui_shell`, the toolkit (`deluge_ui_core`/`deluge_ui_oled`/`deluge_ui_grid`), `deluge_notify`, the model (`src/deluge/model`, `modulation/params`), `sim/` host build.

**Spec:** `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md` (spec E) §1–4, §7 step 1, §8, §9 menu rows, §10. Depends on the **shipped** shell (`src/shell/`, spec D + its §13 amendments: `Scene` with `render_oled`/`render_grid`/`leds`/`after_render`/`handle`, `SceneId`, `SceneKind`, `SceneState`, `SceneRegistry`, `UiEvent`, `UiContext`, `Result`, `Action`, `BoardProfile`, `TextLookup`/`ErrorLookup`) and the **shipped** harness (`tests/harness/`, `tests/spec/shell/support/script_runner.h`, spec G + its §10 amendments: `run_script`, `expect_script_goldens`, `RunOptions`). Survey of the legacy menu system with line numbers: `.superpowers/sdd/2026-08-27-ui-scenes-menus/menu-survey-summary.md`. Reconciliation of this plan against what actually shipped: `.superpowers/sdd/2026-08-27-ui-scenes-menus/e1-reconciliation.md`; the controller's rulings on its nine open questions: `.superpowers/sdd/2026-08-27-ui-scenes-menus/e1-plan-rulings.md`.

**Scope note (spec E is several plans):** this is E1. E2 = custom items + browsers + `RenameScene` + `ContextMenuScene`; E3 = overlays (`CountInScene`, `ParamPopupScene`, `ScreensaverScene`) + the model-touching services (`GlobalControls`, `ModKnobs`, `MidiLearn` — note `ButtonId::ModEnc0`/`ModEnc1` pushes route **scene-first**, not to `mod_knobs`, per `router.h:60-65`); E4–E6 = the root scenes in spec E §7 order (Keyboard/Performance/AudioClip/Slicer/Marker/Recorder; Session/Arranger/Automation; NoteClip). Spec F wires the application root through `ui::shell::SongHooks{SceneId (*root_for_song)(void*); void* ctx;}` (`shell.h:44`) and asserts legacy-vs-new parity with `RunOptions::second_registry` (`script_runner.h:73`). Each is its own plan, written when its predecessor lands.

### Revision 2026-08-29

This plan was written before `src/shell/` (plan D) and `tests/harness/` (plan G) existed. Every line below is now checked against the shipped headers at `ui/harness` tip `5c72cec9f`; the survey is `.superpowers/sdd/2026-08-27-ui-scenes-menus/e1-reconciliation.md` (84 rows: 41 MATCHES, 3 RENAMED, 17 CHANGED SHAPE, 5 MISSING, 18 NEW) and the binding decisions are `e1-plan-rulings.md`. One line per delta:

- **`menu::Hint` and `menu::IntRange` are deleted** — `ui::oled::Hint` (`ui/oled/meters.h:41`, fourteen enumerators) and `ui::oled::Range` (`ui/oled/values.h:18`) already exist and are what every value widget takes; the old "spec listed ten, legacy has twelve" amendment goes away with them (reconciliation §4 rows L83/L87, NEW #1/#2).
- **`MenuScene` is a grid overlay** (`is_grid_overlay() == true`, `dims_grid() == false`, `occludes_grid() == false`) and paints blinking shortcut/patch-source pads in `render_grid`, because `LedFrame` (`ui/core/led_frame.h:36`) has no pad concept at all — the old plan put the pad blink in `leds()` (ruling Q1; reconciliation §1 row L187, §5 Q1).
- **`BoardProfile::shortcuts`/`ShortcutTable` is removed from the shell** and the seven legacy tables become scenes-owned, keyed by `(MenuContext::Root, shift)` — two `std::span<const uint16_t>` layers cannot hold five 2-D plus two 1-D tables selected by context (ruling Q2; reconciliation §3 row L255).
- **`install_l10n(BoardProfile&)` is replaced by `scenes::deluge_profile()`**, a copy of `shell::deluge_profile()` with `.text`/`.error_text` filled in — the shell's profile is a function-local `static const` with nothing to install into, and naming `scenes::text_lookup` from `src/shell` would invert the layers (ruling Q3; reconciliation §3 row L62, §5 Q3).
- **`deluge_scenes` links `deluge_app`, and nothing links `deluge_scenes`** until spec F — the old plan's "`deluge_app` links `deluge_scenes`" is a cycle, and `add_subdirectory(scenes)` moves from after `shell` to after `deluge` (ruling Q4; reconciliation §7 row L59). The link-closure edits (three `build.rs`, two task scripts) are therefore **not** part of E1.
- **CppSpec and `create_specs_driver` are factored into `cmake/deluge_specs.cmake`** shared by `tests/` and `sim/`; the sim's spec support is mock-free (no `deluge_spec`, whose `mock_display.cpp`/`mock_print.cpp` collide with the real `deluge_app`) — the old plan's "reuse that function if `sim/` can include `tests/spec`'s CMake helper" was not a small question (ruling Q5; reconciliation §6 row L45/L59).
- **Per-kind render helpers with typed edit storage** replace the single `int32_t edit_value`: `toggle` takes a `bool&`, `enum_value` takes an index **plus** a `ListState&` and a `ScrollText&` and has no `focused`, and `decimal_value` wants `f.bounds()` rather than `menu_rect(f)` (ruling Q6; reconciliation §4 rows L85–L88).
- **`SceneId::arg` is a stable node id** assigned by the validator and emitted by the dump, so a `.state` golden reads `app16/<id>` and two different menu nodes are distinguishable — `Shell::state_lines()` spells an application scene as `app16[/<arg>]` and nothing else (ruling Q7; reconciliation §6 row L117, §5 Q7).
- **The depth test precedes the back read** — reading *is* claiming (`frame_context.h:137-181`), so "read `back_pressed()` then decide whether to `claim_back()`" is impossible as the old plan wrote it; at depth 0 `render_oled` never touches `back_pressed()` and `handle(Button{Back} down)` returns `Action::Kind::Pop` (ruling Q8; reconciliation §2 row L194, §5 Q8).
- **`save_state` persists a 32-byte `MenuSavedState`, not `MenuState`** — the slot is `alignas(8) uint8_t bytes[256]` (`scene.h:50,76`) and `MenuState` as originally sketched had no room to grow (ruling Q9; reconciliation §1 row L33, §5 Q9).
- **Task 11 loses its `RunOptions::initial_scene` sub-task** — it shipped with plan G's T3 (`script_runner.h:66`) — and its script goldens move to `tests/golden/ui/shell/menu_*/`, the root `expect_script_goldens` pins (reconciliation §6 rows L112/L115, NEW #3).
- **Six held-back inputs become seven**, `Result::unhandled()` is confirmed to exist, `Action::Kind::RepaintAll` replaces any `Repaint{SurfaceRole::None}` sentinel, `build-sim-gfx` becomes `build-sim`, `computeCurrentValueForStandardMenuItem` is located (`gui/menu_item/value_scaling.{h,cpp}`), the `soundEditorRootMenu` child list is corrected (it has 28 children, `menus.cpp:1521-1551`), and the ratchet is 11 rule entries, not 8 (reconciliation §2 rows L49/L50/L52, §6 row L17, §7 row survey-L13).

---

## Global Constraints

- Branch: `ui/scenes-1` off `ui/harness` (stacked on the shipped shell + harness). "Builds" = `./dbt build Debug` compiles to the known ThinLTO link failure; `cmake --build build-sim --target deluge_host` links; the `tests/` suite green; **and** the sim project's new spec executable green: `cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON && cmake --build build-sim --target deluge_scenes_spec && ctest --test-dir build-sim -R scenes`.
- Layering (umbrella §rules, spec E §3 rule 4): `src/scenes/**` may include `scenes/**`, `shell/**`, `ui/**`, `gfx/**`, `notify/**`, `libdeluge/**`, std, and — being the first model-aware layer — `model/**`, `modulation/**`, `playback/**` (read), `storage/**`, `gui/l10n/**`, `definitions_cxx.hpp`. It may **not** include `gui/` (other than `gui/l10n/`), `hid/`, or `gui/ui/ui.h`. Only `src/scenes/menu/bindings.cpp` and `src/scenes/menu/context.cpp` may include `model/model_stack.h`/`modulation/automation/auto_param.h`; `currentSong` reads are confined to `context.cpp` and counted by a ratchet row. Task 12 adds the rows; the ratchet ships with **11 rule entries** today (`model-includes-ui`, `model-calls-ui`, `model-calls-display`, `gfx-includes-outside`, `notify-is-leaf`, `ui-includes-outside`, `ui-oled-no-grid`, `ui-grid-no-oled`, `ui-widgets-read-input-directly`, `shell-includes-outside`, `shell-no-clock` — the last appearing as two rows, one glob each) and nothing under `src/scenes` in `scripts/layer_rules_baseline.txt`.
- Link direction (ruling Q4): `deluge_scenes` links `deluge_app`; **nothing links `deluge_scenes`** until spec F needs an entry point. `add_subdirectory(scenes)` goes after `deluge` in `src/CMakeLists.txt`. No `build.rs`/task-script link-closure edit belongs to E1.
- Menus are data: no `Node` has behaviour beyond function pointers in `Binding`/`relevant`/`dynamic_*`; a `Kind` exists only if ≥3 items use it; `Custom` count ≤ 10 (validator-enforced).
- Parity: `MenuScene` renders through the toolkit widgets that were pixel-pinned to the legacy renderers (`list` ↔ `MenuItem::drawItemsForOled`, `columns` ↔ `HorizontalMenu::renderMenuItems`, value widgets ↔ `Integer/Decimal/Toggle/Selection::drawPixelsForOled`, `meter()` ↔ `Number::renderInHorizontalMenu`); per-kind **pixel** goldens at 128×48 and 128×64, recorded by the spec directly under `tests/golden/ui/menu/`; behaviour cites the legacy line it ports (`sound_editor.cpp`, `menu_item.cpp`, the item classes).
- Script goldens (level 2) live under **`tests/golden/ui/shell/menu_*/`**: `expect_script_goldens` hardcodes `fs::path(UI_GOLDEN_DIR) / name` (`script_runner.h:694`) and `UI_GOLDEN_DIR` is `tests/golden/ui/shell` (`tests/spec/CMakeLists.txt:63`). Scripts live under `tests/ui-scripts/shell/` (`UI_SCRIPTS_DIR`, `:62`); `got/` trees under `<build>/got` (`UI_GOT_DIR`, `:64`).
- `.state` spelling: `Shell::state_lines()` writes an application scene as **`app16`**, or `app16/<arg>` when `arg != 0` (`shell.cpp:885-898`). Category order is fixed (`scene` top-of-stack first, then `led`, `pad`, `indicator`, `lock`, `notice`), ASCII-sorted within each; line width is 96 bytes (`UI_SCRIPT_STATE_LINE_BYTES`, `tests/harness/ui_script.h:8`). The lock line is the literal `lock input`.
- **`MenuScene` reads a given input in at most one render pass per tick** (spec D §13 §3, `src/shell/README.md`, `shell.h` class doc): reading *is* claiming (`frame_context.h` rule 2, `:137-181`), `InputClaims::double_claim` (`:48`) is the diagnostic, and every `MenuScene` spec asserts it stayed false. `MenuScene` renders two surfaces (`Main` OLED and the grid), so this is a real constraint, not a trivial one: **only `render_oled` reads input; `render_grid` paints and reads nothing.**
- **Post-render delivery is claim-gated on seven inputs, not six** (`router.h:73-83`): `Encoder{Select}`→`turn`, `Encoder{GoldUpper}`→`edit0`, `Encoder{GoldLower}`→`edit1`, `Encoder{Horizontal}`→`horizontal`, `Encoder{Vertical}`→`vertical`, `EncoderButton{SelectEnc}`→`select`, `Button{Back}`→`back`. An input a widget claimed during render never reaches `handle()`; an unclaimed one is delivered by `Router::route_post_render` after the render pass. Pads are never masked (`frame_context.h` rule 4, `:204-207`), so the shortcut-pad path always reaches `handle()`.
- Naming: `snake_case`; `PascalCase` types; `k` constants; namespace `deluge::scenes` / `deluge::scenes::menu`. No heap in scenes; POD state; no wall clock (`Scene::next_timer()` and `RepaintGate::request_repaint_after` are how a blink stays alive — the `shell-no-clock` ratchet row extends here in Task 12).
- Never commit `src/bsp/rust/Cargo.lock`; never touch `src/bsp/rza1`; commit per "Commit" step after `./dbt format`.

---

## File structure

```
cmake/deluge_specs.cmake                    CppSpec FetchContent + create_specs_driver(), shared by tests/ and sim/
src/scenes/CMakeLists.txt                   deluge_scenes (STATIC)
src/scenes/README.md
src/scenes/l10n_bridge.h / .cpp             text_lookup / error_lookup (l10n::getView, getErrorMessage port)
src/scenes/profile.h / .cpp                 scenes::deluge_profile() — shell's profile + the l10n bridge
src/scenes/menu/node.h                      Kind, Layout, ParamRef, Binding, CustomItem, Node, constexpr builders
src/scenes/menu/node_id.h / .cpp            stable node ids: assign_node_ids / node_id / node_by_id (SceneId::arg)
src/scenes/menu/context.h / .cpp            MenuContext + context_for() (port of SoundEditor::setup's resolution)
src/scenes/menu/bindings.h / .cpp           read/write through ModelStack/AutoParam; get/set escape hatch; ranges
src/scenes/menu/relevance.h / .cpp          the relevance predicates (has_lpf, is_kit, …) shared by tables
src/scenes/menu/validate.h / .cpp           validate_tree(roots, contexts) -> FixedVector<Issue, 64>
src/scenes/menu/nodes/settings.cpp          settingsRootMenu subtree
src/scenes/menu/nodes/sound_*.cpp           soundEditorRootMenu subtrees (filters, compressor, unison, osc, env, lfo, master, arp, randomizer, fx, sidechain, mixer, voice, bend, actions, root)
src/scenes/menu/nodes/roots.cpp             the other roots (drum, midi/cv, midi drum, gate drum, audio clip, performance, song, kit global fx)
src/scenes/menu/shortcuts.h / .cpp          the seven legacy shortcut tables, keyed by (Root, shift)
src/scenes/menu/menu_scene.h / .cpp         MenuScene (Scene), MenuState, MenuSavedState
src/scenes/menu/menu_dump_main.cpp          deluge_menu_dump: JSON in the retired DocEmitter's schema
src/scenes/registry.h / .cpp                scene_registry(): the application's Scene* table (MenuScene only in E1)
tests/spec/scenes/*_spec.cpp                pure parts: node builders, node ids, validator, relevance with a fake context, shortcut table shapes
sim/scenes_spec/main.cpp                    CppSpec driver (generated by create_specs_driver)
sim/scenes_spec/*_spec.cpp                  model-backed: l10n bridge, profile, bindings, context_for, MenuScene render/handle, tables, level-2 scripts
tests/golden/ui/menu/*.pbm                  per-kind PIXEL goldens, recorded by the spec directly
tests/ui-scripts/shell/menu_*.txt           level-2 scripts
tests/golden/ui/shell/menu_*/               level-2 script goldens (the root expect_script_goldens pins)
website/src/data/generated/menu-hierarchies.json   committed dump (new file — the generator spec E named does not exist)
```

Modified: `src/CMakeLists.txt` (`add_subdirectory(scenes)` **after** `deluge`), `sim/CMakeLists.txt` (`deluge_scenes_spec`, `deluge_menu_dump`, ctest rows with `TIMEOUT`s), `tests/spec/CMakeLists.txt` (include the shared `cmake/deluge_specs.cmake` instead of declaring CppSpec/`create_specs_driver` itself), `src/shell/profile.h` + `src/shell/profiles/deluge.cpp` + `src/shell/README.md` (remove `ShortcutTable`/`BoardProfile::shortcuts`, Task 9), `scripts/check_layer_rules.py` + `scripts/layer_rules_baseline.txt`, `.github/workflows/tests.yml` (menu JSON diff), `src/deluge/gui/menu_item/generate/` (deleted in Task 6 once its six subtrees are re-declared; `menus.cpp:244`'s include removed), `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md` (§4.3 corrected; §4 amendments), `docs/superpowers/specs/2026-08-26-ui-shell-design.md` §13 (the `ShortcutTable` removal), `docs/superpowers/specs/2026-08-26-ui-test-harness-design.md` §10 (the shared cmake module).

---

### Task 1: `deluge_scenes` skeleton, shared spec cmake module, l10n bridge and `scenes::deluge_profile()`

**Files:**
- Create: `cmake/deluge_specs.cmake`, `src/scenes/CMakeLists.txt`, `src/scenes/README.md`, `src/scenes/l10n_bridge.{h,cpp}`, `src/scenes/profile.{h,cpp}`, `sim/scenes_spec/l10n_bridge_spec.cpp`, `sim/scenes_spec/profile_spec.cpp`, `tests/spec/scenes/.keep`
- Modify: `src/CMakeLists.txt`, `tests/spec/CMakeLists.txt`, `sim/CMakeLists.txt`

**Interfaces:**
```cpp
// src/scenes/l10n_bridge.h
namespace deluge::scenes {
/// `l10n::getView(static_cast<l10n::String>(string_id))`. Matches ui::shell::TextLookup.
std::string_view text_lookup(uint16_t string_id);
/// The `getErrorMessage(Error)` switch (src/deluge/hid/display/display.cpp:7), transcribed here:
/// the scenes layer may not include hid/. Matches ui::shell::ErrorLookup.
std::string_view error_lookup(int16_t error_code);
}
// src/scenes/profile.h — ruling Q3
namespace deluge::scenes {
/// `ui::shell::deluge_profile()` field-for-field, with `.text = text_lookup` and
/// `.error_text = error_lookup` filled in. The shell's own profile stays text-less
/// (`src/shell/profiles/deluge.cpp:129-149` states `.text = nullptr` deliberately), so this
/// copy is what `deluge_scenes` and every scenes spec pass to `Shell`/`RunOptions::profile`.
/// `BoardProfile` is spans and function pointers, so the copy is cheap and owns nothing.
const ui::shell::BoardProfile& deluge_profile();
}
```
CMake:
- `cmake/deluge_specs.cmake` holds, verbatim from `tests/spec/CMakeLists.txt:1-5` and `:72-102`, the CppSpec `FetchContent_Declare`/`MakeAvailable` pair and the `create_specs_driver(driver_name spec_dir)` function (`file(GLOB_RECURSE … *_spec.cpp)` → `create_test_sourcelist` → `add_executable` → link `c++spec` → `target_include_directories(${driver_name} PRIVATE ${spec_dir}/..)` → one `add_test(NAME <stem> COMMAND ${driver_name} <relative-id> --verbose)` per spec). `tests/spec/CMakeLists.txt` replaces those two blocks with `include(${CMAKE_CURRENT_LIST_DIR}/../../cmake/deluge_specs.cmake)`; nothing else in it changes.
- `src/scenes/CMakeLists.txt`: `add_library(deluge_scenes STATIC l10n_bridge.cpp profile.cpp)` (grown per task), `target_include_directories(deluge_scenes PUBLIC ${SHARED_INCLUDE} ${CMAKE_CURRENT_LIST_DIR}/../../include ${CMAKE_CURRENT_LIST_DIR}/../deluge)`, `target_link_libraries(deluge_scenes PUBLIC deluge_ui_shell deluge_app)` (`deluge_ui_shell` already brings `deluge_ui_core deluge_ui_oled deluge_ui_grid deluge_notify` PUBLIC, `src/shell/CMakeLists.txt:11`), `target_compile_options(deluge_scenes PRIVATE ${DELUGE_COMMON_COMPILE_OPTIONS})`, `set_target_properties(… CXX_STANDARD 26 CXX_STANDARD_REQUIRED YES)`.
- `src/CMakeLists.txt`: `add_subdirectory(scenes)` on the line **after** `add_subdirectory(deluge)`, with a comment naming ruling Q4 (`deluge_scenes` links `deluge_app`; nothing links `deluge_scenes` until spec F).
- `sim/CMakeLists.txt`: `include(${REPO_ROOT}/cmake/deluge_specs.cmake)`; a mock-free support library `deluge_scenes_spec_support` (STATIC: `${REPO_ROOT}/tests/harness/ui_script.cpp`, `${REPO_ROOT}/tests/harness/snapshot.cpp`; `target_include_directories(… PUBLIC ${REPO_ROOT}/tests ${REPO_ROOT}/src ${REPO_ROOT}/src/deluge ${REPO_ROOT}/include)`; `target_compile_definitions(… PUBLIC UI_SCRIPTS_DIR="${REPO_ROOT}/tests/ui-scripts/shell" UI_GOLDEN_DIR="${REPO_ROOT}/tests/golden/ui/shell" UI_GOT_DIR="${CMAKE_BINARY_DIR}/got" DELUGE_REPO_ROOT="${REPO_ROOT}"`) — **not** `deluge_spec`, whose `tests/32bit_unit_tests/mocks/mock_display.cpp`/`mock_print.cpp` define symbols the real `deluge_app` also defines (ruling Q5); the header-only rig in `tests/spec/shell/support/` (`script_runner.h`, `fake_surfaces.h`) is reached through the `${REPO_ROOT}/tests` include dir. Then `create_specs_driver(deluge_scenes_spec ${CMAKE_CURRENT_LIST_DIR}/scenes_spec)`, plus the host BSP objects `deluge_loadcheck` uses (`sim/CMakeLists.txt:447-469`: `host_bsp.c host_platform.c host_audio.c host_link.c host_ws.c host_pcm.c host_midi.c host_script.c host_script_tables.c host_snapshot.c host_debug.cpp host_efatfs_passthrough.cpp` — **no `host_main.c`**, the driver has its own `main`), and `target_link_libraries(deluge_scenes_spec PRIVATE deluge_scenes deluge_app deluge_scheduler deluge_scenes_spec_support ${SIM_ALLOC_LINK})`.
- ctest row: `add_test(NAME scenes_spec COMMAND ${CMAKE_COMMAND} -E env DELUGE_HOST_DETERMINISTIC=1 DELUGE_SD_ROOT=${REPO_ROOT}/tests/fixtures/card $<TARGET_FILE:deluge_scenes_spec>)` with `set_tests_properties(scenes_spec PROPERTIES TIMEOUT 300)` — sim rows carry an explicit `TIMEOUT` (harness T4 fix round 2, `sim/CMakeLists.txt:586-595`). `create_specs_driver` also registers one row per spec file; give the whole set the same `TIMEOUT`.

- [ ] **Step 1: Failing spec** — `sim/scenes_spec/l10n_bridge_spec.cpp`: `text_lookup(uint16_t(l10n::String::STRING_FOR_LPF)) == l10n::getView(l10n::String::STRING_FOR_LPF)`; `error_lookup(int16_t(Error::SD_CARD_FULL))` equals `deluge::hid::display::getErrorMessage(Error::SD_CARD_FULL)` (call the legacy function in the spec — the *spec* may include `hid/display/display.h`); every `Error` value 0..32 yields a non-empty string. `sim/scenes_spec/profile_spec.cpp`: `scenes::deluge_profile().text == &scenes::text_lookup`, `.error_text == &scenes::error_lookup`, and **every other field equals `ui::shell::deluge_profile()`'s** — `button_by_matrix`, `led_by_button`, `encoder_by_index`, `encoder_kind`, `oled_role_by_index`, `style_by_index`, `button_cols`, `button_rows`, `sidebar_cols` compared one by one (spans by `.data()` and `.size()`), which is the drift guard the two-profile ruling costs; plus `TextScratch s; profile.text_or_digits(uint16_t(l10n::String::STRING_FOR_LPF), s) == text_lookup(...)`.
- [ ] **Step 2: Configure and run** — `cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON && cmake --build build-sim --target deluge_scenes_spec` → fails (no library).
- [ ] **Step 3: Implement** the bridge (transcribe the `getErrorMessage` switch; keep the same `STRING_FOR_ERROR_*` ids), `scenes::profile.cpp` (a function-local `static const BoardProfile` copy-initialised from `ui::shell::deluge_profile()` with `.text`/`.error_text` overwritten), `cmake/deluge_specs.cmake`, and the three CMakeLists edits.
- [ ] **Step 4: Run** — `ctest --test-dir build-sim -R scenes` PASS; `ctest --test-dir build --output-on-failure` (the `tests/` project) still green after the cmake refactor; `./dbt build Debug` compiles (the `deluge_scenes` archive is produced); `cmake --build build-sim --target deluge_host` still links (`deluge_scenes` is deliberately not in its closure); `python3 scripts/check_layer_rules.py` green.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): deluge_scenes skeleton, l10n bridge, scenes profile, model-backed spec executable in sim/"`

---

### Task 2: Menu core types, builders and the node-id registry (pure)

**Files:**
- Create: `src/scenes/menu/node.h`, `src/scenes/menu/node_id.{h,cpp}`, `tests/spec/scenes/node_spec.cpp`, `tests/spec/scenes/node_id_spec.cpp`
- Modify: `src/scenes/CMakeLists.txt` (add `menu/node_id.cpp`). **No `tests/spec/CMakeLists.txt` change**: `../../src` is already on `deluge_spec`'s public include path (`tests/spec/CMakeLists.txt:43`), so `#include "scenes/menu/node.h"` resolves as-is.

**Interfaces** (spec E §4, with the amendments below):
```cpp
#include "ui/oled/meters.h"   // ui::oled::Hint
#include "ui/oled/values.h"   // ui::oled::Range, ui::oled::ValueStyle

namespace deluge::scenes::menu {
enum class Kind : uint8_t { Submenu, Toggle, Enum, Int, Decimal, Bipolar, Range, MultiRange, Colour, SyncLevel, Action, Custom };
enum class Layout : uint8_t { Vertical, Horizontal };
using Hint = deluge::ui::oled::Hint;         // meters.h:41 — do NOT redeclare one here
using IntRange = deluge::ui::oled::Range;    // values.h:18 — { int32_t min, max, step = 1; }
struct ParamRef { deluge::modulation::params::Kind kind = params::Kind::NONE; uint8_t index = 255; constexpr bool valid() const { return kind != params::Kind::NONE; } };
struct MenuContext;                                 // context.h
using GetFn = int32_t (*)(const MenuContext&); using SetFn = void (*)(const MenuContext&, int32_t); using RangeFn = IntRange (*)(const MenuContext&);
using RelevantFn = bool (*)(const MenuContext&); using DynamicNamesFn = size_t (*)(const MenuContext&, std::span<std::string_view> out);
struct Node; using DynamicChildrenFn = size_t (*)(const MenuContext&, std::span<const Node*> out);
struct Binding { ParamRef param{}; GetFn get = nullptr; SetFn set = nullptr; RangeFn range = nullptr; std::span<const uint16_t> names{};
	DynamicNamesFn dynamic_names = nullptr; deluge::ui::oled::ValueStyle style{}; uint8_t decimal_places = 0; };
	// names are l10n string ids (uint16_t) so node.h stays free of gui/l10n;
	// `style` is ui/oled/values.h:23 `{std::string_view unit; bool off_at_zero;}` — the legacy
	// "OFF at zero" and unit-suffix items come from the widget, not from per-item classes
struct MenuState; struct CustomItem { virtual ~CustomItem() = default;
	virtual void render(deluge::ui::oled::OledFrame&, const MenuContext&, MenuState&) const = 0;
	virtual deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent&, const deluge::ui::shell::UiContext&, const MenuContext&, MenuState&) const = 0; };
struct Node { uint16_t name, title; Kind kind = Kind::Submenu; Hint hint = Hint::None; Layout layout = Layout::Vertical; Binding bind{};
	std::span<const Node* const> children{}; RelevantFn relevant = nullptr; DynamicChildrenFn dynamic_children = nullptr; const CustomItem* custom = nullptr; uint8_t slots = 1; };
// builders (constexpr; title defaults to name)
constexpr Node param(uint16_t name, ParamRef p, Hint h = Hint::Knob, RelevantFn rel = nullptr, uint16_t title = 0);
constexpr Node int_item(uint16_t name, GetFn get, SetFn set, IntRange r, Hint h = Hint::None, RelevantFn rel = nullptr);
constexpr Node decimal_item(uint16_t name, GetFn, SetFn, IntRange, uint8_t places, RelevantFn = nullptr);
constexpr Node toggle_item(uint16_t name, GetFn, SetFn, RelevantFn = nullptr);
constexpr Node enumeration(uint16_t name, std::span<const uint16_t> names, GetFn, SetFn, RelevantFn = nullptr);
constexpr Node dynamic_enumeration(uint16_t name, DynamicNamesFn, GetFn, SetFn, RelevantFn = nullptr);
constexpr Node submenu(uint16_t name, std::span<const Node* const> children, Layout = Layout::Vertical, RelevantFn = nullptr, uint16_t title = 0);
constexpr Node dynamic_submenu(uint16_t name, DynamicChildrenFn, RelevantFn = nullptr);
constexpr Node action(uint16_t name, SetFn run, RelevantFn = nullptr);
constexpr Node custom(uint16_t name, const CustomItem*, RelevantFn = nullptr);
inline constexpr size_t kMaxDynamicChildren = 64, kMaxDynamicNames = 64, kMaxNodes = 1024;
}
```
```cpp
// src/scenes/menu/node_id.h — ruling Q7: SceneId::arg is a stable node id
namespace deluge::scenes::menu {
/// Walk `roots` depth-first in declaration order and give every reachable node an id, 1..n
/// (0 is reserved for "no node"). Idempotent: a second call over the same roots re-derives the
/// same ids. `validate_tree` (Task 6) calls this before its checks; `deluge_menu_dump` (Task 10)
/// emits the ids; `MenuScene::on_enter` resolves `SceneId::arg` through `node_by_id`.
uint16_t assign_node_ids(std::span<const Node* const> roots);
/// The id `assign_node_ids` gave `n`, or 0 if it was never assigned one.
uint16_t node_id(const Node& n);
/// The node with id `id`, or nullptr for 0 / an unassigned id.
const Node* node_by_id(uint16_t id);
}
```
Amendments vs spec E §4 (recorded in Task 12): string ids are `uint16_t` (not `l10n::String`) so `node.h` is model-free and host-testable in `tests/spec`; `Hint` and `IntRange` are **aliases** of the shipped `ui::oled::Hint` / `ui::oled::Range` rather than new types (`meters.h:41`, `values.h:18`) — the fourteen-enumerator `Hint` maps 1:1 onto legacy's twelve `Number::RenderingStyle` values (`number.h:25-38`: `NUMBER→None, KNOB→Knob, BAR→Bar, PERCENT→Percent, SLIDER→Slider, LENGTH_SLIDER→LengthSlider, PAN→Pan, LPF→Lpf, HPF→Hpf, ATTACK→Attack, RELEASE→Release, SIDECHAIN_DUCKING→Ducking`), with `Enum`/`Toggle` being toolkit-only slot styles `columns` uses to pick a slot renderer; `Binding` carries a `ui::oled::ValueStyle`; the node-id registry is new (ruling Q7); `Bipolar`/`Range`/`MultiRange`/`Colour`/`SyncLevel` kinds are declared now but their interpreters land in E2 except `SyncLevel` and `Bipolar` (used ≥3 times in E1's tables — verify by counting in Task 8; drop any kind that ends below 3).

- [ ] **Step 1: Failing spec** — `tests/spec/scenes/node_spec.cpp`: `constexpr Node a = param(1, {params::Kind::PATCHED, 5}, Hint::Lpf);` `static_assert(a.kind == Kind::Int && a.bind.param.index == 5 && a.title == 1 && a.hint == deluge::ui::oled::Hint::Lpf)`; `constexpr const Node* kids[] = {&a}; constexpr Node s = submenu(2, kids, Layout::Horizontal);` `static_assert(s.children.size() == 1 && s.layout == Layout::Horizontal)`; a runtime `it` checking `toggle_item`/`enumeration` builders fill `bind.get/set/names`; `static_assert(std::is_same_v<IntRange, deluge::ui::oled::Range>)`. `tests/spec/scenes/node_id_spec.cpp`: over a three-node fake tree, `assign_node_ids` returns 3, ids are 1..3 in depth-first declaration order, `node_by_id(node_id(x)) == &x` for each, `node_id` of an unassigned node is 0, `node_by_id(0) == nullptr`, and a second `assign_node_ids` call yields identical ids.
- [ ] **Step 2: Run to see it fail** — `cmake --build build --target all_specs && ctest --test-dir build -R "node_spec|node_id_spec"`.
- [ ] **Step 3: Implement** `node.h` and `node_id.{h,cpp}` (a `static const Node* g_by_id[kMaxNodes]` plus a linear-probe lookup keyed on the pointer; no heap).
- [ ] **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): declarative menu Node, Binding, ParamRef, constexpr builders and the stable node-id registry"`

---

### Task 3: `MenuContext` and `context_for()` (port of `SoundEditor::setup` context resolution)

**Files:**
- Create: `src/scenes/menu/context.{h,cpp}`, `sim/scenes_spec/context_spec.cpp`
- Ports: `SoundEditor::setup` `src/deluge/gui/ui/sound_editor.cpp:1618` (only the resolution of `newSound/newParamManager/newArpSettings/newModControllable/currentSourceIndex/currentMultiRange/…` and the root-menu choice — not the UI side), `SoundEditor::getCurrentModelStack` `:1982`, `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam` (`sound_editor.h:144`).

**Interfaces:**
```cpp
struct MenuContext {                                       // spec E §4 MenuContext + the survey's current* fields it needs
	Song* song; Clip* clip; Output* output; Sound* sound; ModControllableAudio* mod; ParamManagerForTimeline* params;
	ArpeggiatorSettings* arp; MultiRange* range; int16_t range_index; int8_t source_index; Source* source; SampleControls* sample_controls;
	VoicePriority* priority; SideChain* sidechain; MIDICable* midi_cable; bool affect_entire; bool is_kit, is_drum, is_audio_clip, is_song_view, is_performance, is_kit_global_fx;
	enum class Root : uint8_t { Sound, Drum, MidiOrCv, MidiDrum, GateDrum, AudioClip, Performance, Song, KitGlobalFx, Settings, Count } root;
};
MenuContext context_for(Song&, Clip* clip_or_null, int8_t source_index, bool performance_view, bool session_or_arranger_root);   // no UI reads: the caller passes what SoundEditor read from currentUIMode/getRootUI()
ModelStackWithThreeMainThings* model_stack_for(const MenuContext&, void* memory);   // getCurrentModelStack port (bindings.cpp's only entry point to the stack)
enum class Permission : uint8_t { No, Yes, MustSelectRange };
Permission permission_for(const Node&, const MenuContext&);                          // spec E §4.1: a node with bind.param on a multi-range sound -> MustSelectRange (port of the 18 checkPermissionToBeginSession overrides' common rule; the bespoke ones move to E2 customs)
```

- [ ] **Step 1: Failing spec** (real model on host): build a `Song` with one synth clip (use the model's own constructors/`createNewInstrument…` helpers — `sim/scenes_spec/support/song_fixture.h`, a new header this task adds, modelled on how `src/bsp/host/host_loadcheck_main.cpp` brings the app up; cite whichever path is used): `context_for(song, clip, 0, false, false)` yields `root == Root::Sound`, `sound != nullptr`, `params == &clip->paramManager`, `affect_entire == false`; a kit clip with `affectEntire` → `root == Root::Sound` with `is_kit && affect_entire`; a kit clip with a selected drum → `root == Root::Drum`; `clip == nullptr` and not performance → `root == Root::Settings`; `performance_view` → `Root::Performance`; a MIDI instrument clip → `Root::MidiOrCv`; `model_stack_for` returns a stack whose `modControllable == ctx.mod && paramManager == ctx.params`; `permission_for(param node, ctx with a sound having 2 ranges)` → `Permission::MustSelectRange`.
- [ ] **Step 2: Run to see it fail** — `cmake --build build-sim --target deluge_scenes_spec && ctest --test-dir build-sim -R context_spec`.
- [ ] **Step 3: Implement** by transcribing the resolution branches in `setup()` order, citing each branch's line.
- [ ] **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): MenuContext resolution and model-stack access ported from SoundEditor::setup"`

---

### Task 4: Bindings — read/write through `ModelStack`, escape hatch, ranges, notify

**Files:**
- Create: `src/scenes/menu/bindings.{h,cpp}`, `sim/scenes_spec/bindings_spec.cpp`
- Ports: `patched_param::Integer::readCurrentValue/writeCurrentValue` (`gui/menu_item/patched_param/integer.cpp:33,43`), `UnpatchedParam::readCurrentValue/getModelStack/writeCurrentValue` (`gui/menu_item/unpatched_param.cpp:36,41,46`), `computeCurrentValueForStandardMenuItem` and `computeFinalValueForStandardMenuItem` (`gui/menu_item/value_scaling.cpp:6`, declared `value_scaling.h:76`), `Integer::getMaxValue/getMinValue` defaults for patched/unpatched params (`patched_param/integer.h:35-36`, `unpatched_param.h:42-43`), the AFFECT_ENTIRE kit loop over `kit->firstDrum` (`unpatched_param.cpp:62`'s `getModelStackFromSoundDrum` branch and its patched sibling), the MIDI-follow feedback call (`view.sendMidiFollowFeedback` — exposed as `bindings::set_feedback_hook(fn)`; scenes may not include `gui/views`), the automation-view refresh (`automationView.possiblyRefreshAutomationEditorGrid` → becomes `notify::raise(notify::Dirty::ParamAutomation)`), `Decimal` scaling (`gui/menu_item/decimal.cpp`), `ParamDescriptor` construction for learn (`getLearningThing`, `UNPATCHED_START`).

**Interfaces:**
```cpp
namespace deluge::scenes::menu::bindings {
int32_t read(const Node&, const MenuContext&);                          // ParamRef via the param set's getValue + computeCurrentValueForStandardMenuItem; else bind.get
void write(const Node&, const MenuContext&, int32_t value);             // ParamRef via model_stack_for(...)->get{Patched,Unpatched}AutoParamFromId(idx)->autoParam->setCurrentValueInResponseToUserInput(computeFinalValueForStandardMenuItem(value), ms); kit affect-entire loop; feedback hook; notify::raise(Dirty::ParamAutomation | Dirty::ModTarget | Dirty::Oled per kind); else bind.set + notify::raise(Dirty::Oled)
IntRange range(const Node&, const MenuContext&);                        // bind.range ? bind.range(ctx) : the legacy default per kind (0..50 for standard params — patched_param/integer.h:35-36; -25..25 for pan; enum 0..names-1)
size_t names(const Node&, const MenuContext&, std::span<std::string_view> out);   // static (profile.text_or_digits per id) or dynamic; at most kMaxDynamicNames
ParamDescriptor learn_target(const Node&, const MenuContext&);          // for MidiLearn (E3)
using FeedbackHook = void (*)(const MenuContext&, const Node&, int32_t knob_pos); void set_feedback_hook(FeedbackHook);
}
```
`names()` resolves each id through `scenes::deluge_profile().text_or_digits(id, scratch)` with a caller-supplied `ui::shell::TextScratch` array, **never** through `text_lookup` directly (`profile.h:156`): that is the one sanctioned path and it already implements the digits fallback. Signature note: the scratch lives in the caller's frame, so `names()` takes `std::span<ui::shell::TextScratch> scratch` alongside `out` and the two spans must be the same length.

- [ ] **Step 1: Failing spec** (real model): for a synth clip, `write(lpf_freq_node, ctx, 30)` then `read(...) == 30` and the param set's raw value equals what `patched_param::Integer::writeCurrentValue` would have stored (compute via `computeFinalValueForStandardMenuItem` called directly in the spec); an unpatched global param round-trips; a `toggle_item` with get/set round-trips through a `bool` in the spec; `range(pan_node) == IntRange{-25, 25, 1}`; `names(enum_node, …)` returns the l10n strings; writing raises `notify::Dirty::ParamAutomation` (drain `notify::dirty()` and check `has()`); the kit affect-entire write reaches every drum (`kit->firstDrum` chain: two drums, both updated).
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement**, citing each ported line.  - [ ] **Step 4: Run** — `ctest --test-dir build-sim -R bindings_spec` PASS; `python3 scripts/check_layer_rules.py` green (`bindings.cpp` is one of the two files excluded from the `scenes-model-stack` rule — Task 12).
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): menu bindings through ModelStack/AutoParam with the legacy scaling, ranges and notify raises"`

---

### Task 5: `MenuScene` — the interpreter (render + overlay + after_render + handle + permissions + blink)

**Files:**
- Create: `src/scenes/menu/menu_scene.{h,cpp}`, `src/scenes/registry.{h,cpp}`, `sim/scenes_spec/menu_scene_spec.cpp`, pixel goldens `tests/golden/ui/menu/*.pbm`
- Ports: `MenuItem::drawItemsForOled` (`menu_item.cpp:47`, already pinned by `list`), `Submenu::drawSubmenuItemsForOled`/`focusChild` (`submenu.cpp:99,21`), `HorizontalMenu::renderOLED/renderMenuItems/switchVisiblePage/selectMenuItem/preparePaging` (`horizontal_menu.cpp:109,203,345,444,477`, already pinned by `columns`/`paging_of`/`title_paged`), `SoundEditor::selectEncoderAction :1034`, `buttonAction :333` (back → `goUpOneLevel :672`, select → `selectButtonPress` semantics: nullptr = up, `NO_NAVIGATION` = stay), `horizontalEncoderAction :1008`, `padAction :1405` (+ `potentialShortcutPadAction :1111`), `enterSubmenu :324`, `setupShortcutBlink :959` / `blinkShortcut :974-1000`, `getGreyoutColsAndRows :214`, `getNotificationValue` (Decimal/Enumeration/Integer short formatters), `MultiRange` redirect (`menuItemHeadingTo`) → the `Permission::MustSelectRange` rule.

**Interfaces:**
```cpp
namespace deluge::scenes::menu {

/// Live interpreter state. NOT what `save_state` writes (see MenuSavedState) — ruling Q9.
struct MenuState {
	const Node* path[8] = {};          ///< menu depth, unrelated to the shell's kStackDepth = 8 (scene.h:48)
	uint8_t depth = 0;
	deluge::ui::ListState list[8]{};   ///< one per menu depth; `list[depth].editing` IS the editing flag (state.h:15, a reserved bool no widget reads)
	deluge::ui::ColumnsState columns{};
	deluge::ui::ScrollText scroll{};
	// --- typed per-kind edit storage (ruling Q6): the widgets do not share one int32 ---
	int32_t edit_int = 0;                     ///< Int, Decimal: int_value/decimal_value take `int32_t&`
	bool edit_bool = false;                   ///< Toggle: toggle() takes `bool&` (values.h:169)
	int32_t enum_index = 0;                   ///< Enum: enum_value() takes `int32_t& index` …
	deluge::ui::ListState enum_list{};        ///< … plus its own ListState …
	deluge::ui::ScrollText enum_scroll{};     ///< … plus its own ScrollText (values.h:143)
	int8_t decimal_pos = 0;                   ///< decimal_value's in/out cursor position
	deluge::ui::Blink cursor_blink{};         ///< decimal_value's OLED cursor ONLY (state.h:38; `until_ms` is a phase anchor, not a deadline)
	deluge::ui::Blink shortcut_blink{};       ///< render_grid: the focused param pad, 180 ms arm (sound_editor.cpp:985)
	deluge::ui::Blink source_blink{};         ///< render_grid: the patching-source pads, 20 ms arm (sound_editor.cpp:996) — two cadences, two Blinks
	// --- what this tick's render observed, for after_render to act on ---
	deluge::ui::Response render_resp{};
	bool value_changed = false;
	bool back_claimed = false;
	bool range_picker_pending = false;
	const Node* pending_after_range = nullptr;
};

/// What survives a `replace_root` — ruling Q9. Child indices, not pointers, so the block stays
/// small and a table edit cannot resurrect a stale pointer.
struct MenuSavedState {
	uint16_t root_node_id = 0;      ///< node_id() of path[0]
	uint8_t path_child[8] = {};     ///< the child index taken at each level
	uint8_t depth = 0;
	int16_t focus = 0;              ///< list[depth].cursor / columns.focused at save time
};
static_assert(sizeof(MenuSavedState) <= 32);
static_assert(sizeof(MenuSavedState) <= deluge::ui::shell::kSceneStateBytes);   // scene.h:50 — 256

class MenuScene final : public deluge::ui::shell::Scene {
public:
	void open(const Node& root, const MenuContext&);

	deluge::ui::shell::SceneKind kind() const override;                     // SceneKind::FirstApp (== 16, scene.h:39)
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	void leds(deluge::ui::LedFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event, const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_resume() override;
	void on_model_changed(const deluge::notify::DirtySet& dirty) override;
	bool is_grid_overlay() const override { return true; }                  // ruling Q1
	bool occludes_grid() const override { return false; }                   // the base view keeps painting under the menu
	bool dims_grid() const override { return false; }                       // legacy greys via getGreyoutColsAndRows, it does not halve every pad
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }   // a menu covers the Main panel
	// input_transparent() is left at the default false: MenuScene IS the input_top() (scene.h:322).
	deluge::ui::grid::GreyoutMask greyout_mask() const override;            // port of SoundEditor::getGreyoutColsAndRows (sound_editor.cpp:214)
	void save_state(deluge::ui::shell::SceneState& state) const override;   // memcpy a MenuSavedState in
	void restore_state(const deluge::ui::shell::SceneState& state) override;
	std::optional<uint32_t> next_timer() const override;                    // the shorter of the two blink half-periods; no clock of its own

	const Node* focused() const;
	ParamDescriptor learn_target() const;
	static std::span<const Node* const> roots();                            // indexed by MenuContext::Root; Count entries
private:
	MenuState st_;
	MenuContext ctx_;
	// helpers: visible_children(node, out span, ctx) applying `relevant` + `dynamic_children`;
	// render_vertical(f) -> list; render_horizontal(f) -> columns with per-Kind column renderers;
	// and one leaf helper PER KIND (ruling Q6): render_leaf_int, render_leaf_decimal,
	// render_leaf_enum, render_leaf_toggle — each owning the widget's own argument shapes.
};

// Consumed here, defined in Task 9 (the tables need Tasks 6-8's nodes, so it cannot come earlier):
//   const Node* deluge::scenes::menu::shortcut_node(MenuContext::Root, bool shift, uint8_t x, uint8_t y);
// Task 5 links against a two-cell stub table so its own spec can exercise the jump; Task 9
// replaces the stub with the seven real tables and re-asserts the same behaviour.

/// The application's scene table: a `static Scene* const kByKind[ui::shell::kMaxSceneKinds]` this
/// file owns, with `kByKind[SceneKind::FirstApp] = &menu_scene()` and every other slot null.
/// The SHELL installs its own Popup/Console/Working/Boot/Loading instances over whatever the
/// registry reported (shell.h:120-126,330-342), so this table declares application scenes only.
deluge::ui::shell::SceneRegistry scene_registry();
MenuScene& menu_scene();
}
```

**Behaviour** (each cited):

*Surfaces and roles.* `render_oled` is called once per tick with `role == SurfaceRole::Main` on this board and is a no-op for any other role (spec E §3 rule 5; the board's `oled_role_by_index` has one entry, `profiles/deluge.h`). The 48/64 golden geometries come from `BoardProfile::style_by_index` (`profile.h:130`; `kDelugeStyle48`/`kDelugeStyle64`, `ui/core/style.h:47,60`), **not** from a switch inside the scene — the spec records both by building the `FakeSurfaceSet` from a profile whose `style_by_index` points at each. `render_grid` paints the blinking shortcut and patching-source pads over whatever the base view drew and **reads no input at all**, which is what keeps the "at most one render pass per tick reads a given input" rule true for a two-surface scene.

*Focus.* Frames carry a `FrameContext{const InputSnapshot& input; RepaintGate& gate; InputClaims claims; uint32_t tick, tick_ms; begin_tick(now)}` (`frame_context.h:212`) and an `InputMask{turn, edit, horizontal, vertical, select, back}` with `all()`/`none()`/`operator&` (`:74-88`). `SceneStack::input_top()` — the topmost scene that is not `input_transparent()` — is the only scene whose frames are built with `InputMask::all()`; every other layer, above **or below**, gets `InputMask::none()` and therefore paints normally while reading 0/false and claiming nothing (`scene.h:322`). So while `MenuScene` is on the stack, the base grid view under it still renders its pads but reads no input, which is why `MenuScene` being a non-input-transparent grid overlay is the correct shape rather than a competing reader.

*Rendering and input.* `render_oled` does the drawing **and** the input consumption together, as every toolkit widget does (spec B §4/§5), but never mutates the model — that stays `after_render`'s job. It renders `list(f, menu_rect(f), st_.list[st_.depth], rows, st_.scroll)` (`list.h:123`; `menu_rect(f)` is the legacy-reproducing rect, `list.h:33` — never a hand-computed one) for a `Layout::Vertical` node's children, materialising the `ListRow{label, value, icon, selectable}` span on the stack each tick from `visible_children`, or `columns(f, f.bounds(), st_.columns, cols, title)` (`columns.h:139`) for `Layout::Horizontal`, where `Node::slots → Column::slots`, `Node::relevant(ctx) → Column::relevant`, `Kind::Submenu → Column::is_submenu`, and the per-kind slot renderer is `meter_column` (with `MeterColumn{hint, norm, raw, raw_max}` — **`Hint::Ducking` must set `raw_max`**, `meters.h:212`), `text_column`, `submenu_column` or `toggle_column` (`columns.h:177-230`). **Reading is claiming** (`frame_context.h` rule 2, `:137-181`): `f.turn()`, `f.edit(0)`, `f.select_pressed()` each record the claim in the same call, so there is no separate `claim_*()` step to write — the explicit `claim_*()` methods are only for owning an input the scene does not read.

*Leaf editing (ruling Q6).* One helper per kind, each owning the widget's own argument shapes rather than sharing one `edit_value`:
- `Kind::Int` → `st_.edit_int = bindings::read(node, ctx_)` once on entering the leaf, then `int_value(f, menu_rect(f), st_.edit_int, bindings::range(node, ctx_), node.bind.style, /*focused=*/true)` (`values.h:65`).
- `Kind::Decimal` → `decimal_value(f, f.bounds(), st_.edit_int, range, node.bind.decimal_places, st_.decimal_pos, st_.cursor_blink, /*focused=*/true)` (`values.h:115`) — **`f.bounds()`, not `menu_rect(f)`**: the widget draws at a canvas-absolute y and only honours the rect horizontally, and the golden was recorded that way.
- `Kind::Enum` → `enum_value(f, menu_rect(f), st_.enum_index, names_span, st_.enum_list, st_.enum_scroll, /*wrap=*/false)` (`values.h:143`) — it takes a `ListState` **and** a `ScrollText` and has **no `focused` parameter**; it owns turn and select itself.
- `Kind::Toggle` → `st_.edit_bool` and `toggle(f, menu_rect(f), st_.edit_bool, off_label, on_label, st_.scroll)` (`values.h:169`) — a `bool&`, no `focused`.
Each stashes its `Response` into `st_.render_resp` and its `changed` into `st_.value_changed`. Mutating `st_.edit_int`/`edit_bool`/`enum_index` is exactly the shape `list`/`columns` already have with their own `ListState`/`ColumnsState`: scene-owned locals, never the model.

*Back (ruling Q8).* **The depth test precedes the read.** At `st_.depth > 0`, `render_oled` calls `f.back_pressed()` and records the result in `st_.back_claimed` — that single call both reads and claims. At `st_.depth == 0` it **never calls `back_pressed()` at all**, so `back` stays unclaimed and `Router::route_post_render` delivers `Button{Back}` to `handle()` (`router.h:83`). Writing the read first and then deciding whether to claim is impossible and must not be attempted.

*`after_render()` is where the model is touched*, keeping spec E §3 rule 1 intact by reading it as "`render_oled`/`render_grid` never mutate the model; `handle`/`after_render` do" now that navigation and value commits have two homes: `st_.value_changed` → `bindings::write(node, ctx_, st_.edit_int | st_.edit_bool | st_.enum_index)` and the `notify::` raises that go with it; `render_resp.entered` on a `Submenu` row → `path[++depth]`; `entered` on a leaf → enter editing (or, for `Kind::Toggle`, treat `entered` as "up" **without flipping the value** — `values.h:169`'s doc states a select press reports `entered` and leaves `changed` false); `st_.back_claimed` at `depth > 0` → `path[--depth]` in place; **nothing** at `depth == 0`, because a root's back was never claimed. `after_render()` returns `Result{}` for all in-tree navigation and value commits (`path`/`depth`/the model write stay inside the scene) and asks for a repaint with `Action::Kind::RepaintAll` when a commit changed something off-surface — never `Action{Repaint, SurfaceRole::None}`, which is a documented no-op (`scene.h:165` table). It also raises the horizontal-menu "notification value" popup on column change via `notify::notice_text(short_value)` (`notice_queue.h:72`) with the kind's formatter (`Decimal` at `bind.decimal_places`, `Enum` short option, `Int` digits) — never `notices().push`, which is `[[nodiscard]]` and drops silently (`notice_queue.h:46`).

*`handle()`* therefore no longer applies `turn`/`select`/`back`/the value write itself. It is left with exactly two paths, both of which reach it unaffected by the claims mechanism: **(a)** the shortcut-pad jump — a `UiEvent::Kind::Pad` is never masked (`frame_context.h` rule 4), gated on `ctx.shift` / `ctx.sticky` (`scene.h:116-140`) rather than any legacy `Buttons::` global, resolving `(x, y)` through Task 9's `shortcut_node(ctx_.root, ctx.shift, x, y)` and, on a hit, opening that node; **(b)** the depth-0 `Button{Back}` with `event.on == true`, which returns `Result{.handled = true, .actions = {{.kind = Action::Kind::Pop}}}` — `pop_to` left at `SceneKind::Null`, i.e. "just the top" — because at the root, back closes the editor (`sound_editor.cpp:333`'s `buttonAction` back case → `goUpOneLevel :672`, which exits when there is nothing above). Everything else returns `Result::unhandled()` (`scene.h:198`).

*Permissions.* `Permission::MustSelectRange` on entering a param node → the range picker. Decided in `after_render()` alongside the `Submenu` descend, since it is the same "what does `entered` mean for this node kind" question. In E1 the scene records `range_picker_pending`/`pending_after_range` and descends into a `Kind::MultiRange` placeholder node listing the ranges by index through `bindings::names` — enough for the golden; E2's `MultiRange` custom replaces it.

*Blink and LEDs (ruling Q1).* `leds()` declares **button LEDs only** — `LedFrame` (`ui/core/led_frame.h:36`) has `set(LedId, LedState)` and `indicator(IndicatorId, KnobIndicator)` and no pad concept whatever, and both are `[[nodiscard]]`. The pad blink is grid pixels, painted by `render_grid` from `st_.shortcut_blink` and `st_.source_blink`: legacy alternates two cadences on one counter (`sound_editor.cpp:974-1000` — the focused param pad flashed on the odd counter with a 180 ms re-arm, the patching-source pads at `x ∈ {14, 15}` on the even counter with a 20 ms re-arm, each masked by its own `frequency`), which one `Blink` cannot carry, hence two. `next_timer()` returns the nearer of the two next flips so the blink stays alive with no input, and `RepaintGate::request_repaint_after` is the alternative if the grid needs the tick without the scene tick.

*`on_enter` and `SceneId` (ruling Q7).* `SceneId{SceneKind::FirstApp, arg}` carries `arg = node_id(target)` (Task 2), so `Shell::state_lines()` spells the scene `app16/<id>` and two different menu nodes are distinguishable in a `.state` golden. `on_enter(id)` resolves `node_by_id(id.arg)` and re-targets: `arg` is deliberately **not** part of the `SceneState` cache key (`scene.h:56-59`), so re-entering `FirstApp` with a different `arg` restores the kind's last saved state and then `on_enter` retargets it — the restore-then-retarget order is the contract, and `on_enter` must overwrite `path`/`depth` from `arg` rather than trusting what `restore_state` put there. `on_enter` returns false (refusing the entry, rolling the push back) for `arg == 0` or an unknown id.

- [ ] **Step 1: Failing spec** (real model, `uitest::FakeSurfaceSet` + `Shell` over `scenes::deluge_profile()`): open the settings root → pixel goldens `menu_settings_root_48.pbm` / `_64`; `turn +1` moves the cursor (`st_.list[0].cursor == 1`); select on a submenu descends (`depth == 1`); back ascends; back at `depth == 0` yields a `Result` with one `Action::Kind::Pop`; open the sound root's LPF horizontal submenu → goldens `menu_lpf_columns_48.pbm` / `_64` (four columns FREQ/RES/MODE/MORPH with knob/bar/text per `Hint`); `edit(0) = +1` on the focused FREQ writes through `bindings` after `after_render()` (param value changed; `notify::Dirty::ParamAutomation` raised); a shortcut pad with shift jumps to that node; `permission_for == MustSelectRange` descends into the range list; `greyout_mask()` matches `SoundEditor::getGreyoutColsAndRows`'s cols/rows for the sound editor; `render_grid` paints the focused shortcut pad and nothing else; `is_grid_overlay() && !dims_grid() && !occludes_grid() && !oled_transparent(Main)`; **`f.ctx.claims.double_claim == false` after every tick** (`frame_context.h:48`); `save_state`/`restore_state` round-trip a depth-3 path.
- [ ] **Step 2: Run to see it fail.**
- [ ] **Step 3: Implement** with the toolkit widgets only — no direct `OledFrame` primitive calls from the scene (`frame.h:165-269`'s `pixel`/`hline`/`rect`/`glyphs`/…; the Task 12 ratchet counts them).
- [ ] **Step 4: Run** — PASS; goldens recorded after the state assertions pass; both geometries.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): MenuScene — one interpreter for vertical and horizontal menus over Node tables"`

---

### Task 6: Tables batch 1 — the six generated subtrees (filters, compressor, unison, oscillators, envelopes, LFOs); tree validator; retire `generate/`

**Files:**
- Create: `src/scenes/menu/nodes/sound_filters.cpp`, `sound_compressor.cpp`, `sound_unison.cpp`, `sound_osc.cpp`, `sound_env.cpp`, `sound_lfo.cpp`, `src/scenes/menu/relevance.{h,cpp}`, `src/scenes/menu/validate.{h,cpp}`, `tests/spec/scenes/validate_spec.cpp`, `sim/scenes_spec/tables_1_spec.cpp`
- Delete (at the end of the task): `src/deluge/gui/menu_item/generate/**`; remove `#include "gui/menu_item/generate/g_menus.inc"` at `gui/ui/menus.cpp:244` and re-declare the same globals it produced **as they were** (paste the generated `g_menus.inc` content into `menus.cpp` verbatim — the legacy tree must not change in this plan; only the generator is retired). Remove the `add_custom_target(deluge_menus …)` and its `add_custom_command` (`src/deluge/gui/menu_item/generate/CMakeLists.txt`) and the `add_subdirectory` that pulls them in. Note the generator's JSON (`${CMAKE_CURRENT_BINARY_DIR}/g_menus.json`) is a build artefact and was never committed; `g_menus.inc` **is** committed and is what gets pasted.
- Sources: `src/deluge/gui/menu_item/generate/dmenus/{filter/lpf.py, filter/hpf.py, compressor.py, unison.py, oscillator/*, envelope/*, lfo/*}` (the declarative specs — node names, titles, param ids, hints, relevance/mode conditions) and the classes they instantiate (`gui/menu_item/filter/*`, `audio_compressor/*`, `unison/*`, `osc/*`, `envelope/*`, `lfo/*`) for the get/set logic of non-`ParamRef` items (LPF mode enum, oscillator type, LFO shape/sync).

**Interfaces:**
```cpp
// relevance.h — one predicate per legacy isRelevant these subtrees use (cite each):
//   bool has_lpf(const MenuContext&); has_hpf; is_synth; osc_is_sample; osc_is_wavetable;
//   osc_not_fm; modulator_relevant; lfo_is_synced; … all RelevantFn-shaped.
// A registry for the dump (Task 10): `std::string_view name_of(RelevantFn)` over a
// `constexpr` table of {fn, name} pairs maintained next to the predicates.

// validate.h
struct Issue { enum class Kind : uint8_t { Unreachable, EmptySubmenu, BadParamRef, EnumWithoutNames, ShortcutToNothing, DuplicateName, TooManyCustom, MissingTitle, NullCustom } kind; const Node* node; uint16_t detail; };
/// Every rule of spec E §4.4. Calls `assign_node_ids(roots)` first (ruling Q7), so ids exist
/// before anything downstream needs them. `contexts` is one MenuContext per MenuContext::Root
/// built by the caller: tables_*_spec builds real ones, validate_spec uses fakes.
deluge::ui::shell::FixedVector<Issue, 64> validate_tree(std::span<const Node* const> roots, std::span<const MenuContext> contexts);

// nodes: inline constexpr Node lpf_freq, lpf_res, lpf_mode, lpf_morph, lpf (submenu, Horizontal,
// relevance::has_lpf), hpf…, compressor…, unison…, osc0…, osc1…, env1..env4, lfo1..lfo4;
// each file exposes `extern const std::span<const Node* const> k<Subtree>Children`.
```

- [ ] **Step 1: Failing specs** — `tests/spec/scenes/validate_spec.cpp` (pure): a fake tree with an empty submenu, a dangling shortcut cell, an enum without names, and 11 customs → those four `Issue::Kind`s and no others; the real batch-1 roots validate clean under a fake context that says "synth, has LPF"; `validate_tree` assigned ids to every reachable node (`node_id(x) != 0`). `sim/scenes_spec/tables_1_spec.cpp` (real model): for each of the six subtrees, the `Node` children list equals the legacy `HorizontalMenu`/`Submenu` item names in order (walk `lpfMenu`'s items from `gui/ui/menus.h` and compare `getName()` against the `Node`'s name resolved through `scenes::deluge_profile().text_or_digits`), the `ParamRef`s equal each legacy item's `getParamKind()/getParamIndex()`, and `Hint` equals `getRenderingStyle()` mapped 1:1 per Task 2's table; every enum's names equal the legacy `getOptions()` strings; `MenuScene`'s render of the LPF submenu matches the legacy renderer byte-for-byte (see Known risks).
- [ ] **Step 2: Run to see them fail.**
- [ ] **Step 3: Implement** the six node files by transcribing the Python specs (names/titles → `STRING_FOR_*` ids, `param(...)`, `enumeration(...)` with get/set ported from the legacy classes), `relevance.cpp`, `validate.cpp`; then retire `generate/` as described (the legacy tree byte-identical: `git diff` shows only the include line replaced by the `.inc`'s own text, and the level-3 boot script golden `tests/golden/ui/app/boot/` is unchanged).
- [ ] **Step 4: Run** — PASS; `cmake --build build-sim --target deluge_host` links; `./dbt build Debug` compiles; `ctest --test-dir build-sim -R "scenes|ui_app_scripts"` green.
- [ ] **Step 5: Commit** — two commits: `git commit -m "feat(scenes): menu tables for filters, compressor, unison, oscillators, envelopes, LFOs; tree validator"` then `git commit -m "chore(menus): retire the Python menu generator (subtrees now declared as Node tables)"`.

---

### Task 7: Tables batch 2 — the rest of the sound root (master, arp, randomizer, FX, sidechain, mixer, voice, bend, actions) excluding customs

**Files:**
- Create: `src/scenes/menu/nodes/sound_master.cpp`, `sound_arp.cpp`, `sound_randomizer.cpp`, `sound_fx.cpp`, `sound_sidechain.cpp`, `sound_mixer.cpp`, `sound_voice.cpp`, `sound_bend.cpp`, `sound_actions.cpp`, `sound_root.cpp`, `sim/scenes_spec/tables_2_spec.cpp`
- `sound_root.cpp` holds the `soundEditorRootMenu` literal, children in the legacy order and count — **28 children**, `menus.cpp:1521-1551`: `soundEditorRootActionsMenu, soundMasterMenu, arpMenu, randomizerMenu, audioCompMenu, soundFiltersMenu, soundFXMenu, sidechainMenu, source0Menu, source1Menu, modulator0Menu, modulator1Menu, oscMixerMenu, env1Menu, env2Menu, env3Menu, env4Menu, lfo1Menu, lfo2Menu, lfo3Menu, lfo4Menu, voiceMenu, bendMenu, drumBendRangeMenu, patchCablesMenu, sequenceDirectionMenu, outputMidiSubmenu` (the last three of which include the E2 customs below).
- Sources: `gui/ui/menus.cpp` (the literals for each submenu and their item classes under `gui/menu_item/{master_transpose,arpeggiator,fx,sidechain,mixer,voice,bend_range,…}`), `menu_item/patch_cables.*` and `multi_range.*` **excluded** (E2 customs): where the legacy submenu contains a custom item, the table carries `custom(name, nullptr)` with a `// E2` comment, and `validate_tree`'s `NullCustom` issue is downgraded to a warning only under `DELUGE_SCENES_E1_PARTIAL` (a CMake option, `ON` by default until E2 lands, documented in `src/scenes/README.md` and removed by E2).

- [ ] **Step 1: Failing spec** — same shape as Task 6's `tables_1_spec`: per submenu, names/order/`ParamRef`s/`Hint`s/enum options equal legacy; `validate_tree` clean under the `Root::Sound` context (with `DELUGE_SCENES_E1_PARTIAL` on); the sound root's child count is 28 and its order matches `menus.cpp:1521-1551` element for element; `MenuScene` renders the sound root list → pixel goldens `menu_sound_root_48.pbm` / `_64`, matching legacy `Submenu::renderOLED` byte-for-byte.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement** the ten files.  - [ ] **Step 4: Run** — `ctest --test-dir build-sim -R "tables_2_spec"` PASS; goldens recorded.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): sound-editor root tables (master, arp, randomizer, fx, sidechain, mixer, voice, bend, actions)"`

---

### Task 8: Tables batch 3 — settings root and the other roots (drum, MIDI/CV, MIDI drum, gate drum, audio clip, performance, song, kit global FX)

**Files:**
- Create: `src/scenes/menu/nodes/settings.cpp`, `src/scenes/menu/nodes/roots.cpp`, `sim/scenes_spec/tables_3_spec.cpp`
- Sources: `menus.cpp:1899` (`settingsRootMenu` and its subtrees — `FlashStorage`/`runtimeFeatureSettings` get/set items; cite each class under `gui/menu_item/{defaults,runtime_feature,midi,cv,gate,trigger,record,sample,flash,…}`), the other root literals (grep `PLACE_SDRAM_BSS Submenu` in `menus.cpp`).

- [ ] **Step 1: Failing spec** — as Task 7, per root; plus `MenuScene::roots()[size_t(MenuContext::Root::X)]` resolves each of the ten roots and `roots().size() == size_t(MenuContext::Root::Count)`; `validate_tree` clean across **all** contexts (one `MenuContext` per `Root`, built from a song with a synth clip, a kit clip with a drum, a MIDI clip, a CV clip, an audio clip, and the settings context); the `Kind`-count check that spec E §4 demands — every declared `Kind` is used by ≥3 nodes across all roots, or the spec fails and the kind is dropped (this is where `Bipolar` and `SyncLevel` are confirmed or removed); settings pixel goldens `menu_settings_*_48.pbm` / `_64`.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement.**  - [ ] **Step 4: Run** — PASS; goldens recorded.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): settings root and the drum/MIDI/CV/audio-clip/performance/song/kit-FX root tables"`

---

### Task 9: Shortcut tables in `scenes`; remove `BoardProfile::shortcuts` from the shell

**Files:**
- Create: `src/scenes/menu/shortcuts.{h,cpp}`, `tests/spec/scenes/shortcuts_spec.cpp` (pure: table shapes), `sim/scenes_spec/shortcuts_spec.cpp` (cross-check vs the legacy tables)
- Modify (ruling Q2, the shell edit): `src/shell/profile.h` — delete `struct ShortcutTable` (`:87`) and the `ShortcutTable shortcuts;` member (`:131`); `src/shell/profiles/deluge.cpp` — delete the `.shortcuts = {}` initialiser (`:142`) and trim the comment above it to the l10n half only; `src/shell/README.md` — delete the `ShortcutTable` row (`:24`), drop "shortcuts" from the `BoardProfile` row (`:27`) and from the `deluge_profile()` sentence (`:38`). Those four are the only references in the tree; nothing dereferences the field.
- Sources: `paramShortcutsForSounds` (`menus.cpp:1921`), `paramShortcutsForSoundsSecondLayer` (`:1941`), `paramShortcutsForAudioClips` (`:1960`), `paramShortcutsForSongView` (`:1979`), `paramShortcutsForKitGlobalFX` (`:1998`), `midiOrCVParamShortcuts` (`:949`), `gateDrumParamShortcuts` (`:960`) — cell by cell (`nullptr` → `nullptr`, `comingSoonMenu` → `nullptr` with a comment).

**Interfaces:**
```cpp
// src/scenes/menu/shortcuts.h — the seven legacy tables, keyed by (Root, shift). The shell no
// longer carries them: two `std::span<const uint16_t>` layers could not hold five 16x8 tables
// plus two 1-D ones selected by context (ruling Q2).
namespace deluge::scenes::menu {
/// Pad geometry comes from the board profile, not from a hard-coded 16/8: `cols` is
/// `board.pad_grid_width` and the sidebar is `profile.sidebar_cols` (profile.h:140).
inline constexpr uint8_t kShortcutCols = 16, kShortcutRows = 8;
/// One table: `cells[y * kShortcutCols + x]`, null where the legacy cell was null or comingSoon.
/// A 1-D legacy table (midiOrCV, gateDrum) is stored as a full grid with only column 0 filled,
/// exactly as `potentialShortcutPadAction` indexes it (sound_editor.cpp:1111).
struct ShortcutLayer { std::span<const Node* const> cells; };
/// The layer for a context: `shift` selects the second layer where the root has one
/// (Root::Sound only, `paramShortcutsForSoundsSecondLayer`), otherwise the same layer.
const ShortcutLayer& shortcut_layer(MenuContext::Root root, bool shift);
/// The node at a pad, or nullptr. Bounds-checked; `x`/`y` are absolute pad coordinates with the
/// sidebar included, as `UiEvent{Pad}` delivers them (scene.h:98-110).
const Node* shortcut_node(MenuContext::Root root, bool shift, uint8_t x, uint8_t y);
}
```

- [ ] **Step 1: Failing spec** — `tests/spec/scenes/shortcuts_spec.cpp`: every `shortcut_layer` has `cells.size() == kShortcutCols * kShortcutRows`; `shortcut_node` is null outside those bounds; `shortcut_layer(Root::Sound, true) != shortcut_layer(Root::Sound, false)` and every other root's two layers are the same object. `sim/scenes_spec/shortcuts_spec.cpp`: for every `(x, y)` in each of the seven legacy tables, the `Node`'s resolved name equals the legacy cell's `getName()` (or both are null); `validate_tree`'s `ShortcutToNothing` rule passes over every root; a `Shell` built with `scenes::deluge_profile()` compiles and runs with no `shortcuts` field (the removal's regression net); `MenuScene::handle` on `UiEvent{Pad, x, y, on}` with `ctx.shift` jumps to the same node `shortcut_node` reports.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement** the tables and make the four shell edits.  - [ ] **Step 4: Run** — `ctest --test-dir build-sim -R "shortcuts"` and the full `tests/` suite PASS (the shell's own specs must still build after the field is gone); `./dbt build Debug` compiles.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): shortcut-pad tables as Node pointers, owned by scenes; drop BoardProfile::shortcuts"`

---

### Task 10: `deluge_menu_dump` — JSON in the retired DocEmitter's schema; committed dump; CI diff

**Files:**
- Create: `src/scenes/menu/menu_dump_main.cpp`, `website/src/data/generated/menu-hierarchies.json` (new — spec E §4.3's generator does not exist: there is no `website/scripts/generate-menu-hierarchies.mjs` and no `website/src/data/generated/` directory; `website/src/content/docs/reference/menu_hierarchies.mdx` is hand-written and stays), `scripts/menu_dump_check.sh`
- Modify: `sim/CMakeLists.txt` (`deluge_menu_dump` executable: the same host BSP object list as `deluge_scenes_spec` minus `host_main.c`, plus `menu_dump_main.cpp`'s own `main`, linking `deluge_scenes deluge_app deluge_scheduler ${SIM_ALLOC_LINK}`; ctest row `menu_dump_matches_committed` running `scripts/menu_dump_check.sh` with a `TIMEOUT 300`), `.github/workflows/tests.yml` (a step after the existing `build-sim` X64 configure/build at `:66-68`).
- Output schema = the retired `DocEmitter`'s (`src/deluge/gui/menu_item/generate/dmui/visitor/doc_emitter.py`, read it before deleting it in Task 6 — copy the schema into `menu_dump_main.cpp`'s header comment): `{"type": "basic"|"submenu"|"multimode"|"multimode-mode"|"multicontext", "title", "name", "description", "children"?, "available_when"?}`. Map `Kind::Submenu` → `submenu`, leaves → `basic`, a node with `relevant` → `available_when` = `relevance::name_of(fn)` (Task 6's registry), `dynamic_children` → expanded under each provided context as `"multicontext"`. Add `"id"` = `node_id(n)` to every entry (ruling Q7) so the dump is the lookup table for `app16/<id>` in a `.state` golden. Walk every root under every context (the ten built in Task 8); sort keys; two-space indentation; trailing newline.

- [ ] **Step 1: Failing check** — write `scripts/menu_dump_check.sh` (`cmake --build build-sim --target deluge_menu_dump && ./build-sim/deluge_menu_dump > website/src/data/generated/menu-hierarchies.json && git diff --exit-code website/src/data/generated/menu-hierarchies.json`) and its ctest row first; run it → fails (no tool).
- [ ] **Step 2: Implement** the tool; run it to write the JSON for the first time; commit the JSON so the `git diff --exit-code` net is armed; add the CI step.
- [ ] **Step 3: Run** — `ctest --test-dir build-sim -R menu_dump` PASS; run the tool twice and confirm byte-identical output (determinism).
- [ ] **Step 4:** spot-check five entries against the hand-written `website/src/content/docs/reference/menu_hierarchies.mdx` and report discrepancies in the commit body; do not edit the mdx.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): deluge_menu_dump emits the menu tree as JSON; committed dump diffed in CI"`

---

### Task 11: Level-2 acceptance scripts per item kind

**Files:**
- Create: `tests/ui-scripts/shell/menu_int.txt`, `menu_decimal.txt`, `menu_enum.txt`, `menu_toggle.txt`, `menu_submenu_horizontal.txt`, `menu_shortcut.txt`, `menu_range_permission.txt`; goldens under `tests/golden/ui/shell/menu_int/`, `menu_decimal/`, … (the root `expect_script_goldens` pins: `fs::path(UI_GOLDEN_DIR) / name`, `script_runner.h:694`, `UI_GOLDEN_DIR = tests/golden/ui/shell`); `sim/scenes_spec/menu_scripts_spec.cpp`
- The spec calls `uitest::expect_script_goldens(self, "menu_int", scenes::scene_registry(), ui::shell::Services{}, opts)` (`script_runner.h:688`) with
  ```cpp
  uitest::RunOptions opts{
      .tick_ms = 10,                                             // script_runner.h:58 — the blink cadence is measured in these
      .board = &deluge::ui::shell::kDelugeBoard,
      .profile = &deluge::scenes::deluge_profile(),              // the l10n-bearing copy, Task 1
      .initial_scene = deluge::ui::shell::SceneId{deluge::ui::shell::SceneKind::FirstApp,
                                                 deluge::scenes::menu::node_id(target_node)},
  };
  ```
  `RunOptions::initial_scene` **already exists** (`script_runner.h:66`, plan G T3, spec G §10 §4.1): unset → `Shell::boot("test")`, set → `Shell::init(*initial_scene)`. There is nothing to add to the harness and no amendment to record. The song fixture is built in-process before `expect_script_goldens`, by the same `sim/scenes_spec/support/song_fixture.h` Task 3 added.
- Each script (format `<at_ms> <command> …`, `#` comments, one step per line, non-decreasing times — `tests/harness/ui_script.h`, and `tests/ui-scripts/shell/input.txt` as the worked example): `snapshot` on entry, then `encoder Select +1`, `button SelectEnc down` / `up`, `encoder GoldUpper +2`, a `snapshot` after each, then `button Back down` / `up`, `snapshot`, `quit`. Encoder names are `Select, Horizontal, Vertical, GoldUpper, GoldLower, Tempo` (`script_runner.h:206`); button names come from `ui::shell::button_name()` (`profile.h:169`).
- **What the `.state` can and cannot say** (ruling Q7): the scene line reads `app16/<node id>` (`shell.cpp:885-898`), so the golden *does* pin which node is open, via the id the validator assigned and the dump publishes. It still cannot show the node's *label* or its value, so each script's spec also asserts `menu_scene().focused()->name` and the model value directly, in the same `it` block.

- [ ] **Step 1:** write the seven scripts and `menu_scripts_spec.cpp`; run once to record into `tests/golden/ui/shell/menu_*/`; inspect every `.pbm` snapshot (render each to ASCII with `python3 scripts/pbm2png.py` or the spec's own dump and paste into the commit body); run again → PASS.
- [ ] **Step 2:** determinism — run `ctest --test-dir build-sim -R menu_scripts_spec` twice and confirm the `got/` trees are byte-identical (`python3 scripts/ui_snapshot_diff.py`).
- [ ] **Step 3: Commit** — `git commit -m "test(scenes): level-2 menu scripts per item kind with goldens"`

---

### Task 12: Layer rules, README, spec amendments

**Files:**
- Modify: `scripts/check_layer_rules.py` + `scripts/layer_rules_baseline.txt`, `src/scenes/README.md`, and three specs.

New ratchet rows (the file ships with 11 rule entries in 12 rows today, `shell-no-clock` appearing twice; these five bring it to 15 entries in 17 rows):
```python
("scenes-includes-gui", "src/scenes/**", "include", r"^(gui/(?!l10n/)|hid/)", "2026-08-26-ui-scenes-menus-design.md"),
# The (include, *excludes) glob tuple already exists (`rule_covers`,
# check_layer_rules.py:154-164, as ui-widgets-read-input-directly uses at :97) — no checker
# change needed; do NOT add an `except` field.
("scenes-model-stack",
 ("src/scenes/**", "src/scenes/menu/bindings.cpp", "src/scenes/menu/context.cpp"),
 "include", r"^(model/model_stack\.h|modulation/automation/auto_param\.h)",
 "2026-08-26-ui-scenes-menus-design.md"),
("scenes-currentSong", "src/scenes/**", "symbol", r"\bcurrentSong\b", "2026-08-26-ui-scenes-menus-design.md"),
# Menus draw only through widgets: no direct OledFrame primitive calls from the scene layer.
# The real risk is `f.<primitive>`, not `canvas.` — OledFrame exposes pixel/hline/rect/glyphs
# directly (ui/oled/frame.h:165-269).
("scenes-no-frame-primitives", "src/scenes/menu/**", "symbol",
 r"\b(canvas|f)\.(pixel|hline|vline|line|rect|invert|clear|glyph)", "2026-08-26-ui-scenes-menus-design.md"),
# A THIRD shell-no-clock row: the checker matches one glob per row, which is why this rule is
# already two rows (src/shell/** at :119, src/ui/** at :128).
("shell-no-clock", "src/scenes/**", "symbol", r"\bdeluge_clock_|\bstd::chrono\b|\bclock\(\)|\btime\(",
 "2026-08-26-ui-test-harness-design.md"),
```
Baselines: 0 for every new row except `scenes-currentSong`, whose baseline is the counted number of `currentSong` uses in `context.cpp` after Task 3 (shrink-only, spec E §3 rule 4).

`src/scenes/README.md` covers: how to add a node, a `Kind`, a custom; the relevance registry and its `name_of` table; the validator and `DELUGE_SCENES_E1_PARTIAL`; the node-id registry and how `app16/<id>` reads in a `.state`; the dump and its CI diff; the two-profile arrangement (`scenes::deluge_profile()` vs `ui::shell::deluge_profile()`) and the drift spec that guards it; the E2+ roadmap.

Spec amendments (all three with `git add -f`, since `docs/superpowers/` is gitignored):
- **Spec E** (`2026-08-26-ui-scenes-menus-design.md`): §4.3 corrected (no website generator ever existed; `deluge_menu_dump` *creates* the JSON, and the `.mdx` stays hand-written until a later doc task); §4 amendments (uint16 string ids; `Hint`/`IntRange` are aliases of `ui::oled::Hint`/`ui::oled::Range`, not new types; `ValueStyle` on `Binding`; `Permission`; `MenuState`'s typed per-kind edit storage; `MenuSavedState`; the node-id registry; `roots()`); §4.1 amended for the `render_oled`/`render_grid`/`after_render`/`handle` split and the grid-overlay flags; §4.2 amended: the shortcut tables live in `scenes`, not in the board profile; §7 note that legacy-vs-new *script* acceptance runs in spec F via `RunOptions::second_registry`, E1 pinning the new renderer against the legacy renderer in-process instead (Tasks 6–8).
- **Spec D §13** (`2026-08-26-ui-shell-design.md`, "Amendments during implementation"): under **§2 Board profile and surfaces**, record that `ShortcutTable` and `BoardProfile::shortcuts` were **removed** in spec E1 Task 9 — the shipped two-`std::span<const uint16_t>`-layer shape could not express the seven legacy tables (five 16×8 plus two 1-D) selected by menu context, the shell never dereferenced the field, and putting the tables in `scenes::menu::shortcuts` keeps the profile board description rather than application data.
- **Spec G §10** (`2026-08-26-ui-test-harness-design.md`, "Amendments during implementation"): record that CppSpec's `FetchContent` and `create_specs_driver` moved out of `tests/spec/CMakeLists.txt` into `cmake/deluge_specs.cmake` so the `sim/` project can build a spec driver too, and that the sim's driver uses a **mock-free** support library rather than `deluge_spec` — whose `mock_display.cpp`/`mock_print.cpp` collide with the real `deluge_app` symbols the sim links.

- [ ] **Step 1:** add the five rows; prove each with a deliberate probe (a temporary `#include "gui/ui/ui.h"` in a scenes file, a `currentSong` read in `menu_scene.cpp`, an `f.pixel(...)` call, a `std::chrono` use, a `model/model_stack.h` include outside the two excluded files) — `python3 scripts/check_layer_rules.py` exits 1 each time — then remove the probes.
- [ ] **Step 2:** `python3 scripts/check_layer_rules.py --update` and inspect the baseline diff (only `scenes-currentSong` rows should appear).
- [ ] **Step 3:** write `src/scenes/README.md` and the three spec amendments.
- [ ] **Step 4:** full suites — `ctest --test-dir build --output-on-failure` and `ctest --test-dir build-sim --output-on-failure`; `./dbt build Debug`; `cmake --build build-sim --target deluge_host`.
- [ ] **Step 5: Commit** — `git add scripts src/scenes/README.md && git add -f docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md docs/superpowers/specs/2026-08-26-ui-shell-design.md docs/superpowers/specs/2026-08-26-ui-test-harness-design.md && git commit -m "test(scenes): layer rules for the scenes layer; README; spec E/D/G amendments"`

---

## Self-review notes

- **Spec coverage (E1 scope).** §2 inventory: only `MenuScene` is built here (E2+ for the rest — stated in the scope note). §3 composition rules 1–5: rule 1 (`render` never mutates; `handle` writes and raises) → Tasks 4–5, amended to "`render_oled`/`render_grid` never mutate the *model*; `handle`/`after_render` do" now that `after_render()` exists (`scene.h:245`) — `render_oled` mutates only `MenuState`'s own `edit_int`/`edit_bool`/`enum_index`/`ListState`/`ColumnsState` locals, the same shape `list`/`columns` already have; rule 2 (state struct) → `MenuState` (Task 5); rule 3 (views model-aware, components not) → `MenuScene` maps `Binding` → widget values, widgets never see a `Clip*`; rule 4 (`currentSong` confined + counted) → Task 12's `scenes-currentSong` row; rule 5 (roles) → `render_oled` is a no-op for any role but `Main`, and the 48/64 goldens come from `BoardProfile::style_by_index`. §4 tree/builders → Task 2; `MenuContext` → Task 3; `Binding` → Task 4; `CustomItem` interface → Task 2 (implementations E2). §4.1 `MenuScene` (render/after_render/handle/leds/grid overlay/permissions/learn target/notification value) → Task 5. §4.2 shortcuts → Task 9 (relocated from the profile per ruling Q2). §4.3 dump + DSL retirement → Tasks 10 and 6. §4.4 validation → Task 6, exercised across all ten contexts in Task 8. §7 step 1 → the whole plan; the legacy-vs-new *script* acceptance stays spec F's, with `RunOptions::second_registry` named as its mechanism (Task 12's spec-E amendment). §8 testing → Tasks 5–8 (per-kind pixel goldens at both geometries) and 11 (level-2 scripts). §9 menu rows → Tasks 6–8. §10 open questions: Q1 (`ParamRef` vs a params-registry handle) carried unchanged; Q2 (`Range` in `Binding` vs a per-`ParamRef` table) carried, but `Range` is now `ui::oled::Range` so the eventual move is a `Binding` field deletion, not a type change; Q3 (`path` as pointers vs stable ids) is **answered both ways**: `MenuState::path` stays pointers for speed, `MenuSavedState::path_child` and `SceneId::arg` use ids for persistence (rulings Q7/Q9).
- **Placeholders:** none. Two deliberate, bounded interim mechanisms, both documented and both removed by E2: Task 7's `custom(name, nullptr)` behind `DELUGE_SCENES_E1_PARTIAL`, and Task 5's `Kind::MultiRange` placeholder node for the range picker. Every legacy citation resolves at `5c72cec9f` — verified: `menus.cpp:244, 949, 960, 1521, 1899, 1921, 1941, 1960, 1979, 1998`; `sound_editor.cpp:214, 324, 333, 672, 959, 974, 1008, 1034, 1111, 1405, 1618, 1982`; `sound_editor.h:144, 185`; `menu_item.cpp:47`; `submenu.cpp:21, 99`; `horizontal_menu.cpp:109, 203, 345, 444, 477`; `number.h:25-38`, `number.cpp:75`; `display.cpp:7`; `value_scaling.cpp:6` / `value_scaling.h:76` (the original plan's "`menu_item/param.cpp` or wherever — grep" is now a real path); `patched_param/integer.cpp:33, 43` and `integer.h:35-36`; `unpatched_param.cpp:36, 41, 46, 62` and `unpatched_param.h:42-43`; all six `generate/dmenus` subtrees (`filter/`, `compressor.py`, `unison.py`, `oscillator/`, `envelope/`, `lfo/`) and `dmui/visitor/doc_emitter.py`.
- **Type consistency:** `Node`/`Binding`/`ParamRef`/`Kind`/`Layout`/`Hint`(= `ui::oled::Hint`)/`IntRange`(= `ui::oled::Range`) and `assign_node_ids`/`node_id`/`node_by_id` are defined in Task 2 and used by Tasks 3–11; `MenuContext`/`MenuContext::Root`/`context_for`/`model_stack_for`/`Permission` are defined in Task 3 and used by Tasks 4–10; `bindings::read/write/range/names/learn_target/set_feedback_hook` are defined in Task 4 and used by Task 5; `MenuScene`/`MenuState`/`MenuSavedState`/`roots()`/`scene_registry()`/`menu_scene()` are defined in Task 5 and used by Tasks 6–11; `Issue`/`validate_tree` and `relevance::name_of` are defined in Task 6 and used by Tasks 7, 8, 9 and 10; `ShortcutLayer`/`shortcut_layer`/`shortcut_node` are defined in Task 9 and consumed by Task 5's pad path (Task 5's spec exercises the jump against a stub table and Task 9's spec re-asserts it against the real one — the only forward reference in the plan, and it is a function name, not a shape). `scenes::deluge_profile()` and `text_lookup`/`error_lookup` are defined in Task 1 and used by Tasks 4, 5, 6–9 and 11. Every shell/toolkit/harness type named above is quoted from its shipped header with the file and line.
- **Known risks:** (a) the byte-for-byte legacy-renderer comparisons (Tasks 6–8) need the legacy `HorizontalMenu::renderOLED`/`Submenu::renderOLED` to draw somewhere the spec can read — they draw into `deluge::hid::display::OLED::main`, so the spec calls them and then reads that canvas's bytes (the *spec* may include `hid/display/oled.h`; the library may not), and where legacy rendering depends on `soundEditor` state the spec calls `soundEditor.setup(clip, item)` first (survey §4) — allowed in a test. (b) `deluge_scenes_spec` links the real `deluge_app`, so a spec that brings up a `Song` shares the app's global allocator state between `it` blocks; the fixture header must tear down what it builds, and the ctest row runs with `DELUGE_HOST_DETERMINISTIC=1`. (c) Removing `BoardProfile::shortcuts` (Task 9) touches a shipped, spec'd shell type; the four call sites are all in `src/shell` and the `tests/` suite is the regression net, but the edit must land in the same commit as its spec-D amendment.
