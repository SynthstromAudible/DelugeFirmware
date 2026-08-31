# Scenes and menus: `deluge-scenes`

**Date:** 2026-08-26
**Branch target:** `next`
**Status:** design, pending review. Spec E of the UI stack redesign
(`2026-08-26-ui-stack-redesign-design.md` §9). Depends on specs B (toolkit), C (`notify`),
D (shell), G (harness). Scenes mutate the model **directly** under a direction rule (the
commands gate is staged out); menus are **declarative C++** — `Node` literals, one
interpreter, no code generation.

Naming: `snake_case` functions/variables; `PascalCase` types.

## 1. Why

The 45 `UI` subclasses (49k lines, five of them 22.7k) each mix input, model mutation,
pad rendering and OLED rendering, inherit through a five-deep chain
(`UI → RootUI → TimelineView → ClipNavigationTimelineView → ClipView`), and share
behaviour through the global `View view`. The menu system is 269 classes / 24k lines with
three parallel render paths, plus a partial Python DSL (`gui/menu_item/generate`) that
generates six subtrees and docs. Nine browser UIs implement the same file list and
QWERTY grid nine times. Sixteen context menus are sixteen classes for a list of options.

`deluge-scenes` is where the Deluge's actual UI lives: scenes composed of per-surface
views, gesture code split by concern, a table-driven menu, one browser, one context menu.

## 2. Scene inventory

| Group | Scenes | Today | Notes |
|---|---|---|---|
| Roots | `SessionScene` (rows + grid modes edit/launch/macros), `ArrangerScene`, `NoteClipScene`, `AudioClipScene`, `AutomationScene`, `KeyboardScene`, `PerformanceScene` | 7 root UIs, ~20k lines | state struct + `GridView` + `OledView` (+ `ParamOledView` where the board has `Param` surfaces) |
| Editors | `MenuScene` (one, table-driven), `SampleMarkerScene`, `SlicerScene`, `AudioRecorderScene` | `SoundEditor` + 269 items, marker editor, slicer, recorder | |
| Browsers | `BrowserScene` parameterised by `BrowserKind{Song, InstrumentPreset, Sample, Pattern, MidiDeviceDef, DxSyx, KitRow}` × `Mode{Load, Save}`; `RenameScene` | `Browser`, `SlotBrowser`, 9 `LoadUI`/`SaveUI`, `QwertyUI`, `RenameUI`, `DxSyxBrowser` | one scene, one file-list view, one QWERTY component, per-kind hook rows |
| Overlays | `PopupScene`, `ConsoleScene`, `WorkingScene`, `CountInScene`, `ParamPopupScene`, `ContextMenuScene`, `ScreensaverScene`, `BootScene`, `LoadingScene` | `OLED::popup/console`, loading animation, count-in popup, 16 `ContextMenu` subclasses, screensaver | `ContextMenuScene` takes a table; the 16 subclasses become tables + callbacks |
| Misfiled | `ClipMinderMixin` | `model/clip/clip_minder.cpp`, `instrument_clip_minder.cpp` | UI mixins under `model/`; move here |

The base chain dissolves: `TimelineView`/`ClipNavigationTimelineView`/`ClipView` become a
`TimelineState` POD plus pure helpers (`pos_from_square`, `square_from_pos`,
`square_end_from_pos`, `is_square_defined`, zoom pin squares, `length_chop_amount`,
`length_extend_amount`) in `deluge-scenes/timeline.{h,cpp}`, shared by composition.
`Clip::timeline_window{scroll, zoom}` holds the persisted per-clip values (spec C §5), so
the model's `NoteRow::getSquareInfo` uses the model's own copy, not a view.

## 3. Scene composition pattern

```cpp
namespace deluge::scenes {

struct SessionState {                        // POD; saved/restored by the shell on replace_root
	TimelineState timeline;
	SessionGridMode grid_mode;               // Edit | Launch | Macros
	Hold held_pad; Blink section_blink; Timer long_press;
	int16_t selected_clip = -1;              // index into song->sessionClips
	bool section_pending, viewing_record_arming;   // ex UI_MODE_* hold states
};

class SessionScene final : public ui::shell::Scene {
	SessionState st_;
	SessionGridView grid_{st_};              // ui-grid components ← model
	SessionOledView oled_{st_};              // ui-oled widgets ← model
	SessionParamOledView params_{st_};       // bound only when the board has Param surfaces
	ClipMinderMixin minder_{st_};
public:
	void render(Frames& f) override { grid_.render(f.grid); oled_.render(f.oled[Main]); params_.render_if(f); }
	void leds(LedFrame& l) override;
	Result handle(const UiEvent& e, const UiContext& c) override;
	void on_model_changed(const notify::DirtySet& d) override;   // ClipContent bits → rows dirty
	bool on_enter(const SceneId&) override;  void on_resume() override;
	GreyoutMask greyout_mask() const override;
};

} // namespace deluge::scenes
```

Rules:

1. `render` never mutates the model. `handle` mutates the model directly and raises
   `notify::` bits exactly as the model would (so a scene-initiated change repaints other
   scenes through the same path).
2. Every gesture that spans ticks is a field in the state struct — never a static, never a
   global mode.
3. **Views are model-aware; components are not.** `SessionGridView::render` windows
   `song->sessionClips` into `ClipRowData` spans on the stack and calls `grid::clip_row` /
   `status_pad`; components never see a `Clip*`.
4. Model access goes through `ModelStack` threading where it exists; `currentSong` reads are
   confined to view/gesture files and counted by the ratchet for the later §11 model work.
5. A scene addresses OLED surfaces by role; `render_if` on a `ParamOledView` is a no-op when
   the role is absent, so the Deluge and the new device run the same scene code.

## 4. Menus: declarative C++ tree + one interpreter

```cpp
namespace deluge::scenes::menu {

enum class Kind : uint8_t { Submenu, Toggle, Enum, Int, Decimal, Bipolar, Range, MultiRange,
                            Colour, SyncLevel, Action, Custom };     // a kind exists only if ≥3 items use it
enum class Hint : uint8_t { None, Slider, Knob, Pan, AttackCurve, DecayCurve, LpfCurve, HpfCurve,
                            Percent, BipolarBar, Envelope };
enum class Layout : uint8_t { Vertical, Horizontal };

struct MenuContext {                          // today's soundEditor.current*; passed to every hook
	Song* song; Clip* clip; ModControllableAudio* mod; ParamManagerForTimeline* params;
	int32_t source_index; MultiRange* range; bool affect_entire;
};

struct ParamRef { modulation::params::Kind kind; uint8_t index; };   // read/write via ModelStack — no code

struct Binding {                              // exactly one of param / get+set is set
	ParamRef param;
	int32_t (*get)(const MenuContext&);
	void (*set)(const MenuContext&, int32_t);
	Range (*range)(const MenuContext&);                                   // optional; default from ParamRef
	std::span<const l10n::String> names;                                  // Enum labels
	size_t (*dynamic_names)(const MenuContext&, std::span<std::string_view> out);   // the 63 dynamic lists
};

struct CustomItem {                           // Kind::Custom — small class per bespoke item
	virtual void render(ui::oled::OledFrame&, const MenuContext&, MenuState&) const = 0;
	virtual Result handle(const UiEvent&, const UiContext&, const MenuContext&, MenuState&) const = 0;
};

struct Node {
	l10n::String name, title;
	Kind kind; Hint hint; Layout layout;
	Binding bind;
	std::span<const Node* const> children;                                // Submenu
	bool (*relevant)(const MenuContext&);                                 // the 84 relevance predicates
	size_t (*dynamic_children)(const MenuContext&, std::span<const Node*> out);   // DX presets, MIDI devices…
	const CustomItem* custom;
};

// Builders keep the literals short:
constexpr Node param(l10n::String name, ParamRef p, Hint h = Hint::Knob, bool (*rel)(const MenuContext&) = nullptr);
constexpr Node toggle(l10n::String name, int32_t (*get)(const MenuContext&), void (*set)(const MenuContext&, int32_t));
constexpr Node enumeration(l10n::String name, std::span<const l10n::String> names, /*get,set*/ ...);
constexpr Node submenu(l10n::String name, std::span<const Node* const> children, Layout = Layout::Vertical, bool (*rel)(const MenuContext&) = nullptr);

inline constexpr Node lpf_freq = param(STRING_FOR_FREQUENCY, {params::Kind::PATCHED, params::LOCAL_LPF_FREQ}, Hint::LpfCurve);
inline constexpr Node lpf = submenu(STRING_FOR_LPF, {&lpf_freq, &lpf_res, &lpf_mode, &lpf_morph}, Layout::Horizontal, relevant::has_lpf);

} // namespace deluge::scenes::menu
```

Custom items (the escape hatch, ≤10): multi-range editor, patch cables, file selector,
edit-name, DX browser hooks, patch-cable strength, active scales. If the list grows past
ten, the interpreter is failing and a new `Kind` is warranted.

### 4.1 `MenuScene`

`MenuState{const Node* path[8]; uint8_t depth; ListState list[8]; ColumnsState columns; bool editing;}`.

- `render`: the current node's children (filtered by `relevant`, extended by
  `dynamic_children`) through `ui::oled::list` or, for `Layout::Horizontal`, `ui::oled::columns`
  with each column rendering its kind's value widget (`int_value`, `enum_value`, `toggle`,
  `bipolar_bar`, `knob_arc`, `pan`, `envelope`, `filter_curve` per `Hint`).
- `handle`: `turn` → cursor/focus; select → descend or begin editing per kind; `edit[]` →
  `Binding` write (`ParamRef` through `ModelStack`, else `set`) then `notify::raise(...)`
  (`ParamAutomation`, `ModTarget`, or `Oled`); back → ascend; shortcut pad → jump.
- `leds` + grid overlay: the focused node's `ParamRef` drives the shortcut-pad blink and
  patching-source blink (today's `shouldBlinkPatchingSourceShortcut`,
  `getIndexOfPatchedParamToBlink`, `updatePadLights`), computed in one place.
- **Permissions**: entering a node with `bind.param` on a multi-range sound pushes the range
  picker first (`MenuPermission::MUST_SELECT_RANGE`) — one interpreter rule replaces 18
  `checkPermissionToBeginSession` overrides.
- **MIDI/CC learn**: `MidiLearn` asks `MenuScene::learn_target() -> ParamDescriptor` from the
  focused `ParamRef`; `MenuItemWithCCLearning` and per-item `learnCC` go away.
- The "notification value" shown when a horizontal-menu column changes
  (`getNotificationValue`) is the kind's short formatter, not a per-item override.

### 4.2 Shortcut pads

`constexpr const Node* shortcuts_sound[16][8]`, `shortcuts_sound_layer2`,
`shortcuts_audio_clip`, `shortcuts_song`, referenced from the board profile's
`ShortcutTable`. Pressing a pad pushes `MenuScene` at that node (today's
`potentialShortcutPadAction` + `paramShortcutsForSounds`).

### 4.3 Docs and the retirement of the DSL

- `deluge_menu_dump` (host tool linking `deluge-scenes`, no hardware) walks the tree under
  every relevance context and emits `website/src/data/generated/menu-hierarchies.json` in the
  existing schema, replacing `website/scripts/generate-menu-hierarchies.mjs` (a 4.5k-line
  parser over `menus.cpp`). The JSON is committed and diffed in CI as a regression net.
- `gui/menu_item/generate` (`dmui`, `dmenus`, `g_menus.inc`) retires once its six subtrees
  (filters, compressor, unison, oscillators, envelopes, LFOs) are re-declared as `Node`
  literals; the Sphinx docs it produced are superseded by the JSON.

### 4.4 Validation (host test, every build)

Every node reachable from a root; every `Submenu` non-empty under at least one context;
every `ParamRef` resolves for its `MenuContext`; every `Enum` has `names` or
`dynamic_names`; every shortcut cell points at a node; no duplicate `(name, parent)`;
`Custom` count ≤ 10.

## 5. Splitting the god-views

Files split by concern; `automation/editor_layout/` is the template and moves in as-is.

`NoteClipScene` (from `instrument_clip_view.cpp`, 7.9k lines / 205 methods):

| File | Contents | Today |
|---|---|---|
| `note_clip_scene.{h,cpp}` | state, `handle` dispatch, enter/exit/resume | the `UI` overrides |
| `note_clip_grid_view.cpp` | note rows, playhead, audition column, drum rows → `clip_row`, `audition_pad`, `sidebar_columns`, `tick_squares` | `renderMainPads`/`renderSidebar`/`renderRow`/`drawAuditionSquare` |
| `note_clip_oled_view.cpp` | output name, note/velocity/probability readouts, scale display | `renderOLED` + popup callers |
| `note_edit_gestures.cpp` | press/hold/drag/copy, velocity, iterance/probability/fill, nudge, length | "PAD ACTION" + "OTHER" groups |
| `audition_gestures.cpp` | audition pads, held-note sounding, MIDI-learn reports | audition group |
| `scale_gestures.cpp` | learn scale, cycle, root note, enter/exit | "SCALE MODE" group |
| `drum_row_gestures.cpp` | add/drag/randomise/rename kit rows | drum-row group |
| `timeline.{h,cpp}` (shared) | `TimelineState` + helpers | `TimelineView`, `ClipView`, `ClipNavigationTimelineView` |

`SessionScene`: rows / grid-edit / grid-launch / macros as four gesture files over one
state. `ArrangerScene`, `AudioClipScene`, `PerformanceScene` likewise. `AutomationScene`
keeps `AutomationEditorLayout`. `KeyboardScene` wraps the existing `KeyboardLayout`s and
`ControlColumn`s through the toolkit adapters; they keep their `RGB image[][]` signatures
until the final phase.

## 6. Browsers, QWERTY, context menus, overlays

- **`BrowserScene`**: state `{FileList{items, cursor, scroll, folder}, Qwerty{text, cursor, predicting}, Favourites{bank, visible}}`;
  `FileListOledView` (list + scrollbar + icons); `QwertyGridView` (`text_keyboard` +
  favourites/banks pads); a `BrowserKind` table of hooks `{root_path, extensions,
  allow_folders, on_enter(item), preview(item), on_save(name), title}`. Nine `LoadUI`/`SaveUI`
  classes become nine rows. The file-item store (`fileItems`, culling, sorting) moves to
  `storage/` as `FileIndex` — it is not UI.
- **`RenameScene`**: `Qwerty` state + `on_commit(name)`; used for clip/output/save names.
- **`ContextMenuScene`**: `{title, span<Option>, accept_button, on_accept(index) -> bool}`;
  the loading/saving variants are `on_accept` behaviours. Pushed via
  `Action::Push{SceneId::ContextMenu{table}}`.
- **Overlays**: `PopupScene{text, popup_type, until_ms}`, `ConsoleScene{ring of lines}`,
  `WorkingScene{word, Blink}`, `CountInScene` (reads `playback.count_in_bars_remaining`),
  `ParamPopupScene` (from `ModKnobs`), `ScreensaverScene` (rainfall/starfield, shell-provided
  seed), `BootScene`, `LoadingScene`. Each draws a `popup_frame` region and reports
  `oled_transparent(Main) == true` so the scene beneath shows around it.

## 7. Migration order and acceptance

Ordered by payoff ÷ risk; each scene lands behind the shell-as-guest or the inverted
shell per spec F:

1. `MenuScene` + the settings / sound-editor tables — retires `gui/menu_item/` (24k lines);
   per-kind goldens against the legacy renderers before each kind's tables ship.
2. Overlays (popup, console, working, count-in, context menu) — retires `OLED::` compositing.
3. `BrowserScene` family — nine UIs → one.
4. `KeyboardScene`, `PerformanceScene`, `AudioClipScene`, `SlicerScene`, `SampleMarkerScene`,
   `AudioRecorderScene`.
5. `SessionScene`, `ArrangerScene`, `AutomationScene`.
6. `NoteClipScene` last; split by concern as it goes.

Acceptance per scene (spec G §6): the level-2 scripts — boot-into-scene, one navigation,
one edit gesture, one popup — produce identical snapshots for the legacy UI through
`LegacySceneAdapter` and for the new scene; plus the scene's state-transition unit tests.

## 8. Testing

- Menu tree validation (§4.4) on every build; `deluge_menu_dump` JSON diffed in CI.
- Per-kind golden frames at 128×48 and 128×64; per-scene level-2 scripts.
- Gesture files unit-tested with `tests/unit/mocks/song_mock.h` where it suffices, with the
  real model on host otherwise.

## 9. Where existing code goes

| Today | Target |
|---|---|
| `gui/menu_item/**` (269 classes), `gui/ui/menus.{h,cpp}`, `gui/menu_item/generate/**` | `menu/nodes/*.cpp` (literals), `menu/bindings.cpp`, `menu/relevance.cpp`, `menu/custom/*.cpp`, `menu_scene.cpp`, `deluge_menu_dump` |
| `gui/ui/sound_editor.*` | `MenuScene` + `MenuContext` |
| `gui/views/*_view.*` | scenes + views + gesture files |
| `gui/views/timeline_view.*`, `clip_view.*`, `clip_navigation_timeline_view.*` | `timeline.{h,cpp}` |
| `gui/ui/browser/**`, `gui/ui/load/**`, `gui/ui/save/**`, `qwerty_ui.*`, `rename/*` | `BrowserScene`, `RenameScene`, `storage/FileIndex` |
| `gui/context_menu/**` | `ContextMenuScene` + tables |
| `gui/ui/keyboard/**` | `KeyboardScene` + adapters (layouts/columns unchanged) |
| `gui/ui/slicer.*`, `sample_marker_editor.*`, `audio_recorder.*` | scenes |
| `gui/waveform/**` | peak scanning stays (reads `Sample`); drawing → toolkit |
| `model/clip/clip_minder.*`, `instrument_clip_minder.*` | `ClipMinderMixin` |
| `hid/display/rainfall.*`, `starfield.*`, `screensaver.*` | `ScreensaverScene` |

## 10. Open questions

1. `ParamRef` versus the future params registry handle (target architecture §7): `ParamRef`
   is `{Kind, index}` today; when the registry lands it becomes a stable key. The table
   literals change spelling in one builder, not per node.
2. Whether `Range` (min/max/step) belongs in `Binding` or in a per-`ParamRef` lookup table
   — the latter removes it from 200 literals; decide when the params registry's shape is
   known.
3. `MenuState::path` as node pointers (fast, `constexpr`-friendly) versus stable node ids
   (survives table edits across firmware versions for "reopen where I was") — pointers
   now; ids only if per-song menu position persistence is wanted.

## 11. Amendments during implementation (2026-08-30)

Recorded against `src/scenes/**` as it was built (Tasks 1–11) and its review rounds. Each line
names the change and why it deviates from the draft above; where a draft section number is not
named the amendment stands on its own.

**§4.3 correction** — no website generator for the menu JSON ever existed on this branch to
replace: `website/scripts/generate-menu-hierarchies.mjs` and `menu-hierarchies.json` are the target
architecture's aspiration, not code found on `next`. `deluge_menu_dump` (Task 10,
`src/scenes/menu/menu_dump_main.cpp`) *creates* the JSON from scratch, walking the tree under every
`MenuContext::Root`; the `.mdx` documentation pages that would read it stay hand-written until a
later doc task.

**§4 amendments**
- Node names are `uint16_t` string ids resolved through the l10n bridge
  (`scenes::text_lookup`/`error_lookup`, Task 1), not `l10n::String` directly — `src/scenes` may
  include `gui/l10n/` (ruling Q3), but every `Node`/`Binding` literal in `menu/nodes/*.cpp` spells
  its name as the numeric id so a table file needs no `gui/l10n/` include of its own.
- `Hint` and `IntRange` are **aliases** of `ui::oled::Hint` and `ui::oled::Range` (the toolkit's
  own types, spec B), not new types the menus layer declares — the widgets that render a `Hint` or
  a `Range` already have their own names for these shapes, and a second, menus-only enum would be a
  translation step with nothing on either side to translate.
- `Binding` gained **`ValueStyle style`** (`deluge::ui::oled::ValueStyle`, `node.h:168`) — the
  per-widget rendering style (units, precision) the draft's `Binding` sketch has no field for.
- `MenuContext` gained an explicit **`Permission`** enum (`context.h:179`, `permission_for()`'s
  return type) rather than the draft's bare description of "18 `checkPermissionToBeginSession`
  overrides collapse to one rule" — `Permission::{Yes, No, MustSelectRange}` is the actual shape,
  named so `MenuScene::descend_if_allowed` (`menu_scene.h:269`) can switch on it.
- `MenuState` (§4.1) carries **typed per-kind edit storage** (`edit_int`, `edit_bool`,
  `enum_index` + its own `ListState`/`ScrollText`, `decimal_pos`, `cursor_blink`) rather than one
  shared `int32_t` the draft's sketch implies — the widgets take different reference types
  (`int32_t&`, `bool&`, an index plus its own list-cursor state), so a single shared slot would
  need an unsafe reinterpretation at every call site instead of one field per kind.
- **`MenuSavedState`** (`menu_scene.h:175`, `static_assert(sizeof(MenuSavedState) <=
  deluge::ui::shell::kSceneStateBytes)`) is a new type the draft does not name: what a
  `replace_root` must preserve is a small subset of `MenuState` (child *indices*, not pointers, so
  a table edit between save and restore cannot resurrect a stale pointer), not the whole
  `MenuState` the shell's per-scene state budget could not hold.
- The **node-id registry** (`assign_node_ids`/`node_id`/`node_by_id`, `node_id.h`, ruling Q7) is
  new: `SceneId::arg` is 16 bits (spec D §3), so a menu position that must survive a `Push`/`Pop`
  round trip (a shortcut-pad jump, `app16/<id>` in a `.state` line) needs a stable id, not the
  pointer `MenuState::path` uses internally for speed. Both halves of open question 3 above are
  therefore true at once: pointers for the live path, ids for anything that crosses a scene
  boundary or a save file.
- **`roots()`** (`MenuScene::roots()`, indexed by `MenuContext::Root`) is the one place all ten
  root tables are gathered for `on_enter`, `validate_tree`, and `deluge_menu_dump` to walk — not
  named in the draft's §4, which shows individual root literals (`lpf`, etc.) with no table
  gathering them.

**§4.1 amendment** — `MenuScene::render` in the draft's pseudocode is now the
**`render_oled`/`render_grid`/`after_render`/`handle` split** spec D's Task 9 reconciliation
introduced shell-wide (spec D §13's "task-9 reconciliation" note): `render_oled` and `render_grid`
read and render only, `after_render` commits what a render pass observed (`MenuState::value_changed`
etc.) into a `Binding` write and raises `notify::`, and `handle` answers discrete events (back,
shortcut-pad jump). The grid overlay (shortcut-pad blink, patching-source blink) is driven by two
flags on `MenuState` (`blink_anchor_ms`, `blink_node`) rather than the draft's implicit "leds + grid
overlay" bullet, because the blink is a two-phase cycle with its own anchor, not a per-tick
computation from the focused node alone (see the blink-cadence bullet below).

**§4.2 amendment** — the shortcut tables live in **`scenes::menu::shortcuts`**
(`src/scenes/menu/shortcuts.{h,cpp}`), not referenced from the board profile's `ShortcutTable` as
the draft's §4.2 states: `BoardProfile::shortcuts` was removed in Task 9 (see spec D §13's Task 12
addition) because its two-`std::span<const uint16_t>`-layer shape could not hold five 16×8 tables
of *node pointers* plus two one-dimensional ones selected by `MenuContext::Root`, and the shell
never dereferenced the field regardless — the tables are application data belonging to the layer
that owns the `Node`s they point at.

**§7 amendment** — "the legacy-vs-new *script* acceptance" named in §7 step 1 is spec F's, not
E1's: `RunOptions::second_registry` (spec G §10's Task-6-acceptance amendment) is the mechanism a
later plan uses to run one script against both the legacy `SceneRegistry` and the new one and diff
the two `got/` trees against each other. E1 itself pins the new renderer against the legacy
renderer **in-process** instead (Tasks 6–8's per-kind goldens, `sim/scenes_spec/tables_*_spec.cpp`)
— a byte-for-byte comparison of the two renderers' output for the same fixture, not a script replay
through two registries.

**§2 correction** — `soundEditorRootMenu` has **27 children**, not the plan's 28
(`sound_root.h:6-37`; `tables_2_spec` asserts 27 against the live legacy object rather than trust a
hand count).

**Range-dependent items** — `MustSelectRange` (§4.1) is driven by an explicit `Node::range_dependent`
flag (`node.h:254`, set via `with_range_dependent(...)`, ported from legacy's
`MenuItem::isRangeDependent()`), not by "any node whose `Binding` carries a param" as an earlier
pass assumed — only the ten osc-subtree nodes legacy actually marks (sample transpose, loop point,
the file selector, the audio recorder, …) carry the flag; `validate_tree`'s
`expected_range_dependent` parameter (`validate.h:141`) audits the flag is set on exactly that set
and no others.

**`Binding::Scaling`** — `Binding` gained an explicit `Scaling{Standard, HalfPrecision, Pan}`
(`node.h:102`, `bindings.h:16-23`): legacy's default integer scaling (`Scaling::Standard`) is not
universal. Missed in an earlier pass and caught reviewing the port against
`value_scaling.cpp:36` — **`Scaling::HalfPrecision`** covers osc pulse width and the compressor
params, whose legacy scaling halves precision to make room for negative stored values
`Scaling::Standard` cannot represent; **`Scaling::Pan`** covers the two pan params, which
`bindings.cpp:207-214` additionally infers as a backstop when a node's `ParamRef` names one of them
and no scaling was set explicitly.

**`Binding::updates_reverb_params` and the `ReverbUpdateWithHook` validator rule** — a node whose
write must additionally poke `AudioEngine::reverb`'s live parameters (the sidechain/reverb params,
`sound_sidechain.h:14`, `roots.h:44`) sets `Binding::updates_reverb_params` (`node.h:188`);
`validate_tree` now rejects (`Issue::Kind::FlagMismatch`, detail `Flag::ReverbUpdateWithHook`,
`validate.cpp:277-279`) a node that sets both `updates_reverb_params` **and** a `set` hook, because
`bindings::write` runs the `set` hook and returns without ever reaching the
`updates_reverb_params` branch (`bindings.cpp:611` sits after the `set`-hook dispatch) — the flag
would silently never fire on such a node, and the two mechanisms were never meant to combine on the
same item.

**`bindings::serves_param` and `Serves{unpatched, patched}`** — Task 6's ruling: `serves_param`
(`bindings.cpp:329`) takes an explicit `Serves` struct (`{bool unpatched; bool patched;}`,
`bindings.cpp:247`) derived from the *resolved* `MenuContext` (`serves_for(ctx)`,
`bindings.cpp:304`: `.unpatched = holds_unpatched_params(ctx)`, `.patched = ctx.sound != nullptr`)
rather than asked of the `ParamManager` directly, because **`ParamManager` has no structural
discriminator of its own** between an `UnpatchedParamSet` and a `MIDIParamCollection` in
`summaries[0]`, nor between a `PatchedParamSet` and an `ExpressionParamSet` in `summaries[1]`
(`param_manager.cpp:60-82`: `setupMIDI`/`setupUnpatched`/`setupWithPatching` all write a bare
`paramCollection` pointer into the same `summaries[]` slot with nothing recording which concrete
type is there). The caller's own context — what kind of output/clip is open — is the only thing
that currently answers "which concrete collection is this", which is why `serves_param` takes it as
a parameter instead of asking the manager. **Recorded here as owed, out of scope for E1**: the
durable fix is a model-side tag on `ParamManager` itself (e.g. a `ParamCollectionKind` per summary
slot), so a caller — this layer or any other — can ask the manager directly instead of
re-deriving the answer from context every time a menu binding reads or writes.

**Dropped and surviving `Kind`s** — spec E §4's `Kind::{Bipolar, Range, MultiRange, Colour, Action}`
were **dropped**: zero nodes across all ten roots need them, which fails spec E §4's own "a kind
exists only if ≥3 items use it" rule at zero, let alone three. `SyncLevel` survived (it has real
users). The shipped `Kind` enum is `{Submenu, Toggle, Enum, Int, Decimal, SyncLevel, Custom}`
(`node.h:64-72`) — seven, not the draft's twelve. `Action` is re-added in E2 with its first real
consumer (a bespoke action item E1's ten roots have no need for).

**`DuplicateName`'s co-visibility qualifier** — `validate_tree`'s `DuplicateName` rule
(`validate.cpp:167-180, :336`) is qualified: two siblings sharing a `(name, parent)` are flagged
only when both could actually be on screen at once (their `relevant` predicates are not mutually
exclusive) — a flat "no two children share a name" rule would reject legacy's own **nine blessed
`FormattedTitle` collisions** (e.g. both sample recorders under different contexts), which are
genuinely the same displayed name on two nodes that are never both relevant simultaneously.

**`scenes-includes-outside` allows `io/**`** — the layer-rule ratchet's `scenes-includes-outside`
row (`scripts/check_layer_rules.py`) permits `src/scenes/**` to include `io/**`, joined in Task 8:
the MIDI/CV settings root needs `midiEngine` (`io/midi/midi_engine.h`), `MIDIDeviceManager`
(`io/midi/midi_device_manager.h`) and `MIDITranspose` (`io/midi/midi_transpose.h`), which hold the
state seven of the MIDI settings page's items edit and have no `model/` spelling. `hid/` stays out
— nothing in `src/scenes` needed it, and the rule's whole point is that menus never reach for
display/LED primitives directly.

**Shortcut-pad mechanics** — three corrections to how legacy's shortcut pads actually work, found
porting Task 9:
- Legacy's **second shortcut layer is a second press of the focused item's own pad**
  (`secondLayerShortcutsToggled`, `sound_editor.cpp:1309-1324`), **not** "shift held" — shift is
  only the gate that lets a pad be treated as a shortcut at all (`sound_editor.cpp:1114`); which
  layer's cell to use is the caller's business (`shortcuts.h:73-78`).
- Two deliberate press/blink asymmetries, both legacy's own and preserved rather than "fixed":
  **`kMidiExtraCells`** (`shortcuts.cpp:244`) resolves four pads on the MIDI/CV and drum roots that
  are in no legacy table at all (`sequenceDirectionMenu` and the MIDI/CV randomizer trio,
  `sound_editor.cpp:1282-1302`) — they navigate on press but never blink, because the blink side
  (`updatePadLightsFor`) only ever scans the shortcut tables' own column 11. The **`KitGlobalFx`
  press gate** (`kit_global_fx_press_gate`, `shortcuts.cpp:255-263`) is the mirror: legacy's press
  arm is gated `(x <= kDisplayWidth - 2) || (x == 15 && y == 1)` (`sound_editor.cpp:1153-1157`), so
  column 15 is admitted only at `y == 1` — the table's own `(15, 2)`/`(15, 3)` cells are unreachable
  by press yet still blink, since the blink scan has no such guard.

**Blink cadence** — the shortcut-pad blink is legacy's **200 ms two-phase cycle**
(`menu_scene.h:70-72`, `menu_scene.cpp:672-674`): a 20 ms re-arm on the source-pad phase and a
180 ms re-arm on the focused-param phase, not a symmetric period — a `Blink`'s single symmetric
period cannot represent this, which is why `MenuState` carries `blink_anchor_ms`/`blink_node`
instead of a `Blink` for this one purpose. The **visible DUTY cycle within each phase is not
derivable from this repo**: legacy's two re-arm intervals are timer re-arm periods driving a
PIC-side `SET_PAD_FLASHING` state machine, not on/off durations this codebase can read — the port
reproduces the two re-arm intervals faithfully and leaves the PIC-side duty cycle as a hardware
fact outside this layer's reach.

**The dump's `description` omission and fail-closed `dynamic_children`** — `deluge_menu_dump`
(`menu_dump_main.cpp`) omits `"description"` **structurally**, not as an empty string: `Node` has no
such field, and the retired Python DSL's `description` came from a whole separate `docs/menus/*.md`
file per node that has no analogue here. Separately, a node with `Node::dynamic_children` set
**fails the dump closed** (`fail_on_dynamic_children`, `menu_dump_main.cpp:230-246`, exits
non-zero) rather than emit an invented `"multicontext"` shape — an earlier pass had synthesized one
to keep the dump running, but the recovered `doc_emitter.py`'s own `visit_multicontext_instance`
entry (`"multicontext"`, `{"type", "title", "name", "description"}`) has **no `"children"` key at
all**, so inventing one would not even match the schema it was trying to preserve. The first real
`dynamic_children` node (E2's job) is what has to design this dump's multicontext handling for
real, against the tree it is then dumping under, rather than have this task guess at a shape ahead
of any real user.

**Table specs drive both `write()` paths independently** — every `sim/scenes_spec/tables_*_spec.cpp`
exercises `bindings::write()` for every node with a `set` hook against legacy's own
`writeCurrentValue()` on the **same fixture**, driving each path from an **independent sentinel**
value rather than comparing one against the other's output (`tables_1_spec.cpp:316-330`, fix round
1 issue I2): the original shape ran the legacy write first and the new write second and compared
results, which cannot tell a real `set` hook from a silent no-op port hook — both leave the fixture
at the legacy write's value, which would read as agreement. Seeding each side with its own sentinel
before driving it means a no-op port hook is caught immediately (the fixture stays at the sentinel
instead of moving to the expected value).

**An upstream legacy bug found, not fixed** — `osc::AudioRecorder::isRelevant`
(`gui/menu_item/osc/audio_recorder.h:59-62`) does `static_cast<Sound*>(modControllable)
->getSynthMode()` with no null check. Opening the ACTIONS submenu (`soundEditorRootActionsMenu`,
whose children include the two sample-recorder items, `sound_actions.h:23`) on a MIDI or gate drum
row — whose `ModControllableAudio*` is null — dereferences a null `Sound` in legacy. The ported
predicate, `relevance::is_subtractive` (`relevance.cpp:256-257`), guards `ctx.sound != nullptr`
before touching `getSynthMode()`, so the port does not reproduce the crash — this is a legacy bug
the port fixes as a side effect of porting the predicate correctly, not one this task went back and
patched in `gui/menu_item/`. Recorded here rather than filed, since spec E1's scope is the port, not
legacy bug triage.

**A second upstream legacy bug, and this one WAS patched — E2 task 11** — `FlashStorage::readSettings`
(`storage/flash_storage.cpp`, the `buffer[166]` branch) assigned `gui::menu_item::fillColourMenu.value`
in **both** arms where it meant `onceColourMenu.value`. `writeSettings` stores FILL at byte 165 and
ONCE at byte 166, so on every boot the ONCE pad colour was discarded and the FILL colour was
immediately overwritten by ONCE's stored byte. Two tokens, in the exact routine E2 task 11 was
porting.

*Why patched rather than recorded, when the `AudioRecorder::isRelevant` deref above was recorded.*
The distinction is not severity, it is whether the port can stand without the fix:

  * `AudioRecorder::isRelevant` is a **predicate the port replaces**. `relevance::is_subtractive`
    guards the null, so the ported path is already correct and legacy's copy is dead to this plan —
    patching it would change a code path no ported item runs.
  * The colour bytes are **shared state the port now reads and writes through the same storage as
    legacy** (`FlashStorage::padColours`, task 11). The port's six `pad_colour_*` nodes and legacy's
    six `Colour` items address the identical byte, so shipping the port on top of a loader that
    mis-assigns that byte would mean shipping a menu item whose value is silently discarded at every
    boot. There is no version of "port this item correctly" that leaves it.

*It changes shipping firmware behaviour, and there is a migration consequence.* On flash written by
the buggy firmware, byte 166 holds whatever `onceColourMenu.value` happened to be at write time —
which is its static-init value, RED, unless the user edited ONCE during that same boot. After the
fix that byte is read into ONCE, so an upgraded user can boot with **ONCE = RED**. The both-red
guard immediately below the branch only fires when FILL is red as well, so it does not catch this
case. It is self-healing the first time the user sets the ONCE colour, and it is stated in
`CHANGELOG.md` where a release note will find it.

**`selectButtonPress()` chaining, and this is the whole list** — legacy's "select on
this leaf opens *that* menu" is a per-item `MenuItem* selectButtonPress()` override returning
another `MenuItem*`; `SoundEditor` navigates to whatever comes back
(`gui/ui/sound_editor.cpp`'s select handling). E1 had no `Node` spelling for it at all:
`MenuScene::after_render` ascended on **any** leaf select press (`menu_scene.cpp:893-897`, which
transcribed only the `return nullptr` = "go up" arm of the base class), and until fix round 2's I5
the difference lived only in four scattered header comments. The complete sweep of
`grep -rn selectButtonPress src/deluge/gui/menu_item/`, in three groups:

**E2 task 3 ported group (a)** as `Node::select_chain` (a `const Node* (*)(const MenuContext&,
const MenuState&)` hook — null means "ascend", legacy's own spelling) beside `Node::chain_targets`
(a static span of every node the hook may return). The span is what a *walker* uses: a hook cannot
be called ahead of time, and without a static list the seven pages no submenu literal names would
be unnumbered by `assign_node_ids`, unvalidated by `validate_tree` and absent from
`deluge_menu_dump` — so `SceneId::arg` could not address a chained page and `MenuScene::on_enter`
could not restore one. `assign_node_ids`, `validate.cpp`'s `collect` and the dump all follow
`chain_targets` as a second edge kind; the first two terminate on a cycle by the "already seen"
test they already had, and the dump fails loudly on one rather than invent a `$ref` shape.

*(a) Leaf-to-menu chaining whose source node E1 declares — ported by E2 task 3 except the `dxMenu`
row:*

| legacy item | jumps to | condition | port node | status |
|---|---|---|---|---|
| `arpeggiator::PresetMode` (`arpeggiator/preset_mode.h:109-118`) | `arpOctaveModeToNoteModeMenu`, or `…ForDrums` when `editingKitRow()` | value is `ArpPreset::CUSTOM` | `nodes::arp_preset` | ported |
| `arpeggiator::OctaveModeToNoteMode` (`arpeggiator/octave_mode.h:90`) | `arpNoteModeFromOctaveModeMenu` | always | `nodes::arp_octave_mode_to_note_mode`, reachable only via the row above (`arpPatternMenu` names the *plain* `arpOctaveModeMenu`) | ported |
| `arpeggiator::OctaveModeToNoteModeForDrums` (`arpeggiator/octave_mode.h:99`) | `arpNoteModeFromOctaveModeMenuForDrums` | always | `nodes::arp_octave_mode_to_note_mode_drums`, as above | ported |
| `unison::CountToStereoSpread` (`unison/count.h:76-79`) | `unison::stereoSpreadMenu` | always | `nodes::unison_count` | ported |
| `voice::PolyphonyType` (`voice/polyphony.h:139-143`) | `polyphonicVoiceCountMenu` | mode is `PolyphonyMode::POLY` | `nodes::polyphony` | ported |
| `osc::Type` (`osc/type.h:120-125`) | `dxMenu` | osc type is `OscType::DX7` | `nodes::osc0_type`, `nodes::osc1_type` | **carried**: `dxMenu` (`menus.cpp:1715-1720`) is itself chain-only, and its three children are E2's DX browser items — declaring it would mean three more `kPending` placeholders with no implementation behind them |
| `cv::Selection` (`cv/selection.h:41-46`) | `cvSubmenu` (also sets `currentSourceIndex` and the page title) | always | `nodes::cv_selection` | chain data ported; see below |
| `gate::Selection` (`gate/selection.h:43-54`) | `gateOffTimeMenu` at `NUM_GATE_CHANNELS`, else `gateModeMenu` (after `format()` + `updateOptions()`) | always | `nodes::gate_selection` | chain data ported; see below |
| `PatchedParam` (`patched_param.cpp:32-38`) | `source_selection::regularMenu` (also sets `soundEditor.patchingParamSelected` to the param) | shift **not** held; the shift arm delegates to `Param::selectButtonPress` (= `Automation`'s automation delete) | **53 nodes** — see below | **carried** |

**The largest chaining override in the tree is `PatchedParam`'s, and E2 task 3's review is where the
sweep first got it right.** The row above was missing from the original sweep, and the group-(c)
entry below described `PatchedParam` as merely *inheriting* `Automation::selectButtonPress` — which
is half the function. `PatchedParam::selectButtonPress` (`gui/menu_item/patched_param.cpp:32-38`)
delegates to the automation-delete arm **only when shift is held**; otherwise it records the param
in `soundEditor.patchingParamSelected` and returns `&source_selection::regularMenu`, i.e. a select
press on VOLUME opens the mod-source picker. Three `final`/`override` delegators funnel into it:
`MasterTranspose` (`master_transpose.h:67`), `menu_item::Transpose` (`transpose.h:33`) and
`patched_param::Integer` (`patched_param/integer.h:42`).

By node count it dwarfs every other row. Measured against the live legacy objects over all ten
roots (`dynamic_cast<PatchedParam*>` on each `(node, item)` pair of `tables_3_spec`'s lockstep walk,
deduplicated by node): **53** distinct port nodes, of which 4 are the `menu_item::Transpose`
derivatives that carry a learn-only `ParamRef`. That set is exactly the set of nodes with
`bind.param.kind == params::Kind::PATCHED` — the two counts agreed with zero mismatches, so the
port-side test for "does this node chain to the mod-source picker" is a one-line predicate rather
than a list.

**Deliberately not implemented by E2 task 3** (ruling): its target, `source_selection::regularMenu`,
is a patch-cable page — a `kPending` `Kind::Custom` node today (`nodes/sound_master.h:16-22` holds
its sibling), owned by the patch-cable task. Implementing the chain now would mean inventing a page
whose owner arrives later, which is the one thing the "declare every chain-target page you need"
ruling exists to avoid doing badly. The chain arrives with that page: one
`with_select_chain(..., patched_param_chain, kPatchCablePickerChain)` applied to the 53, whose hook
also needs somewhere to put `patchingParamSelected` (no `MenuContext` field for it today) and a
`UiContext::shift` read the `SelectChainFn` signature does not currently pass.

`cvSubmenu`, `gateModeMenu` and `gateOffTimeMenu` were the three menus reachable **only** through
this mechanism and so absent from `settings.h` — E2 task 3 declares all three (with `cvSubmenu`'s
two children, `cv::Volts` and `cv::Transpose`, over `MenuContext::source_index`), hung off the two
selectors' `chain_targets`, which is how they get node ids without inventing a parent legacy does
not have. Two halves of those two chains are still owed: legacy's selectors *write*
`soundEditor.currentSourceIndex` before navigating and a `SelectChainFn` is handed a `const
MenuContext&`, so the pages address whatever output `source_index` already holds; and both selectors
are still `kPending` `Kind::Custom` stand-ins, whose renderer never reports a select press, so
nothing fires the hooks through the interpreter yet. The chain data is real and specced directly
(`sim/scenes_spec/tables_3_spec.cpp`'s chaining group, against the live legacy globals).

One arm of legacy's own navigation is deliberately not ported with it: `SoundEditor`'s
`maybeGetParentMenu`/`focusChild` reparenting (`sound_editor.cpp:369-375`), which enters a
horizontal *parent* with the named child focused rather than the child itself. `MenuScene::enter_child`
has never done that for any descent, so it is a pre-existing difference the chaining work inherits.

*(b) Chaining whose source node is already a `kPending` `Kind::Custom` placeholder, so it arrives
with that item's real implementation rather than needing chaining as such:*
`source_selection::Regular` → `patch_cable_strength::regularMenu` (`source_selection/regular.cpp:32`);
`source_selection::Range` → `patch_cable_strength::rangeMenu` (`source_selection/range.cpp:32`);
`patch_cable_strength::PatchCableStrength` → `source_selection::rangeMenu`, plus a shift-held
automation-delete arm (`patch_cable_strength.cpp:302-306`) — reached from the shipped `kPending`
node `patch_cable_strength::Fixed` (`nodes/sound_master.h:16-22`);
`MultiRange` → its stored `menuItemHeadingTo` (`multi_range.cpp:360`);
`PatchCables` → the cable's own strength page (`patch_cables.cpp:164-171`);
`midi::Devices` → `midiDeviceMenu` (`midi/devices.cpp:154`);
`mpe::DirectionSelector` → `zoneSelectorMenu` (`mpe/direction_selector.cpp:31`);
`mpe::ZoneSelector` → `zoneNumMemberChannelsMenu` (`mpe/zone_selector.cpp:32`);
`dx::{Cartridge, GlobalParams, OperatorParams, EngineSelect}` (`dx/cartridge.cpp:157`,
`dx/global_params.cpp:78`, `dx/operator_params.cpp:80`, `dx/engine_select.cpp:62`).

*(c) Overrides that open a UI or act rather than naming another menu, plus the base-class arms:*
`FileSelector` (`file_selector.cpp:49`), `ActiveScaleMenu` (`active_scales.cpp:108`),
`audio_clip::AudioSourceSelector` (`audio_clip/audio_source_selector.h:27`),
`audio_clip::SetClipLengthEqualToSampleLength` (`.../set_clip_length_equal_to_sample_length.h:26`),
`song::ConfigureMacros` (`song/configure_macros.h:27`), `song::MidiLearn` (`song/midi_learn.h:27`),
`stem_export::Start` (`stem_export/start.h:31`), `reset_settings::Reset` (`reset_settings/reset.h:29`),
`performance_session_view::EditingMode` (`performance_session_view/editing_mode.h:49-72`),
`note::IterancePreset` → `noteCustomIteranceRootMenu` and `note_row::IterancePreset` →
`noteRowCustomIteranceRootMenu` (`note/iterance_preset.h:59`, `note_row/iterance_preset.h:72` — the
note editor's menus, which none of E1's ten roots reach) — every one of these is either a `kPending`
`Kind::Custom` node already or outside E1's tree. And the base classes:
`MenuItem::selectButtonPress` returns `nullptr` = "go up", which is exactly what
`MenuScene::after_render`'s `else` arm does (and what a null `select_chain` answer means);
`Submenu::selectButtonPress` (`submenu.cpp:187-194`) enters the
focused child, which `enter_child` does — **except** its `shouldForwardButtons()` arm, which
forwards the press to the focused child instead, and which is not ported;
`Toggle`/`Selection::selectButtonPress` (`toggle.h:33-45`, `selection.h:54-64`) toggle in place when
the item is not itself the open menu, which this generation's widgets own rather than the
interpreter (`ui/oled/values.h:169`); and `Automation::selectButtonPress`
(`automation/automation.cpp:37`), inherited by every `Param`/`UnpatchedParam` item,
**deletes the parameter's automation when shift is held** — a second dropped behaviour, listed here
because the sweep found it, owed to E2 alongside the chaining. (A `PatchedParam` item reaches that
arm too, but *only* through `PatchedParam::selectButtonPress`'s shift branch — its non-shift branch
is the group-(a) row above, which this entry originally hid. E2 task 3's review, corrected.)

Not an override, but the sweep's `grep` finds it and it is the one place a `selectButtonPress()`
result is **discarded**: `HorizontalMenu::handleItemAction`
(`gui/menu_item/horizontal_menu.cpp:663-666`) calls `menuItem->selectButtonPress()` on a non-submenu
column that returns false from `allowToBeginSessionFromHorizontalMenu()` — the default
(`menu_item.h:314`), so nearly every value column — throws the returned `MenuItem*` away, and shows
a notification instead. **So in legacy a chaining column edits in place on a horizontal page and
never navigates.** The port matches without needing a rule for it: on a horizontal page the
*submenu* is the focused node, so `after_render` takes its `Kind::Submenu` arm and never reaches the
`select_chain` consult, and `MenuScene::render_columns` already answers a value-column press with
the same notification and no navigation (E1's own transcription of `:657-661`). Two ported chains do
have a column source (`arp_preset` on `arpBasicMenu`, `unison_count` on `unisonMenu`), so this is
what makes "select on the UNISON page's NUM column does not open the spread page" correct rather
than a gap — the chain fires once that column has been entered as a leaf, which is when legacy's
`SoundEditor` consults `selectButtonPress` for navigation too.

**The `Custom` cap counts nodes, and is not 10** (fix round 2, I4) — spec E §4 says "at most 10
`Kind::Custom` items across the whole tree", and `validate_tree`'s `TooManyCustom` rule implemented
that by counting **distinct `CustomItem` instances**. That was inert: E1 ships the *shape* of its
bespoke items and points every one of them at the single shared `nodes::kPending` placeholder, so
the distinct-instance count of the whole shipped tree is **1** and the rule could not fire however
many custom nodes a table added — it would have begun counting only once E2 replaced the
placeholders one by one, i.e. it was switched off for exactly the phase it existed to police.
Meanwhile `src/scenes/README.md` told the next contributor the cap was enforced. The rule now counts
**nodes that point at a custom item** (`validate.h`'s `kMaxCustomNodes`, `validate.cpp:343`). The
real number: E1's tree had **59** `Kind::Custom` nodes reachable from `nodes::kAllRoots`, and the
cap was set to **60**, one above reality — a ratchet against the escape hatch growing unnoticed, not
spec E's number and not a claim that ten was ever reachable for a faithful port of legacy's tree.
`sim/scenes_spec/shortcuts_spec.cpp` pins the exact count (`kShippedCustomNodes`) separately, so
adding a custom node has to be said in two places.

*E2 amendment (task 12).* The count is now **52** and the cap **53**. E2 took twelve nodes out of the
escape hatch rather than implementing them as bespoke items — task 11's nine `Kind::Enum`s (the six
pad colours, the play cursor, the dev-sysex setting, the shortcuts version) and task 11b's three
revived `Kind::Range`s (`default_tempo`, `default_swing_amount`, `default_key`) — while the cap
stayed where E2 task 9 left it, so it drifted to four above reality: four bespoke items a table
could have added for free. Ratcheted down to follow. The same task added
`validate.h`'s `kPendingCustomNodes` (**11**) and a by-identity assertion of *which* nodes those
are: a count alone cannot tell "a placeholder was finished" from "a placeholder was finished and a
new one appeared somewhere else", which is exactly the drift a number meant to say "E2 is not
finished" must not be able to hide. The eleven are the file/sample family — both oscillator file
browsers, the four sample loop points, the two audio recorders — plus `midi_device_linked`,
`specific_output_selector` and `audio_clip_waveform`. All of them need the card or a view no plan
so far ports, and they belong to E3; the plan that closes them out edits that list deliberately,
and deletes `ValidateOptions::allow_placeholder_custom` with the last of them.

**E2's carried work** — behaviour spec E named as belonging to a later phase, still owed once E1's
ten roots and `MenuScene` ship: `source_shortcut_pads` (needs patch cables); per-pad blink
frequency masks; the `SHORTCUTS_VERSION_1` pre-3.0 shortcut layer; two context-dependent blink
suppressions; settings-root shortcut jumps (blocked on `MenuContext` no longer conflating "which
root menu is open" with "what view is behind it" — needs an additive `background_root` field, a
`context_for` argument to supply it, and an application-side caller holding the live clip);
the four `hid/`-side write side effects a menu write can trigger (pad brightness, screensaver
suppression, canvas flags, sticky shift) that `src/scenes` cannot reach directly;
**`selectButtonPress()` chaining** — DONE in E2 task 3 (`Node::select_chain` /
`Node::chain_targets`, and the seven chain-only pages declared with it). What is left of that row,
largest first: **`PatchedParam` → `source_selection::regularMenu` on 53 nodes**
(`patched_param.cpp:32-38`), which waits on the patch-cable page that is its target;
`osc0_type`/`osc1_type` → `dxMenu` (`osc/type.h:120-125`), which waits on the DX browser; the two
write halves of the CV/gate chains; and the two other arms the sweep turned up, `Submenu`'s
`shouldForwardButtons()` forwarding and `Automation::selectButtonPress`'s shift-held automation
delete — see the `selectButtonPress()` amendment above;
`Source::getOrCreateFirstRange()` on a zero-range sample item; per-node cursor memory across
sessions; `Kind::Custom` receiving raw input (today's `CustomItem` interface renders but the E1
placeholder never routes input to it — the shipped mechanism is `nodes::kPending` /
`CustomItem::is_placeholder()` / `Issue::Kind::PlaceholderCustom` /
`ValidateOptions::allow_placeholder_custom`; the `DELUGE_SCENES_E1_PARTIAL` macro the plan
proposed was never built); and gold-knob acceleration
(`calcNextKnobSpeed`).
