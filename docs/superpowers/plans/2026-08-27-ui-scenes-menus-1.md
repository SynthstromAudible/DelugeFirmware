# Scenes and menus, part 1 (`deluge_scenes` foundation + `MenuScene`) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Create the `deluge_scenes` library and land spec E's first migration step — the declarative C++ menu tree (`Node` literals), one interpreter (`MenuScene`), the bindings that read/write the model through `ModelStack`, the shortcut tables, the tree validator, and the host tool that dumps the tree to JSON — so `gui/menu_item/**` (24k lines) can be retired once spec F's shell-as-guest routes `soundEditor` to `MenuScene`.

**Architecture:** `src/scenes/` is the first layer allowed to include the model. `menu/` holds pure data (`Node`, `Binding`, `ParamRef`, builders) plus `bindings.cpp` (the only file that touches `ModelStack`/`AutoParam`) and `context.cpp` (the port of `SoundEditor::setup`'s context resolution). `MenuScene` renders the current node through the toolkit (`list`/`columns`/value widgets/meters) and applies input to the focused node's `Binding`. Tables are `inline constexpr Node` literals in `menu/nodes/*.cpp`, one file per legacy root; custom items are ≤10 small classes behind `CustomItem`. Tests run in a new `deluge_scenes_spec` executable inside the `sim/` project (the only host build that links the real model), plus the pure parts in `tests/spec`.

**Tech Stack:** C++26, CMake, CppSpec, `deluge_ui_shell`, the toolkit, `deluge_notify`, the model (`src/deluge/model`, `modulation/params`), `sim/` host build.

**Spec:** `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md` (spec E) §1–4, §7 step 1, §8, §9 menu rows, §10. Depends on the shell plan (`2026-08-27-ui-shell.md`: `Scene`, `SceneId`, `Frames`, `UiEvent`, `UiContext`, `Result`, `Action`, `BoardProfile::shortcuts`, `TextLookup`) and the harness plan (`2026-08-27-ui-test-harness.md`: `run_script` for level 2). Survey of the legacy menu system with line numbers: `.superpowers/sdd/2026-08-27-ui-scenes-menus/menu-survey-summary.md`.

**Scope note (spec E is several plans):** this is E1. E2 = custom items + browsers + `RenameScene` + `ContextMenuScene`; E3 = overlays (`CountInScene`, `ParamPopupScene`, `ScreensaverScene`) + the model-touching services (`GlobalControls`, `ModKnobs`, `MidiLearn`); E4–E6 = the root scenes in spec E §7 order (Keyboard/Performance/AudioClip/Slicer/Marker/Recorder; Session/Arranger/Automation; NoteClip). Each is its own plan, written when its predecessor lands.

## Global Constraints

- Branch: `ui/scenes-1` off `ui/harness` (stacked on shell + harness). "Builds" = `./dbt build Debug` compiles to the known ThinLTO link failure; `cmake --build build-sim-gfx --target deluge_host` links; `tests/` suite green; **and** the sim project's new spec executable green: `cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON && cmake --build build-sim --target deluge_scenes_spec && ctest --test-dir build-sim -R scenes`.
- Layering (umbrella §rules, spec E §3 rule 4): `src/scenes/**` may include `scenes/**`, `shell/**`, `ui/**`, `gfx/**`, `notify/**`, `libdeluge/**`, std, and — being the first model-aware layer — `model/**`, `modulation/**`, `playback/**` (read), `storage/**`, `gui/l10n/**`, `definitions_cxx.hpp`. It may **not** include `gui/` (other than `gui/l10n/`), `hid/`, or `gui/ui/ui.h`. Only `src/scenes/menu/bindings.cpp` and `src/scenes/menu/context.cpp` may include `model/model_stack.h`/`modulation/automation/auto_param.h`; `currentSong` reads are confined to `context.cpp` and counted by a ratchet row. Task 12 adds the rows.
- Menus are data: no `Node` has behaviour beyond function pointers in `Binding`/`relevant`/`dynamic_*`; a `Kind` exists only if ≥3 items use it; `Custom` count ≤ 10 (validator-enforced).
- Parity: `MenuScene` renders through the toolkit widgets that were pixel-pinned to the legacy renderers (`list` ↔ `MenuItem::drawItemsForOled`, `columns` ↔ `HorizontalMenu::renderMenuItems`, value widgets ↔ `Integer/Decimal/Toggle/Selection::drawPixelsForOled`, `meter()` ↔ `Number::renderInHorizontalMenu`); per-kind goldens at 128×48 and 128×64; behaviour cites the legacy line it ports (`sound_editor.cpp`, `menu_item.cpp`, the item classes).
- Naming: `snake_case`; `PascalCase` types; `k` constants; namespace `deluge::scenes` / `deluge::scenes::menu`. No heap in scenes; POD state; no wall clock.
- Never commit `src/bsp/rust/Cargo.lock`; never touch `src/bsp/rza1`; commit per "Commit" step after `./dbt format`.

---

## File structure

```
src/scenes/CMakeLists.txt                   deluge_scenes (STATIC); deluge_menu_dump (host tool, sim project)
src/scenes/README.md
src/scenes/l10n_bridge.h / .cpp             TextLookup/ErrorLookup for the shell profile (l10n::getView, getErrorMessage port)
src/scenes/menu/node.h                      Kind, Hint, Layout, ParamRef, Binding, CustomItem, Node, builders
src/scenes/menu/context.h / .cpp            MenuContext + context_for(Clip*, source_index) (port of SoundEditor::setup's resolution)
src/scenes/menu/bindings.h / .cpp           read/write through ModelStack/AutoParam; get/set escape hatch; range defaults
src/scenes/menu/relevance.h / .cpp          the relevance predicates (has_lpf, is_kit, …) shared by tables
src/scenes/menu/validate.h / .cpp           validate_tree(roots) -> FixedVector<Issue, 64>
src/scenes/menu/nodes/settings.cpp          settingsRootMenu subtree
src/scenes/menu/nodes/sound_*.cpp           soundEditorRootMenu subtrees (filters, compressor, unison, osc, env, lfo, master, arp, randomizer, fx, sidechain, mixer, voice, bend, actions)
src/scenes/menu/nodes/roots.cpp             the other roots (drum, midi/cv, midi drum, gate drum, audio clip, performance, song, kit global fx)
src/scenes/menu/nodes/shortcuts.cpp         shortcut tables -> ShortcutTable
src/scenes/menu/menu_scene.h / .cpp         MenuScene (Scene), MenuState
src/scenes/menu/menu_dump_main.cpp          deluge_menu_dump: JSON in the existing g_menus.json schema
src/scenes/registry.h / .cpp                scene_registry(): shell scenes + MenuScene (grows in E2+)
tests/spec/scenes/*_spec.cpp                pure parts: node builders, validator, relevance with a fake context, shortcut tables
sim/scenes_spec/*_spec.cpp                  model-backed: bindings, context_for, MenuScene render/handle, level-2 scripts
tests/golden/ui/menu/*.pbm|.state           per-kind goldens; script goldens
tests/ui-scripts/shell/menu_*.txt
website/src/data/generated/menu-hierarchies.json   committed dump (new file — the generator spec E named does not exist)
```

Modified: `src/CMakeLists.txt`, `src/deluge/CMakeLists.txt` (link `deluge_scenes` into `deluge_app` — unused until spec F), `sim/CMakeLists.txt` (`deluge_scenes_spec`, `deluge_menu_dump`, ctest rows), `tests/CMakeLists.txt`/`tests/spec/CMakeLists.txt`, link closure (build.rs ×3, harness ×2, task scripts), `scripts/check_layer_rules.py` + baseline, `.github/workflows/tests.yml` (menu JSON diff), `src/deluge/gui/menu_item/generate/` (deleted in Task 6 once its six subtrees are re-declared; `menus.cpp:244` include removed), `docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md` (§4.3 corrected; amendments).

---

### Task 1: `deluge_scenes` skeleton, l10n bridge, sim spec executable

**Files:**
- Create: `src/scenes/CMakeLists.txt`, `src/scenes/README.md`, `src/scenes/l10n_bridge.{h,cpp}`, `sim/scenes_spec/main.cpp`, `sim/scenes_spec/l10n_bridge_spec.cpp`, `tests/spec/scenes/.keep`
- Modify: `src/CMakeLists.txt` (`add_subdirectory(scenes)` after `shell`), `src/deluge/CMakeLists.txt`, `sim/CMakeLists.txt` (target `deluge_scenes_spec`: CppSpec driver over `sim/scenes_spec/*_spec.cpp`, linking `deluge_app deluge_scenes deluge_ui_shell` and the host BSP objects **without** `host_main.c` — copy how `deluge_loadcheck`/`host_loadcheck_main.cpp` is wired; `add_test(NAME scenes_spec COMMAND deluge_scenes_spec)` with `DELUGE_HOST_DETERMINISTIC=1`), link closure files (add `deluge_scenes` next to `deluge_ui_shell`).

**Interfaces:**
- `deluge::scenes::text_lookup(uint16_t string_id) -> std::string_view` = `l10n::getView(static_cast<l10n::String>(id))`; `error_lookup(int16_t code) -> std::string_view` = the `getErrorMessage(Error)` switch (`src/deluge/hid/display/display.cpp:7-…`) **ported** here (the scenes layer may not include `hid/`); `install_l10n(BoardProfile&)` sets both function pointers.
- CMake: `deluge_scenes` STATIC linking `deluge_ui_shell deluge_ui_oled deluge_ui_grid deluge_notify` and the app's include dirs (`src/deluge`, `src`, `include`).

- [ ] **Step 1: Failing spec** — `sim/scenes_spec/l10n_bridge_spec.cpp`: `text_lookup(uint16_t(l10n::String::STRING_FOR_LPF)) == l10n::getView(l10n::String::STRING_FOR_LPF)`; `error_lookup(int16_t(Error::SD_CARD_FULL))` equals the legacy `getErrorMessage(Error::SD_CARD_FULL)` (call the legacy function in the spec — the *spec* may include `hid/display/display.h`); every `Error` value 0..32 yields a non-empty string.
- [ ] **Step 2: Configure and run** — `cmake -B build-sim -S sim -G Ninja -DDELUGE_SIM_X64=ON && cmake --build build-sim --target deluge_scenes_spec` → fails (no library).
- [ ] **Step 3: Implement** the bridge (transcribe the switch; keep the same `STRING_FOR_ERROR_*` ids), the CMake wiring, the spec driver (`CppSpec::parse(argc, argv).add_spec(...)` per spec file, aggregated in `main.cpp` like `tests/spec`'s `create_specs_driver` — or reuse that function if `sim/` can include `tests/spec`'s CMake helper).
- [ ] **Step 4: Run** — `ctest --test-dir build-sim -R scenes_spec` PASS; `./dbt build Debug` compiles (archive produced); `deluge_host` links; `python3 scripts/check_layer_rules.py` green.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): deluge_scenes skeleton, l10n bridge, model-backed spec executable in sim/"`

---

### Task 2: Menu core types and builders (pure)

**Files:**
- Create: `src/scenes/menu/node.h`, `tests/spec/scenes/node_spec.cpp`
- Modify: `tests/spec/CMakeLists.txt` (add `../../src/scenes` include dir only — `node.h` is header-only and model-free so `tests/spec` can test it without the model)

**Interfaces** (spec E §4, with the amendments below):
```cpp
namespace deluge::scenes::menu {
enum class Kind : uint8_t { Submenu, Toggle, Enum, Int, Decimal, Bipolar, Range, MultiRange, Colour, SyncLevel, Action, Custom };
enum class Hint : uint8_t { None, Slider, LengthSlider, Knob, Pan, AttackCurve, ReleaseCurve, LpfCurve, HpfCurve, Percent, Bar, Ducking };   // = Number::RenderingStyle (number.h:25-38), one per legacy style
enum class Layout : uint8_t { Vertical, Horizontal };
struct ParamRef { deluge::modulation::params::Kind kind = params::Kind::NONE; uint8_t index = 255; constexpr bool valid() const { return kind != params::Kind::NONE; } };
struct MenuContext;                                 // context.h
struct IntRange { int32_t min, max, step = 1; };
using GetFn = int32_t (*)(const MenuContext&); using SetFn = void (*)(const MenuContext&, int32_t); using RangeFn = IntRange (*)(const MenuContext&);
using RelevantFn = bool (*)(const MenuContext&); using DynamicNamesFn = size_t (*)(const MenuContext&, std::span<std::string_view> out);
struct Node; using DynamicChildrenFn = size_t (*)(const MenuContext&, std::span<const Node*> out);
struct Binding { ParamRef param{}; GetFn get = nullptr; SetFn set = nullptr; RangeFn range = nullptr; std::span<const uint16_t> names{}; DynamicNamesFn dynamic_names = nullptr; uint8_t decimal_places = 0; };   // names are l10n string ids (uint16_t) so node.h stays free of gui/l10n
struct MenuState; struct CustomItem { virtual ~CustomItem() = default; virtual void render(ui::oled::OledFrame&, const MenuContext&, MenuState&) const = 0;
	virtual ui::shell::Result handle(const ui::shell::UiEvent&, const ui::shell::UiContext&, const MenuContext&, MenuState&) const = 0; };
struct Node { uint16_t name, title; Kind kind = Kind::Submenu; Hint hint = Hint::None; Layout layout = Layout::Vertical; Binding bind{};
	std::span<const Node* const> children{}; RelevantFn relevant = nullptr; DynamicChildrenFn dynamic_children = nullptr; const CustomItem* custom = nullptr; uint8_t slots = 1; };
// builders (constexpr; title defaults to name)
constexpr Node param(uint16_t name, ParamRef p, Hint h = Hint::Knob, RelevantFn rel = nullptr, uint16_t title = 0);
constexpr Node int_item(uint16_t name, GetFn get, SetFn set, IntRange r, Hint h = Hint::None, RelevantFn rel = nullptr);
constexpr Node decimal_item(uint16_t name, GetFn, SetFn, IntRange, uint8_t places, RelevantFn = nullptr);
constexpr Node toggle(uint16_t name, GetFn, SetFn, RelevantFn = nullptr);
constexpr Node enumeration(uint16_t name, std::span<const uint16_t> names, GetFn, SetFn, RelevantFn = nullptr);
constexpr Node dynamic_enumeration(uint16_t name, DynamicNamesFn, GetFn, SetFn, RelevantFn = nullptr);
constexpr Node submenu(uint16_t name, std::span<const Node* const> children, Layout = Layout::Vertical, RelevantFn = nullptr, uint16_t title = 0);
constexpr Node dynamic_submenu(uint16_t name, DynamicChildrenFn, RelevantFn = nullptr);
constexpr Node action(uint16_t name, SetFn run, RelevantFn = nullptr);
constexpr Node custom(uint16_t name, const CustomItem*, RelevantFn = nullptr);
inline constexpr size_t kMaxDynamicChildren = 64, kMaxDynamicNames = 64;
}
```
Amendments vs spec E §4 (record in Task 12): string ids are `uint16_t` (not `l10n::String`) so `node.h` is model-free and host-testable in `tests/spec`; `Hint` mirrors the twelve legacy `Number::RenderingStyle`s 1:1 (spec listed ten); `Bipolar`/`Range`/`MultiRange`/`Colour`/`SyncLevel` kinds are declared now but their interpreters land in E2 except `SyncLevel` and `Bipolar` (used ≥3 times in E1's tables — verify by counting; drop any kind that ends below 3).

- [ ] **Step 1: Failing spec** — `tests/spec/scenes/node_spec.cpp`: `constexpr Node a = param(1, {params::Kind::PATCHED, 5}, Hint::LpfCurve);` `static_assert(a.kind == Kind::Int && a.bind.param.index == 5 && a.title == 1)`; `constexpr const Node* kids[] = {&a}; constexpr Node s = submenu(2, kids, Layout::Horizontal);` `static_assert(s.children.size() == 1 && s.layout == Layout::Horizontal)`; a runtime `it` checking `toggle`/`enumeration` builders fill `bind.get/set/names`.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement** `node.h`.  - [ ] **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): declarative menu Node, Binding, ParamRef and constexpr builders"`

---

### Task 3: `MenuContext` and `context_for()` (port of `SoundEditor::setup` context resolution)

**Files:**
- Create: `src/scenes/menu/context.{h,cpp}`, `sim/scenes_spec/context_spec.cpp`
- Ports: `SoundEditor::setup` `src/deluge/gui/ui/sound_editor.cpp:1618-1852` (only the resolution of `newSound/newParamManager/newArpSettings/newModControllable/currentSourceIndex/currentMultiRange/…` and the root-menu choice — not the UI side), `SoundEditor::getCurrentModelStack` `:1982-2005`, `checkPermissionToBeginSessionForRangeSpecificParam` (sound_editor.h:144).

**Interfaces:**
```cpp
struct MenuContext {                                       // spec E §4 MenuContext + the survey's current* fields it needs
	Song* song; Clip* clip; Output* output; Sound* sound; ModControllableAudio* mod; ParamManagerForTimeline* params;
	ArpeggiatorSettings* arp; MultiRange* range; int16_t range_index; int8_t source_index; Source* source; SampleControls* sample_controls;
	VoicePriority* priority; SideChain* sidechain; MIDICable* midi_cable; bool affect_entire; bool is_kit, is_drum, is_audio_clip, is_song_view, is_performance, is_kit_global_fx;
	enum class Root : uint8_t { Sound, Drum, MidiOrCv, MidiDrum, GateDrum, AudioClip, Performance, Song, KitGlobalFx, Settings } root;
};
MenuContext context_for(Song&, Clip* clip_or_null, int8_t source_index, bool performance_view, bool session_or_arranger_root);   // no UI reads: the caller passes what SoundEditor read from currentUIMode/getRootUI()
ModelStackWithThreeMainThings* model_stack_for(const MenuContext&, void* memory);   // getCurrentModelStack port (bindings.cpp's only entry point to the stack)
enum class Permission : uint8_t { No, Yes, MustSelectRange };
Permission permission_for(const Node&, const MenuContext&);                          // spec E §4.1: a node with bind.param on a multi-range sound -> MustSelectRange (port of the 18 checkPermissionToBeginSession overrides' common rule; the bespoke ones move to E2 customs)
```

- [ ] **Step 1: Failing spec** (real model on host): build a `Song` with one synth clip (use the model's own constructors/`createNewInstrument…` helpers — find how `deluge_loadcheck` or `tests/spec_*` build a song; if none, use `Song::…` minimal setup and cite): `context_for(song, clip, 0, false, false)` yields `root == Sound`, `sound != nullptr`, `params == &clip->paramManager`, `affect_entire == false`; a kit clip with `affectEntire` → `root == Sound` with `is_kit && affect_entire`; a kit clip with a selected drum → `root == Drum`; `clip == nullptr` and not performance → `root == Settings`; `performance_view` → `Performance`; a MIDI instrument clip → `MidiOrCv`; `model_stack_for` returns a stack whose `modControllable == ctx.mod && paramManager == ctx.params`; `permission_for(param node, ctx with sound having 2 ranges)` → `MustSelectRange`.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement** by transcribing the resolution branches in `setup()` order, citing each branch.  - [ ] **Step 4: Run** — PASS.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): MenuContext resolution and model-stack access ported from SoundEditor::setup"`

---

### Task 4: Bindings — read/write through `ModelStack`, escape hatch, ranges, notify

**Files:**
- Create: `src/scenes/menu/bindings.{h,cpp}`, `sim/scenes_spec/bindings_spec.cpp`
- Ports: `patched_param::Integer::readCurrentValue/writeCurrentValue` (`gui/menu_item/patched_param/integer.cpp:32-74`), `UnpatchedParam::readCurrentValue/writeCurrentValue/getModelStack` (`unpatched_param.cpp:…-73`), `computeCurrentValueForStandardMenuItem` and its inverse (`menu_item/param.cpp` or wherever — grep), `Integer::getMaxValue/getMinValue` defaults for patched/unpatched params (`patched_param/integer.h`, `unpatched_param.h`, `integer.h`), the AFFECT_ENTIRE kit loop over `kit->firstDrum` (both write functions), the MIDI-follow feedback call (`view.sendMidiFollowFeedback` — expose as a hook `bindings::set_feedback_hook(fn)`; scenes may not include `gui/views`), the automation-view refresh (`automationView.possiblyRefreshAutomationEditorGrid` → becomes `notify::raise(Dirty::ParamAutomation)`), `Decimal` scaling (`decimal.cpp`), `ParamDescriptor` construction for learn (`getLearningThing`, `UNPATCHED_START`).

**Interfaces:**
```cpp
namespace deluge::scenes::menu::bindings {
int32_t read(const Node&, const MenuContext&);                          // ParamRef via param set getValue + computeCurrentValueForStandardMenuItem; else bind.get
void write(const Node&, const MenuContext&, int32_t value);              // ParamRef via model_stack_for(...)->get{Patched,Unpatched}AutoParamFromId(idx)->autoParam->setCurrentValueInResponseToUserInput(scaled, ms); kit affect-entire loop; feedback hook; notify::raise(ParamAutomation | ModTarget | Oled per kind); else bind.set + notify::raise(Dirty::Oled)
IntRange range(const Node&, const MenuContext&);                        // bind.range ? bind.range(ctx) : legacy default per kind (0..50 for standard params — cite integer.h; -25..25 for pan; enum 0..names-1)
size_t names(const Node&, const MenuContext&, std::span<std::string_view> out);   // static (text_lookup per id) or dynamic
ParamDescriptor learn_target(const Node&, const MenuContext&);          // for MidiLearn (E3)
using FeedbackHook = void (*)(const MenuContext&, const Node&, int32_t knob_pos); void set_feedback_hook(FeedbackHook);
}
```

- [ ] **Step 1: Failing spec** (real model): for a synth clip, `write(lpf_freq_node, ctx, 30)` then `read(...) == 30` and the param set's raw value equals what `patched_param::Integer::writeCurrentValue` would have stored (compute via the legacy scaling function called directly in the spec); an unpatched global param round-trips; a `toggle` with get/set round-trips through a `bool` in the spec; `range(pan node) == {-25, 25, 1}`; `names(enum node)` returns the l10n strings; writing raises `Dirty::ParamAutomation` (drain `notify::dirty()` and check); the kit affect-entire write reaches every drum (`kit->firstDrum` chain: two drums, both updated).
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement**, citing each ported line.  - [ ] **Step 4: Run** — PASS; both checkers green (`bindings.cpp` is one of the two files allowed to include the model stack — Task 12's rule).
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): menu bindings through ModelStack/AutoParam with the legacy scaling, ranges and notify raises"`

---

### Task 5: `MenuScene` — the interpreter (render + handle + permissions + shortcut blink)

**Files:**
- Create: `src/scenes/menu/menu_scene.{h,cpp}`, `src/scenes/registry.{h,cpp}`, `sim/scenes_spec/menu_scene_spec.cpp`, goldens `tests/golden/ui/menu/*.pbm`
- Ports: `MenuItem::renderOLED/drawItemsForOled` (`menu_item.cpp:47-66`, already pinned by `list`), `HorizontalMenu::renderOLED/renderMenuItems/preparePaging/selectMenuItem/switchVisiblePage` (`horizontal_menu.cpp`), `SoundEditor::selectEncoderAction :1034`, `buttonAction :333` (back → `goUpOneLevel :672`, select → `selectButtonPress` semantics: nullptr = up, `NO_NAVIGATION` = stay), `horizontalEncoderAction :1008`, `padAction :1405` (+ `potentialShortcutPadAction :1111`), `enterSubmenu :324`, `setupShortcutBlink :959`/`blinkShortcut :974-1006`, `getNotificationValue` (Decimal/Enumeration/Integer short formatters), `Submenu::focusChild`, `MultiRange` redirect (`menuItemHeadingTo`) → the `Permission::MustSelectRange` rule.

**Interfaces:**
```cpp
struct MenuState { const Node* path[8]; uint8_t depth; ui::ListState list[8]; ui::ColumnsState columns; ui::ScrollText scroll; ui::Blink blink; int8_t decimal_pos; bool editing; bool range_picker_pending; const Node* pending_after_range; };
class MenuScene final : public ui::shell::Scene {
public: void open(const Node& root, const MenuContext&);                         // SceneId arg = root table index (registry of roots)
	SceneKind kind() const override;                                                // SceneKind::FirstApp + 0
	void render(Frames&) override; Result handle(const UiEvent&, const UiContext&) override; void leds(LedFrame&) override;
	bool on_enter(const SceneId&) override; void on_resume() override; void on_model_changed(const notify::DirtySet&) override;
	GreyoutMask greyout_mask() const override;                                       // legacy: menus grey out the grid except shortcut pads — cite sound_editor.cpp getGreyoutColsAndRows
	const Node* focused() const; ParamDescriptor learn_target() const;
	static std::span<const Node* const> roots();                                     // Settings, Sound, Drum, … indexed by MenuContext::Root
private: MenuState st_; MenuContext ctx_; /* helpers: visible_children(node, out span, ctx) applying relevant + dynamic_children; render_vertical(list); render_horizontal(columns with per-Kind column renderers: Int/Decimal -> meter_column with Hint, Enum/Toggle -> text_column/toggle_column, Submenu -> submenu_column); render_leaf (int_value/decimal_value/enum_value/toggle at the leaf when editing); notification value formatter */ };
```
Behaviour (each cited): `turn` moves the list cursor / column focus; select on a `Submenu` descends (`path[++depth]`), on a leaf enters editing (or toggles for `Toggle` on the two-row screen per Task-6-toolkit ruling: select reports `entered` → menu treats as "up"); `edit[0]`/`edit[1]` while editing → `bindings::write` with the range; `back` ascends (`goUpOneLevel`'s permission loop: skip nodes whose `permission_for == No`); shortcut pad (with shift, per `potentialShortcutPadAction`) → jump to the table's node; `Permission::MustSelectRange` on entering a param node → push the range picker (E2's `MultiRange` custom; in E1 the scene records `range_picker_pending` and shows the legacy "select range" behaviour by descending into a `Kind::MultiRange` placeholder node that lists the ranges by index using `bindings::names` — enough for the golden; E2 replaces it); `leds()` blinks the focused node's shortcut pad and patching-source pads (`Blink{kShortcutBlinkMs}` from `blinkShortcut`'s 180/20-tick cadence converted to ms — cite the tick length); the horizontal-menu "notification value" popup on column change uses `notify::notice_text(short_value)` with the kind's formatter (`Decimal` 2 places, `Enum` short option, `Int` digits).

- [ ] **Step 1: Failing spec** (real model, `FakeSurfaceSet` + `Shell` from the shell plan): open the settings root → golden `menu_settings_root_48.pbm`/`_64`; `turn +1` moves the cursor (state `list[0].cursor == 1`); select on a submenu descends (`depth == 1`); back ascends; open the sound root's LPF horizontal submenu → golden `menu_lpf_columns_48.pbm` (four columns FREQ/RES/MODE/MORPH with knob/bar/text/… per Hint) and `_64`; `edit[0] = +1` on the focused FREQ writes through `bindings` (param value changed; `Dirty::ParamAutomation` raised); shortcut pad with shift jumps to that node; `permission_for == MustSelectRange` path descends into the range list; `leds()` contains the focused shortcut pad blink; `greyout_mask()` matches legacy's cols/rows for the sound editor.
- [ ] **Step 2: Run to see it fail.**  - [ ] **Step 3: Implement** with the toolkit widgets only (no direct canvas drawing in the scene — the ratchet will count `canvas.` uses).  - [ ] **Step 4: Run** — PASS; goldens recorded after the state assertions; both geometries.
- [ ] **Step 5: Commit** — `git commit -m "feat(scenes): MenuScene — one interpreter for vertical and horizontal menus over Node tables"`

---

### Task 6: Tables batch 1 — the six generated subtrees (filters, compressor, unison, oscillators, envelopes, LFOs); retire `generate/`

**Files:**
- Create: `src/scenes/menu/nodes/sound_filters.cpp`, `sound_compressor.cpp`, `sound_unison.cpp`, `sound_osc.cpp`, `sound_env.cpp`, `sound_lfo.cpp`, `src/scenes/menu/relevance.{h,cpp}`, `src/scenes/menu/validate.{h,cpp}`, `tests/spec/scenes/validate_spec.cpp`, `sim/scenes_spec/tables_1_spec.cpp`
- Delete (at the end of the task): `src/deluge/gui/menu_item/generate/**`; remove `#include "gui/menu_item/generate/g_menus.inc"` at `gui/ui/menus.cpp:244` and re-declare the same globals it produced **as they were** (paste the generated `.inc` content into `menus.cpp` verbatim — the legacy tree must not change in this plan; only the generator is retired). Remove the `deluge_menus` custom target from CMake.
- Sources: `src/deluge/gui/menu_item/generate/dmenus/{filter/lpf.py, filter/hpf.py, compressor.py, unison.py, oscillator/*, envelope/*, lfo/*}` (the declarative specs — node names, titles, param ids, hints, relevance/mode conditions) and the classes they instantiate (`gui/menu_item/filter/*`, `compressor/*`, `unison/*`, `osc/*`, `envelope/*`, `lfo/*`) for the get/set logic of non-`ParamRef` items (e.g. LPF mode enum, oscillator type, LFO shape/sync).

**Interfaces:**
```cpp
// relevance.h — one predicate per legacy isRelevant that these subtrees use (cite each): has_lpf, has_hpf, is_synth, osc_is_sample(ctx), osc_is_wavetable, osc_not_fm, modulator_relevant, lfo_is_synced, …
// validate.h
struct Issue { enum class Kind : uint8_t { Unreachable, EmptySubmenu, BadParamRef, EnumWithoutNames, ShortcutToNothing, DuplicateName, TooManyCustom, MissingTitle } kind; const Node* node; uint16_t detail; };
FixedVector<Issue, 64> validate_tree(std::span<const Node* const> roots, std::span<const MenuContext> contexts);   // every rule of spec E §4.4; `contexts` = one MenuContext per Root kind built by the caller (tables_spec builds real ones; validate_spec uses fakes)
// nodes: inline constexpr Node lpf_freq, lpf_res, lpf_mode, lpf_morph, lpf (submenu, Horizontal, relevance::has_lpf), hpf…, compressor…, unison…, osc0…, osc1…, env1..env4, lfo1..lfo4; each file exposes `extern const Node* const k<Subtree>Roots[]`
```

- [ ] **Step 1: Failing specs** — `validate_spec.cpp` (pure): a fake tree with an empty submenu, a dangling shortcut, an enum without names, 11 customs → the four issues; the real batch-1 roots validate clean under a fake context that says "synth, has LPF". `tables_1_spec.cpp` (real model): for each of the six subtrees, the `Node` children list equals the legacy `HorizontalMenu`/`Submenu` item names in order (walk `lpfMenu.getItems()` from `menus.h` and compare `getName()` against `text_lookup(node.name)`), the `ParamRef`s equal each legacy item's `getParamKind()/getParamIndex()`, and `Hint` equals `getRenderingStyle()` mapped 1:1; every enum's names equal the legacy `getOptions()` strings; `MenuScene` render of the LPF submenu matches the legacy renderer byte-for-byte: render `lpfMenu` through the legacy `HorizontalMenu::renderOLED` into a `Canvas` (the spec may include `gui/`) and compare with the `MenuScene` golden from Task 5 (`menu_lpf_columns_48.pbm`).
- [ ] **Step 2: Run to see them fail.**  - [ ] **Step 3: Implement** the six files by transcribing the Python specs (names/titles → `STRING_FOR_*` ids, `param(...)`, `enumeration(...)` with get/set ported from the legacy classes), `relevance.cpp`, `validate.cpp`; then retire `generate/` as described (the legacy tree byte-identical: `git diff --stat` on `g_menus.inc`'s content pasted in, and the level-3 boot script golden from the harness plan unchanged).
- [ ] **Step 4: Run** — PASS; `deluge_host` links; `./dbt build Debug` compiles; `ctest --test-dir build-sim -R "scenes|ui_app_scripts"` green.
- [ ] **Step 5: Commit** — two commits: `feat(scenes): menu tables for filters, compressor, unison, oscillators, envelopes, LFOs; tree validator` then `chore(menus): retire the Python menu generator (subtrees now declared as Node tables)`.

---

### Task 7: Tables batch 2 — the rest of the sound root (master, arp, randomizer, FX, sidechain, mixer, voice, bend, actions) excluding customs

**Files:**
- Create: `src/scenes/menu/nodes/sound_master.cpp`, `sound_arp.cpp`, `sound_randomizer.cpp`, `sound_fx.cpp`, `sound_sidechain.cpp`, `sound_mixer.cpp`, `sound_voice.cpp`, `sound_bend.cpp`, `sound_actions.cpp`, `sound_root.cpp` (the `soundEditorRootMenu` literal: children in the legacy order `soundEditorRootActionsMenu, soundMasterMenu, arpMenu, randomizerMenu, audioCompMenu, soundFiltersMenu, soundFXMenu, sidechainMenu, source0Menu, source1Menu, modulator0Menu, modulator1Menu, oscMixerMenu, env1..4, lfo1..4, voiceMenu, bendMenu, drumBendRangeMenu` — `menus.cpp:1521-1548`), `sim/scenes_spec/tables_2_spec.cpp`
- Sources: `gui/ui/menus.cpp` (the literals for each submenu and their item classes under `gui/menu_item/{master,arp,randomizer,fx,sidechain,mixer,voice,bend,…}`), `menu_item/patch_cables.*` and `multi_range.*` **excluded** (E2 customs): where the legacy submenu contains a custom item, the table carries `custom(name, nullptr)` with a `// E2` comment and the validator's `TooManyCustom`/`nullptr` rule tolerates a null custom only under `DELUGE_SCENES_E1_PARTIAL` (a CMake option on by default until E2 lands — document).

- [ ] **Step 1: Failing spec** — same shape as Task 6's `tables_1_spec`: per submenu, names/order/ParamRefs/Hints/enum options equal legacy; `validate_tree` clean under the Sound context (with the partial flag); `MenuScene` renders the sound root list → golden `menu_sound_root_48.pbm`/`_64` and matches the legacy `Submenu::renderOLED` byte-for-byte.
- [ ] **Step 2–4:** implement, run, record.  - [ ] **Step 5: Commit** — `git commit -m "feat(scenes): sound-editor root tables (master, arp, randomizer, fx, sidechain, mixer, voice, bend, actions)"`

---

### Task 8: Tables batch 3 — settings root and the other roots (drum, MIDI/CV, MIDI drum, gate drum, audio clip, performance, song, kit global FX)

**Files:**
- Create: `src/scenes/menu/nodes/settings.cpp`, `src/scenes/menu/nodes/roots.cpp`, `sim/scenes_spec/tables_3_spec.cpp`
- Sources: `menus.cpp:1899` (`settingsRootMenu` and its subtrees — `FlashStorage`/`runtimeFeatureSettings` get/set items, cite each class under `gui/menu_item/{defaults,runtime_feature,midi,cv,gate,trigger,record,sample,flash,…}`), the other `soundEditorRootMenu*` literals (`menus.cpp`, grep `PLACE_SDRAM_BSS Submenu soundEditorRootMenu`), `midiOrCVParamShortcuts`/`gateDrumParamShortcuts` (`:949, :960`).

- [ ] **Step 1: Failing spec** — as before per root; plus `MenuScene::roots()[Root::X]` resolves each; `validate_tree` clean across **all** contexts (one `MenuContext` per Root built from a song with a synth clip, a kit clip with a drum, a MIDI clip, a CV clip, an audio clip, and the settings context); settings goldens.
- [ ] **Step 2–4:** implement, run, record.  - [ ] **Step 5: Commit** — `git commit -m "feat(scenes): settings root and the drum/MIDI/CV/audio-clip/performance/song/kit-FX root tables"`

---

### Task 9: Shortcut tables → `ShortcutTable`; profile wiring

**Files:**
- Create: `src/scenes/menu/nodes/shortcuts.cpp`, `tests/spec/scenes/shortcuts_spec.cpp` (pure: table shapes), `sim/scenes_spec/shortcuts_spec.cpp` (cross-check vs legacy tables)
- Modify: `src/shell/profiles/deluge.cpp` (`shortcuts` = the scenes' tables — the profile is data; the shell plan left `ShortcutTable` empty), `src/shell/profile.h` if `ShortcutTable` needs to become `std::span<const Node* const>` per layer (it was `uint16_t` param ids — amend to node pointers per spec E §4.2; the shell never dereferences them)
- Sources: `paramShortcutsForSounds :1921`, `SecondLayer :1941`, `AudioClips :1960`, `SongView :1979`, `KitGlobalFX :1998`, `midiOrCVParamShortcuts :949`, `gateDrumParamShortcuts :960` — cell by cell (`nullptr` → `nullptr`, `comingSoonMenu` → `nullptr` with a comment).

- [ ] **Step 1: Failing spec** — for every (x,y) in each table, the node's `name` equals the legacy cell's `getName()` (or both null); the shell's `deluge_profile().shortcuts.layer0[y*16+x]` is the same pointer; `validate_tree`'s `ShortcutToNothing` rule passes.
- [ ] **Step 2–4:** implement, run.  - [ ] **Step 5: Commit** — `git commit -m "feat(scenes): shortcut-pad tables as Node pointers, wired into the Deluge profile"`

---

### Task 10: `deluge_menu_dump` — JSON in the existing `g_menus.json` schema; committed dump; CI diff

**Files:**
- Create: `src/scenes/menu/menu_dump_main.cpp`, `website/src/data/generated/menu-hierarchies.json` (new — the generator spec E §4.3 named does not exist; the docs page `website/src/content/docs/reference/menu_hierarchies.mdx` is hand-written and stays), `scripts/menu_dump_check.sh`
- Modify: `sim/CMakeLists.txt` (`deluge_menu_dump` executable linking `deluge_app deluge_scenes` + host BSP objects sans main; ctest `menu_dump_matches_committed`), `.github/workflows/tests.yml`

- Output schema = the retired DocEmitter's (`generate/dmui/visitor/doc_emitter.py`): `{"type": "basic"|"submenu"|"multimode"|"multimode-mode"|"multicontext", "title", "name", "description", "children"?, "available_when"?}` — map `Kind::Submenu` → `submenu`, leaves → `basic`, a node with `relevant` → `available_when` = the predicate's registered name (`relevance::name_of(fn)` — a small table from function pointer to string, maintained next to the predicates), `dynamic_children` → expanded under each provided context with `"multicontext"`. Walk every root under every context (the six built in Task 8); sort keys; stable indentation.

- [ ] **Step 1:** build the tool; run it → write the JSON; `scripts/menu_dump_check.sh` = build + run + `git diff --exit-code website/src/data/generated/menu-hierarchies.json`; ctest row runs the script; CI step runs it after the sim build.
- [ ] **Step 2:** spot-check five entries against the hand-written `menu_hierarchies.mdx` (report discrepancies; do not edit the mdx).
- [ ] **Step 3: Commit** — `git commit -m "feat(scenes): deluge_menu_dump emits the menu tree as JSON; committed dump diffed in CI"`

---

### Task 11: Level-2 acceptance scripts per item kind

**Files:**
- Create: `tests/ui-scripts/shell/menu_int.txt`, `menu_decimal.txt`, `menu_enum.txt`, `menu_toggle.txt`, `menu_submenu_horizontal.txt`, `menu_shortcut.txt`, `menu_range_permission.txt`; goldens `tests/golden/ui/menu/<script>/`; `sim/scenes_spec/menu_scripts_spec.cpp` (uses the harness plan's `run_script`/`expect_script_goldens` with the real registry and a song fixture built in-process)
- Each script: open the menu at a known node (`push` via a `menu <root> <path…>` extension? No — keep the script format: the spec pushes `MenuScene` programmatically before `run_script` by passing `initial` in `RunOptions` — add `RunOptions::initial_scene` to the harness runner in this task, documented as an amendment), then `encoder Select +1`, `button SelectEnc down/up`, `encoder GoldUpper +2`, `snapshot` after each, `button Back`, `snapshot`, `quit`.

- [ ] **Step 1:** write the scripts; run once to record; inspect every snapshot (ASCII in the report); run again → PASS; determinism: run twice, byte-identical.
- [ ] **Step 2: Commit** — `git commit -m "test(scenes): level-2 menu scripts per item kind with goldens"`

---

### Task 12: Layer rules, README, spec amendments

**Files:**
- Modify: `scripts/check_layer_rules.py` (+ baseline): rows `scenes-includes-gui` (`src/scenes/**`, include, `^(gui/(?!l10n/)|hid/)`, baseline 0), `scenes-model-stack` (`src/scenes/**` **except** `menu/bindings.cpp` and `menu/context.cpp` — implement as a glob list or a `except` field on the row; include `^(model/model_stack\.h|modulation/automation/auto_param\.h)`, baseline 0), `scenes-currentSong` (`src/scenes/**`, symbol, `\bcurrentSong\b`, baseline = today's count in `context.cpp` — counted, shrink-only, spec E §3 rule 4), `scenes-no-canvas` (`src/scenes/menu/**`, symbol, `\bcanvas\.` , baseline 0 — menus draw only through widgets), `shell-no-clock` extended to `src/scenes/**`; `src/scenes/README.md` (how to add a node, a kind, a custom; the validator; the dump; the E2+ roadmap); spec E: §4.3 corrected (no website generator existed; the dump creates the JSON; the mdx stays hand-written until a later doc task), §4 amendments (uint16 string ids, Hint list, `IntRange`, `Permission`, `MenuState` fields, `roots()`), §7 note that legacy-vs-new script acceptance runs in spec F when the guest lands (E1 pins the new renderer against the legacy renderer in-process instead — Tasks 6–8).
- [ ] **Steps:** prove each new rule with a probe (exit 1) then remove; `--update`; full suites (tests/, sim/); commit — `git add scripts src/scenes/README.md && git add -f docs/superpowers/specs/2026-08-26-ui-scenes-menus-design.md && git commit -m "test(scenes): layer rules for the scenes layer; README; spec E amendments"`

---

## Self-review notes

- **Spec coverage (E1 scope).** §2 inventory: only `MenuScene` built here (E2+ for the rest — stated). §3 composition rules 1–5: rule 1 (`render` never mutates; `handle` writes + raises) → Tasks 4–5; rule 2 (state struct) → `MenuState`; rule 3 (views model-aware, components not) → `MenuScene` maps `Binding` → widget values, widgets never see the model; rule 4 (`currentSong` confined + counted) → Task 12; rule 5 (roles) → `MenuScene` draws on `Main` only via `Frames::by_role`. §4 tree/builders → Task 2; `MenuContext` → Task 3; `Binding` → Task 4; `CustomItem` interface → Task 2 (implementations E2); §4.1 `MenuScene` (render/handle/leds/permissions/learn target/notification value) → Task 5; §4.2 shortcuts → Task 9; §4.3 dump + DSL retirement → Tasks 10, 6; §4.4 validation → Task 6 (+8 across contexts); §7 step 1 → the whole plan; §8 testing → Tasks 5–8, 11; §9 menu rows → Tasks 6–8; §10 open questions carried (`ParamRef` shape, `Range` placement, path as pointers).
- **Placeholders:** none; Task 7's `custom(name, nullptr)` with the `DELUGE_SCENES_E1_PARTIAL` flag is an explicit, validated, documented interim (E2 removes the flag).
- **Type consistency:** `Node`/`Binding`/`ParamRef`/`Kind`/`Hint`/`Layout`/`IntRange` (T2) used by T3–T11; `MenuContext`/`context_for`/`model_stack_for`/`Permission` (T3) by T4–T8; `bindings::read/write/range/names/learn_target` (T4) by T5; `MenuScene`/`MenuState`/`roots()` (T5) by T6–T11; `validate_tree`/`Issue` (T6) by T7–T9; `ShortcutTable` (shell) amended in T9 and consumed by T5's pad handling.
- **Known risks:** the byte-for-byte legacy-renderer comparisons (Tasks 6–8) require the legacy `HorizontalMenu::renderOLED` to draw into a canvas the spec controls — it draws into `OLED::main`; the spec reads `OLED::main`'s bytes after calling it (the spec, not the library, includes `hid/display/oled.h`); if legacy rendering depends on `soundEditor` state, the spec sets up `soundEditor` via `soundEditor.setup(clip, item)` first (the survey's §4) — that is allowed in a test.
