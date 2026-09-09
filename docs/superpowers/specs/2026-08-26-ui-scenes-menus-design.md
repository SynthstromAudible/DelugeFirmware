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
| Overlays | `PopupScene`, `ConsoleScene`, `WorkingScene`, `CountInScene`, `ParamPopupScene`, `ContextMenuScene`, `ScreensaverScene`, `LoadingScene` | `OLED::popup/console`, loading animation, count-in popup, 16 `ContextMenu` subclasses, screensaver | `ContextMenuScene` takes a table; the 16 subclasses become tables + callbacks |
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

*Amended through E1–E3.* The cap counts **nodes that point at a custom item**, not distinct
`CustomItem` instances (§11), and it is `kMaxCustomNodes == 56` rather than 10 — see §11's account of
why 10 was never achievable. The duplicate-name rule gained a co-visibility qualifier (§11). As of
**E3 task 7 the walk over `kAllRoots` with default options reports zero `PlaceholderCustom`**, and
the option that used to suppress that report is deleted — §15.1.

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
  seed), `LoadingScene`. Each draws a `popup_frame` region and reports
  `oled_transparent(Main) == true` so the scene beneath shows around it.

## 7. Migration order and acceptance

Ordered by payoff ÷ risk; each scene lands behind the shell-as-guest or the inverted
shell per spec F:

1. `MenuScene` + the settings / sound-editor tables — retires `gui/menu_item/` (24k lines);
   per-kind goldens against the legacy renderers before each kind's tables ship.
2. Overlays (popup, console, working, count-in, context menu) — retires `OLED::` compositing.
3. `BrowserScene` family — nine UIs → one. *Amended, E3: this splits.* E3 ports the **two the menu
   tree reaches** (the sample browser and the MIDI device-definition loader) as
   `browser::BrowserScene` plus a `Descriptor` table; the other seven — the load/save family for
   songs, presets, patterns and kit rows — are E4's, because they are reached from **root UIs rather
   than menu items** (so the menu tree's legacy-vs-port oracle cannot exercise them) and because they
   carry `SlotBrowser`'s brand-new-name axis this scene has none of. See §15.7.
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
- `Binding` gained **`ValueStyle style`** (`deluge::ui::oled::ValueStyle`, `node.h:169`) — the
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
flag (`node.h:255`, set via `with_range_dependent(...)`, ported from legacy's
`MenuItem::isRangeDependent()`), not by "any node whose `Binding` carries a param" as an earlier
pass assumed — only the ten osc-subtree nodes legacy actually marks (sample transpose, loop point,
the file selector, the audio recorder, …) carry the flag; `validate_tree`'s
`expected_range_dependent` parameter (`validate.h:141`) audits the flag is set on exactly that set
and no others.

**`Binding::Scaling`** — `Binding` gained an explicit `Scaling{Standard, HalfPrecision, Pan}`
(`node.h:103`, `bindings.h:16-23`): legacy's default integer scaling (`Scaling::Standard`) is not
universal. Missed in an earlier pass and caught reviewing the port against
`value_scaling.cpp:36` — **`Scaling::HalfPrecision`** covers osc pulse width and the compressor
params, whose legacy scaling halves precision to make room for negative stored values
`Scaling::Standard` cannot represent; **`Scaling::Pan`** covers the two pan params, which
`bindings.cpp:207-214` additionally infers as a backstop when a node's `ParamRef` names one of them
and no scaling was set explicitly.

**`Binding::updates_reverb_params` and the `ReverbUpdateWithHook` validator rule** — a node whose
write must additionally poke `AudioEngine::reverb`'s live parameters (the sidechain/reverb params,
`sound_sidechain.h:14`, `roots.h:44`) sets `Binding::updates_reverb_params` (`node.h:189`);
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
(`node.h:65-73`) — seven, not the draft's twelve. `Action` is re-added in E2 with its first real
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
  (`secondLayerShortcutsToggled`, `sound_editor.cpp:1364-1379`), **not** "shift held" — shift is
  only the gate that lets a pad be treated as a shortcut at all (`sound_editor.cpp:1169`); which
  layer's cell to use is the caller's business (`shortcuts.h:73-78`).
- Two deliberate press/blink asymmetries, both legacy's own and preserved rather than "fixed":
  **`kMidiExtraCells`** (`shortcuts.cpp:244`) resolves four pads on the MIDI/CV and drum roots that
  are in no legacy table at all (`sequenceDirectionMenu` and the MIDI/CV randomizer trio,
  `sound_editor.cpp:1337-1357`) — they navigate on press but never blink, because the blink side
  (`updatePadLightsFor`) only ever scans the shortcut tables' own column 11. The **`KitGlobalFx`
  press gate** (`kit_global_fx_press_gate`, `shortcuts.cpp:255-263`) is the mirror: legacy's press
  arm is gated `(x <= kDisplayWidth - 2) || (x == 15 && y == 1)` (`sound_editor.cpp:1208-1212`), so
  column 15 is admitted only at `y == 1` — the table's own `(15, 2)`/`(15, 3)` cells are unreachable
  by press yet still blink, since the blink scan has no such guard.

**Blink cadence** — the shortcut-pad blink is legacy's **200 ms two-phase cycle**
(`menu_scene.h:70-72`, `menu_scene.cpp:873-875`): a 20 ms re-arm on the source-pad phase and a
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

*It changes shipping firmware behaviour, and there is a migration consequence — for **both**
colours, not just ONCE* (fix wave, minor 4: the changelog said ONCE and stopped there). Trace the
buggy loader's steady state, `readSettings` then `writeSettings`:

  * `readSettings` assigned `fillColourMenu.value` from byte 165 and then **again** from byte 166,
    so FILL ended each boot holding ONCE's stored byte; `onceColourMenu.value` was never assigned
    at all and kept its static-init RED.
  * `writeSettings` then stored `buffer[165] = fillColourMenu.value` — i.e. the previous boot's
    byte 166 — and `buffer[166] = onceColourMenu.value`. So **byte 165 converged onto byte 166**,
    one boot behind.

After the fix byte 165 is read into FILL and byte 166 into ONCE, so an upgraded user can boot with
**ONCE = RED** *and* with **FILL showing whatever ONCE last stored**. The both-red guard
immediately below the branch fires only when the two collide on RED — in which case it resets FILL
to AMBER and ONCE to MAGENTA, which is a third possible outcome rather than a fix for the first
two. All of it is self-healing the first time the user sets each colour, and all of it is stated in
the changelog where a release note will find it.

**`selectButtonPress()` chaining, and this is the whole list** — legacy's "select on
this leaf opens *that* menu" is a per-item `MenuItem* selectButtonPress()` override returning
another `MenuItem*`; `SoundEditor` navigates to whatever comes back
(`gui/ui/sound_editor.cpp`'s select handling). E1 had no `Node` spelling for it at all:
`MenuScene::after_render` ascended on **any** leaf select press (`menu_scene.cpp:1094-1098`, which
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
test they already had.

*The dump's cycle handling changed in E2 task 9, and the earlier "fails loudly" wording is
superseded.* Task 3 built `fail_on_chain_cycle` to stop on a cycle, on the reasoning that a cyclic
menu graph would be a porting mistake. It is not: legacy has a genuine 2-cycle — from
`patch_cable_strength::rangeMenu` a select press opens `source_selection::rangeMenu` and straight
back again — so the dump would have refused to emit the shipped tree. It now emits a **`"ref"`
stub** for a back edge: an entry carrying only the id of a node already emitted in full earlier in
the walk. Verified against the committed JSON as 118 stubs, every one an ancestor on its own path,
every ref id resolving to a full entry seen earlier, walk terminating, output deterministic. Because
that removed the net that said "you added a cycle", the ref-producing **edge set** is pinned by a
spec instead, and it expects exactly **two** entries — both directions of the single legacy loop,
since the tree reaches each end first from somewhere. An unintended new cycle names the offending
edge rather than merely moving the JSON.

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
`maybeGetParentMenu`/`focusChild` reparenting (`sound_editor.cpp:407-413`), which enters a
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
`performance_session_view::EditingMode` (`performance_session_view/editing_mode.h:50-95`),
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

**Correction (F3 task 3): the paragraph above is right about the default and wrong about the
overrides, and "the port matches without needing a rule for it" must not be read as covering them.**
`allowToBeginSessionFromHorizontalMenu()` defaults to false (`menu_item.h:314`), and for a value
column that is the whole story — everything above stands. But **five items override it to `true`**,
and every one of them is a scene opener: `FileSelector` (`gui/menu_item/file_selector.h:35`),
`osc::AudioRecorder` (`gui/menu_item/osc/audio_recorder.h:76`), `sample::Start` / `sample::End`
(`gui/menu_item/sample/start.h:25`, `gui/menu_item/sample/end.h:25`) and
`audio_clip::SampleMarkerEditor` (`gui/menu_item/audio_clip/sample_marker_editor.h:34`). For those,
`handleItemAction`'s guard is false and it falls through to `soundEditor.enterSubmenu(menuItem)`
(`horizontal_menu.cpp:677`) — **legacy opens the browser, the recorder or the marker editor straight
from the horizontal page.**

The port has no equivalent of the bit. `Column::is_submenu` is `kid.kind == Kind::Submenu`
(`scenes/menu/menu_scene.cpp`'s `render_horizontal`) and `columns` claims the press only for a
submenu column (`ui/oled/columns.cpp:419-423`), so a select press on the FILE BROWSER column of
`source0` raises the notification and opens nothing. Combined with the port's fixed `Node::layout`
(legacy renders these pages vertically when the `HorizontalMenus` feature is off,
`horizontal_menu.cpp:596-601`), the consequence is that **a file selector is reachable only by the
shortcut pad**, which lands it at depth 0. Both are carried rows in `src/scenes/README.md`; neither
is fixed, because closing either moves every horizontal golden. Recorded here rather than left as an
omission because this passage names the symbol and reads as settled, which is worse than silence.

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
`validate.h`'s `kPendingCustomNodes` (**11** at the time) and a by-identity assertion of *which*
nodes those are: a count alone cannot tell "a placeholder was finished" from "a placeholder was
finished and a new one appeared somewhere else", which is exactly the drift a number meant to say
"E2 is not finished" must not be able to hide. The eleven were the file/sample family — both
oscillator file browsers, the four sample loop points, the two audio recorders — plus
`midi_device_linked`, `specific_output_selector` and `audio_clip_waveform`. All of them need the
card or a view no plan before E3 ports.

*E3 amendment (task 6).* `kPendingCustomNodes` is now **2**, and the distinct-item count is **51**
(the node count and the cap are unchanged at 52 and 53 — nine nodes were re-pointed, not added).
Task 6 gave nine of the eleven real items in `menu/nodes/sample.h`: `FileBrowserItem` over
`browser::BrowserScene` (`BrowserId::Sample`) for the two file selectors, `MarkerItem` over
`marker::MarkerScene` for the four loop points **and** `audio_clip_waveform` (which is the
`MarkerType::END` spelling — `gui/ui/menus.cpp:1183` is the global `audioClipSampleMenu:1193`
names), and `AudioRecorderItem` over `recorder::RecorderScene` for the two record-to-sample items.
Nine instances rather than three, for `MidiCommandItem`'s reason: `on_focus` is handed a
`MenuContext` and a `MenuState`, never its own `Node`, so an item shared across the two oscillators
could not tell which source it was being focused as.

The **two** that remained were `midi_device_linked` (whose `BrowserScene` descriptor already
shipped — `BrowserId::MidiDeviceDefinition`, E3 task 3 — so only the item was owed) and
`specific_output_selector` (an output picker, not a browser).

*E3 amendment (task 7) — the tree has no placeholders left.* `kPendingCustomNodes` is **0** and the
distinct-item count is **52**, which is also the node count (`kShippedCustomNodes`) and the cap is
unchanged at **53**: two nodes were re-pointed, not added, and the shared `nodes::kPending` left the
distinct-item set with them, so 51 became 52 rather than 53. Every `Kind::Custom` node in the tree
now has an item of its own and no two share one. The two items are in `menu/nodes/output_items.h`,
grouped because both are about the clip's `Output` rather than its sound:

* **`MidiDeviceLinkedItem`** — `midi::device_definition::Linked`
  (`gui/menu_item/midi/device_definition/linked.h:29`), which is a **`menu_item::Toggle`**, not a
  plain opener: its value is "is a definition file linked" (`readCurrentValue`, `:33-36`) and
  `writeCurrentValue` (`:37-49`) has two arms — ON opens `loadMidiDeviceDefinitionUI` (`:42`), OFF
  clears `MIDIInstrument::deviceDefinitionFileName` (`:45-48`). Both are ported. `Toggle` overrides
  `shouldEnterSubmenu()` to false (`gui/menu_item/toggle.h:19`), so the port takes
  `nodes::ActionItem`'s shape for that legacy shape: descend for one tick, act in `on_focus`, and
  leave again — via `pending_scene` + `go_up_on_resume` on the ON arm, and via `render` reporting
  `entered` on the OFF arm.
* **`SpecificOutputItem`** — `audio_clip::SpecificSourceOutputSelector`
  (`gui/menu_item/audio_clip/specific_output_source_selector.h:26`), a page rather than a browser:
  the song's output list walked with the select encoder, skipping every output the audio track
  cannot record from (`AudioOutput::canRecordFrom`, `processing/audio_output.h:109-111`, which
  excludes itself, MIDI and CV). Its cursor lives in `MenuState::list[depth]` for
  `SourceSelectionItem`'s reason, and the `setOutputRecordingFrom` write goes through
  `CustomItem::commit`, since a render may not write the model.

`ValidateOptions::allow_placeholder_custom` is **deleted**, in this task rather than in the plan's
accounting task: with `kPendingCustomNodes` at 0 it could never suppress anything again, and a
permanently-satisfiable opt-out is the shape that hides the *next* regression rather than
documenting this one. `Issue::Kind::PlaceholderCustom` is therefore unconditional, and
`nodes::kPending` stays as a live mechanism with no users —
`sim/scenes_spec/output_items_spec.cpp` asserts the zero three ways (the constant, a strict
`validate_tree` over `kAllRoots`, and a walk of every reachable `Kind::Custom` node).

Two divergences are recorded rather than closed: a `Kind::Custom` node cannot draw a row icon in its
parent's list, so `midi_device_linked` loses `Linked::renderSubmenuItemTypeForOled`'s checkbox and
file name (`:56-78`) — it must stay `Kind::Custom` because the dump JSON carries the node's type,
and closing it needs a `CustomItem` hook `MenuScene::render_oled`'s `row_icon` can ask.

*E3 amendment (task 7, fix round 1) — `CustomItem::on_focus_is_press` and `enter_leaf`'s
`FocusCause`.* The first cut of this task recorded, rather than fixed, that `on_focus` doing the
toggle meant a `restore_state` onto the item toggled too. That was wrong: it is a defect, not a
divergence, and the `ActionItem` analogy that justified it does not hold. The E2 ruling that blesses
re-firing `on_focus` on a restore is about a *scene-opening* item and cites legacy **agreeing**
(`EditName::beginSession` opens `RenameUI` unconditionally); here legacy **disagrees deliberately** —
`Toggle::beginSession` does not toggle, and `Toggle::selectButtonPress` carries a targeted guard,
`if (soundEditor.getCurrentMenuItem() == this) return nullptr;` (`gui/menu_item/toggle.h:35-37`),
whose condition *is* "we are already focused on this item", i.e. precisely the restore case. Both
paths are live: `SceneStack::replace_root` saves the menu parked on this leaf while `BrowserScene`
sits above it (`MenuSavedState` does not carry `go_up_on_resume`, so the restore lands on the leaf),
and `retarget`'s depth-0 deep link does the same.

The lost distinction is legacy's own: `selectButtonPress()` *acts*, `beginSession()` only
(re-)initialises, and the port collapsed both onto `on_focus`. It is restored in two pieces, because
neither works alone:

* **`MenuScene::enter_leaf(node, FocusCause)`** — `RowPress` from `descend_to` (the one path with a
  select press behind it), `NoRowPress` from `retarget`, `restore_state` and `ascend`.
* **`CustomItem::on_focus_is_press()`** — the scope. A blanket skip on `NoRowPress` would regress the
  shipped, specced ruling above; a flag with no cause could not tell a press from a rebuild. The
  line is "does it act without a press", not "does it do anything": `EditNameItem`,
  `ContextMenuItem` and the nine `nodes/sample.h` items keep it false because their act is opening a
  scene the user can back out of, and their legacy classes do it in `beginSession`.

`enter_leaf` runs `on_focus` unless the cause is `NoRowPress` **and** the item declares itself a press.
Two items declare it: `MidiDeviceLinkedItem` and `ActionItem`.

**This also closes a pre-existing exposure the fix was not originally about.** At depth ≥ 1 the
action drain ascends inside the same `after_render`, so a save can never capture an `ActionItem` as
the parked leaf — but the depth-0 deep link could, and re-filed `StartStemExport` /
`SetClipLengthToSampleLength` / the two song verbs with nothing pressed. `menu_actions_spec.cpp`'s
"opened as a root, files its action" example asserted that behaviour and is **reversed**: a deep link
onto one of these now shows the item's title and does nothing. `PerformEditorItem` is untouched — it
files from `render`, on a press it observes itself, which is the same discriminator `after_render`'s
ascend arm already used to tell the two shapes apart.

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
`CustomItem::is_placeholder()` / `Issue::Kind::PlaceholderCustom`, the last of these with no
suppressing option since E3 task 7; the `DELUGE_SCENES_E1_PARTIAL` macro the plan
proposed was never built); and gold-knob acceleration
(`calcNextKnobSpeed`).

*E3 amendment (task 8) — the count moves for the first time by* addition. The per-device MIDI page
(`menu/nodes/midi_device.h`, the carry-list row below) adds **three** bespoke items —
`MpeDirectionItem`, `MpeZoneItem`, `MpeZoneChannelsItem` — so the node count and the distinct-item
count are both **55** and the cap is **56**, still exactly one above. Unlike every E3 addition
before it, these are new `Kind::Custom` *nodes*, not placeholders re-pointed at a real item: the
page did not exist. All three are bespoke for one reason — their value is `MenuState` navigation
state (`mpe_direction`/`mpe_zone`, legacy's two menu-item members that a third page reads), which a
`Binding`'s `get`/`set` hook, handed only a `const MenuContext&`, cannot see.

*The `MenuState` vs `MenuContext` rule, stated operationally (task-8 review, Important 1).* It is
tempting to summarise the placement as "legacy stored it on a menu item, so it is `MenuState`", and
that is **wrong as a rule** — it is only a proxy that happens to agree here.
`MenuContext::midi_cable` is the counterexample: a `soundEditor.current*` member, so context, that is
nonetheless *written* by a menu item (`midi::Devices`) through a one-shot mailbox
(`MenuState::midi_cable_choice` → `MenuScene::after_render`). Who writes a value says nothing about
where it lives. Two questions decide it, in this order:

1. **Can a `Binding::get`/`set` — handed only a `const MenuContext&` — ever need to see it?** If
   yes it must be a `MenuContext` field, whatever legacy called it and whoever writes it; a menu
   item that has to set it does so through a mailbox. `midi_cable` answers yes (the per-device
   page's four *data* nodes read it); `source_index` answers yes (`gate_mode`'s hooks read it).
2. **Does it die when the walk is re-rooted?** If yes it is `MenuState`, and `retarget` clears it
   along with the rest of the walk.

`mpe_direction`/`mpe_zone` answer **no** to (1) — their only readers are `CustomItem`s, which are
handed `MenuState` too — and **yes** to (2), so `MenuState` is right and the three items are
necessarily bespoke. A later port of a menu-item member that a **data** node must read fails (1) and
belongs in `MenuContext` with a mailbox, exactly as `midi_cable` does. Recorded because the
one-line proxy would give that port the wrong answer. The direction selector
is additionally bespoke because `DirectionSelector::getTitle()` is computed from that value
(`gui/menu_item/mpe/direction_selector.h:40-43`) and a `Node::title` is one static id. The page's
other four children are ordinary data nodes over a `get`/`set` family scoped to
`MenuContext::midi_cable`. Three rows of the `selectButtonPress()` sweep's group (b) above are
ported with it: `midi::Devices` → `midiDeviceMenu`, `mpe::DirectionSelector` → `zoneSelectorMenu`
and `mpe::ZoneSelector` → `zoneNumMemberChannelsMenu`.

---



## 12. Amendments closing E2 (2026-08-30, Tasks 1–13 of `2026-08-30-ui-scenes-menus-2.md`)

E2 finished the item port: every legacy menu item that needs no card access now has a real
implementation. This section records the rulings that changed the design above, each with what it
cost and what it would cost to reverse. The E3 carry list is §13.

### 12.1 `Kind`: the ≥3 rule applied in all three directions

§4's line 95 states the rule verbatim: *a kind exists only if ≥3 items use it*. E1 task 8 used it to
delete five speculative kinds at zero consumers. E2 applied it three more times, and two of those
went against the plan that proposed them.

**`Kind::Info` was created and deleted inside one task (task 2).** The plan added it to carry the
two read-only text pages, `firmware_version` and `battery_level`. Two consumers is under the bar —
so it was under the bar the day it was written into the plan, and the spec, not the plan, is the
authority. A second reason confirmed the first rather than standing alone: `InfoFn` was a stateless
function pointer, and legacy's battery item derives "is charging" from a voltage delta held **on the
item instance** across timer ticks (`gui/menu_item/battery/level.h:57-61`). The first implementation
therefore dropped legacy's `" CHG"` suffix — design dictating behaviour, the wrong way round for a
port whose whole premise is legacy parity. Both items are `CustomItem`s, which may hold `mutable`
state exactly as legacy does, and the suffix came back. *Cost if wrong:* two small `CustomItem`s
instead of one kind; if a third read-only text item appears in E3 the kind returns with three
consumers, which is what the rule is for. The custom-input plumbing that task built — the valuable
half, and what every later item group stands on — is untouched by this.

**`Kind::Range` returned at exactly three (task 11b).** `default_tempo`, `default_swing_amount` and
`default_key` are legacy's `menu_item::Range` and its two subclasses (`gui/menu_item/range.h:30`,
`integer_range.h:25`, `key_range.h:24`). Three is the bar, not a near miss, and the same rule that
deleted `Range` as speculative in E1 is the rule that brought it back. `Binding::get`/`set` are the
**lower** edge; `Binding::range_spec` carries the upper edge's hooks and the two per-item rules (how
the ends spell themselves; what happens when they would cross — `Push` drags the other end, `Bound`
*is* the bound). The consumer set is pinned by node identity in a spec, so a fourth consumer or a
lost third is a deliberate change rather than drift.

**The six pad colours need no `Kind::Colour` (task 11).** This was expected to be the kind's first
real consumer. It is not: legacy's `menu_item::Colour` is a plain `Selection` over nine options plus
a rendering side effect, so the port's six are ordinary `Kind::Enum` nodes with get/set hooks. The
lesson for §4 is that "there is a legacy class named after it" is not the test — three consumers
with a shape no existing kind expresses is.

### 12.2 `CustomItem` grew an interface, not just implementations

§4's sketch gives `CustomItem` a render hook and nothing else. E2 added, in the order the item
families needed them:

* **Input dispatch (task 2).** `handle(UiEvent, UiContext, const MenuContext&, MenuState&)`, called
  by `MenuScene` **before** the shortcut-pad jump and the root `Back` — the precedence legacy gives
  a `MenuItem`'s own `buttonAction` over the rest of `SoundEditor::buttonAction`
  (`gui/ui/sound_editor.cpp:607 buttonAction`). Without this every bespoke item was render-only, so
  this is the prerequisite the whole rest of E2 stands on.
* **`on_focus` (task 2, review fix).** The port of `beginSession`. Added because
  `BatteryLevelItem`'s charging baseline never reset on re-entry: legacy resets
  `lastBatteryMV`/`voltageCheckCounter` in `beginSession` every time the item is focused, and the
  port set its flag once for the whole process, so a second visit computed a delta over an arbitrary
  real-world gap and could show a spurious `CHG`. The ruling was to add the missing lifecycle hook
  rather than document the deviation, because `beginSession` is a general concept that ~45 ported
  items use — `midi::Command::beginSession` and `midi::Devices::beginSession` both need it, and
  bolting it on in a later task would have left every item written in between with no way to
  initialise. It returns `void`, and must: it is also reached from `retarget()` and
  `restore_state()`, neither of which has a `Result` to put an `Action` in.
* **The learn virtuals (task 8).** `learn_note_on`, `learn_cc`, `learn_program_change`, `unlearn`,
  `wants_learn_blink`, `learn_descriptor`, all with inert defaults, plus the matching
  `MenuScene::learn_*` entry points (which match `sound_editor.cpp:1605-1625`, including
  `noteOnReceivedForMidiLearn` having **no** UI-mode gate). When this was written **nothing routed
  MIDI into the scene stack**, so these were specced at the item level and uncovered at the routing
  level. **F1 task 10 closed two of the three**: `ShellHostUI` overrides
  `noteOnReceivedForMidiLearn` and `pcReceivedForMidiLearn` and forwards to `MenuScene::learn_note_on`
  / `learn_program_change`, covered by `sim/scenes_spec/menu_entry_spec.cpp`'s
  `menu_entry_midi_learn` group. `learn_cc` is still unrouted and cannot be routed without adding a
  `UI` virtual, because `PlaybackHandler::midiCCReceived` tests `getCurrentUI() == &soundEditor`
  directly (`playback/playback_handler.cpp:3164`); and every one of the three is inert until
  something under the guest sets `UI_MODE_MIDI_LEARN`, which nothing does — see migration §10.2
  item 8. `bindings::learn_target` is a different path — PARAM learn, not global-command learn — and
  now says so on its declaration.

### 12.3 The action seam (`menu/action.h`, task 11b)

`MenuState::pending_scene` (task 5) is the **noun** half: an item names a scene, and
`MenuScene::after_render` — the first hook after `enter_leaf` that has a `Result` — turns it into an
`Action::Kind::Push`. It works because every scene a menu opens is inside `src/scenes`.

Five items are not like that: `perform_editor`, `start_stem_export`, `set_clip_length`,
`song_midi_learn` and `configure_song_macros` each run a piece of legacy **root-UI** behaviour.
Every one names a symbol in `gui/views/`, `gui/ui/` or `gui/context_menu/`, which this layer may not
reach — and two of the five open legacy `ContextMenu` subclasses this plan has not ported, so even
`pending_scene` could not spell them. The layer rule is not incidental to the seam, it is its
justification, and it is now machine-checked: `scenes-no-root-ui` in `scripts/check_layer_rules.py`
is a **symbol** row rather than an include row, because this library routinely names a legacy global
through a bare `extern` declaration instead of its header (precisely because the include rule forbids
the header), which an include row cannot see.

The verb half mirrors the noun half: a closed `MenuAction` enum, a `MenuActionRequest` carrying it
plus one `uint16_t` payload for the one action whose meaning is not fixed by its identity, a
`MenuState::pending_action` field, and a drain **where `pending_scene` drains**. Identity is the
strong part: distinct per item, exhaustively switched at the drain, mapped by identity rather than
position, so a dispatcher cannot mis-map it and a sixth enumerator breaks the switch loudly.

**`take_action()`'s contract, stated because it is a limit and not an oversight.** It is a
**one-slot mailbox with a silent overwrite**, and nothing obliges per-tick polling. Two requests
filed between two polls lose the first. It is sufficient today because a menu item cannot file two
actions in one tick; it stays sufficient only while the wiring plan's dispatcher polls every tick.
A dispatcher that wants to be lazy must turn the slot into a queue first. **Nothing performs any of
these actions** — `menu_actions_spec.cpp` asserts that the right item emits the right identity and
payload through the real input path, and cannot assert that anything happens as a result.

### 12.4 Three toolkit widgets, and why each was a widget rather than a relaxation

`scenes-no-frame-primitives` forbids scene code from calling `f.rect`/`f.hline`/… directly. Three
times in E2 a page needed pixels the toolkit had no widget for. Each time the answer was a widget;
relaxing the rule was considered and rejected each time, because it would reopen the boundary for
the whole library to serve one page.

* **`ui::oled::text_edit` (task 4)** — `QwertyUI::drawTextForOLEDEditing`. It is render-only: the
  horizontal encoder belongs to `RenameScene::handle` under `frame_context.h`'s reading-is-claiming
  rule. Its non-obvious arm is the **invert-to-end** presentation, which is not predictive-text
  highlighting but the cursor itself whenever the cursor is mid-string (the `atVeryEnd` false arm),
  with the blinking bar used only at the end. Dropping it would have shown an underline where legacy
  shows an inverted tail in the ordinary editing case — and it is expressible model-free, since it
  depends only on cursor, scroll, the rect and `Style::invert_highlight`, which the toolkit already
  models.
* **`ui::oled::option_window` (task 6)** — the context-menu box. It is a separate widget from
  `ui::oled::list` on three measured differences, all verified against legacy: label inset 1 vs
  `style.inset_x` = 6; highlight extent 106 vs a value derived from the label's own rect (101); and
  the cursor pinned to the **bottom** row where `list_window_start` centres it. Parameterising
  `list` would have meant two rects, an external window start, and suppressing its
  `claim_turn`/`claim_select`/`move_cursor` — i.e. disabling most of what `list` is.
* **`ui::oled::routing_chain` and `segmented_switch` (task 9)** — the patch-cable strength page's
  arrow diagram and BI/UNI box. The information was all present without them, so this was not data
  loss; the argument for adding them is that this is the one page in the tree whose job is making a
  routing legible at a glance, and arrows carry "A modulates B" pre-verbally where a title line makes
  the user parse a string.

Two consequences of the third were recorded rather than fixed, and the fix wave has now closed both.

The *value* went first. At the toolkit's 20 px face the number clipped its last digit at five
glyphs, and the range is ±50.00, so every value ≥ 10 clipped and every negative was worse at six —
so task 12 adopted legacy's own 13 px face, right-aligned three pixels off the panel edge.

The *switch position* was upheld at the time, on the grounds that legacy's absolute canvas row 34
would fall under the port's digits and that buying pixel fidelity on one page costs cross-page
consistency. **Overturned, and the ruling was already dead when it was upheld**: it rested on the
premise that legacy's 13 px right-aligned face was unavailable, and task 12 had just adopted exactly
that face. What the page then had was legacy's face for the number and the toolkit's position for
the switch — the one arrangement neither argument supported.

The fix wave resolved it by finishing the move. The obstruction was never the switch: it was that
`decimal_value` hard-coded the **canvas-absolute rows 20 and 41**, which are
`Decimal::drawPixelsForOled`'s constants for the *full-panel number page* and have nothing to do
with this one. `patch_cable_strength.cpp` uses its own — `textY = extraY + OLED_MAIN_TOPMOST_PIXEL +
4` (canvas 10) and the cursor at `textY + digitHeight + 1` (canvas 24) — precisely so the switch
fits underneath at `startY = 34`. Inheriting row 20 from the wrong page is what pushed the switch
above the number. `DecimalLayout` now carries the rows as well as the face
(`ui/oled/values.h`), expressed **relative to the caller's rect** so the page stays frame-local as
one piece, and the page reproduces legacy's three constants exactly on the reference geometry:
measured off the re-recorded 48-row golden, the digits occupy canvas rows 10–22 and the switch's
bracket rule lands on canvas row 32 with its slot band at 34–43. The side effect is that the page's
two halves no longer follow different coordinate systems — the note that used to record "the gap
between the diagram and the value differs by five rows between the two geometries" is gone, because
it no longer does.

### 12.5 Two new scenes

* **`rename::RenameScene` (task 5)**, `SceneKind::FirstApp + 1`. One scene in place of legacy's
  five-class `gui/ui/rename/` directory: the four subclasses differ only in `getCurrentName()`,
  `trySetName()` and `allowEmpty()`/`canRename()`, so those become a `rename::Target` table indexed
  by `SceneId::arg`. **Three** targets, not four — `renameMidiCCUI`'s only opener is
  `gui/views/automation_view.cpp:1663` and no `Node` reaches it, so it is deliberately absent rather
  than shipped untested. Rejection is popup-and-stay-open via `Action::Kind::Popup`, matching legacy,
  because no `Severity::Warn` path exists (notify's raise helpers take no severity).
* **`context_menu::ContextMenuScene` (tasks 6, 7, 11b)**, `SceneKind::FirstApp + 2`. §6's sketch is
  confirmed: one scene plus a `Descriptor` table, `DescriptorId` in `SceneId::arg`. Six descriptors
  ship — the five `reset_settings::Reset` actions and `audio_clip::AudioSourceSelector`, the last
  deliberately deferred from task 6 until `Descriptor` grew the three hooks it needs. Corrections to
  the draft worth carrying: `ResetSettings`' options are `{OK}` under the title "Are you sure?", not
  `{NO, YES}`; the box is a fixed 100×40 square, not a computed popup rect; the title is a plain left
  `drawString` at x 22, not `ui::oled::title`.

### 12.6 `dynamic_children` fits none of the runtime-list pages

§4's `Node` sketch carries `size_t (*dynamic_children)(...)` with "DX presets, MIDI devices…" beside
it, and E1's dump fails closed on any node that sets it. E2 task 10 went looking for the first real
consumer among the three pages that obviously wanted one — the MIDI device list and the CV and gate
output selectors — and found that **none of them can be one**. Two independent reasons:

1. **Labelling.** A submenu row is labelled from `Node::name`, one static `uint16_t` l10n id; a
   device row's label is a runtime `getDisplayName()` string. `dynamic_children` returns *pointers to
   `Node`s*, so N runtime-labelled rows would need N pieces of `Node` storage varying at runtime, in
   a `constexpr` table layer that allocates nothing. A name hook does not rescue that.
2. **Shape.** `midi::Devices` is `class Devices final : public Value<int32_t>` — a **leaf** whose
   value is an index. `drawPixelsForOled` paints the labels itself and `selectButtonPress` returns
   *one* destination for every row, so N rows chaining to the same page is structurally wrong for
   `dynamic_children` before labelling even enters it. CV and gate are hard-coded `Selection`s over
   compile-time channel counts.

All three are therefore `CustomItem`s (`menu/nodes/dynamic.h`), the dump's fail-closed arm stays, the
committed JSON is unchanged, and a spec now pins the `dynamic_children` count at **zero** so the arm
guards a case nothing produces. The shape that *would* work — a static "may-yield" span on `Node`, in
`chain_targets`' shape — is written where the fail-closed comment sits. A further reason not to force
it: the dump has no `MenuContext` at all (E1 task 10 removed its fixtures), so expanding dynamic
children there would make the committed JSON a function of `MIDIDeviceManager`'s globals.

### 12.7 Two edits to legacy source, in a plan whose safety argument is that legacy stays put

This plan's whole comparison method is that `gui/menu_item/**` is an independent oracle. Editing it
is therefore a thing to justify twice, not once.

**The `Colour::value` move (task 11).** A member of a `gui/` class is unreachable by declaration from
`src/scenes`, so `menu_item::Colour::value` moved into `FlashStorage::padColours` with `value` left
as a reference onto it. The risk is that port and legacy now read and write the same storage *by
construction*, which would hollow out the comparison. It does not: `Option` was already
`enum Option : uint8_t`, so the type and width are unchanged, there is no aliasing or init-order
hazard, and every call site is unchanged in meaning. It does make the *value* comparison degenerate —
both sides write the same byte — but legacy's write is a bare store with no transformation, so
nothing was being compared away; and the shared byte is used as the **mechanism** for a stronger
test, which drives one node, requires `moved_count == 1` across the six legacy items, and asserts the
observed permutation is the identity while a transposition of it is not. Shared storage was already
the pattern here (`PadLEDs::flashCursor`, `runtimeFeatureSettings`).

**The `readSettings` byte-166 fix** is recorded in §11 above, including why it was patched where
`AudioRecorder::isRelevant` was only recorded, and its migration consequence — which is a *pair* of
consequences: an upgraded user can boot with ONCE = RED and with FILL showing the colour ONCE used
to be, because byte 165 converged onto byte 166 under the bug. Self-healing the first time they set
each colour.

### 12.8 The host BSP's flash became RAM-backed

`readSettings`/`writeSettings` turned out to be **untestable, not untested**: the host BSP's flash
stub read `0xFF` and dropped writes, so no spec could observe a settings round trip and the byte-166
bug had no regression net. It is now RAM-backed — 256 bytes, one sector. The properties that matter
for every *other* spec were checked and hold: the region still boots erased (`memset 0xFF` on first
use), reads past it still return erased, and erase resets the init flag — so every process still
takes `readSettings`' "no settings saved" arm and nothing that only reads changes behaviour.

### 12.9 `ascend()` re-seeds `beginSession`, and the arm it deliberately omits

`MenuScene::ascend()` never re-ran the equivalent of `beginSession` on the leaf it lands on, unlike
legacy's `goUpOneLevel` → `beginScreen`. Returning from a chained value page therefore drew and
edited the **child's** value under the parent's title. Found by E2 task 12's acceptance layer, which
is what an acceptance layer is for: no unit spec covered a return path.

The fix excludes `Kind::Custom` deliberately — including it broke the CV selector, which a level-2
script caught. That exclusion is sound only while every chain-source custom item's backward behaviour
is "keep what `MenuState` already holds", which is true of all of them today (they write disjoint
`MenuState` fields) and **untested**. It stands in for a missing `CustomItem::on_focus_backward`:
`on_focus` is the forward arm only. An item needing a different answer on the way back needs
`CustomItem` to gain that parameter, not this exclusion relaxed. Carried to E3.

### 12.10 The auto-repeat bug, and why its unit spec was structurally blind

Legacy auto-repeats a held backspace (`gui/ui/qwerty_ui.cpp` arms 500 ms, then re-arms at 80 ms), so
task 5 ported it into `ui::grid::text_keyboard`'s `Response::held_index` and specced it from both
sides of both constants. **It never fired at a real tick cadence.** The shell runs `after_render` on
OLED-only ticks where `held_index` is `-1`, which disarmed the hold at the blink half-period (~110
ms) after every press.

The spec passed not by luck of tick placement but because it was **structurally blind**: a
`RepaintAll` was in flight after every press, arming both gates at 0 ms, so every tick the spec
placed was a grid pass. A unit spec that drives its own ticks can only see the cadence it creates.
The general lesson, and the reason this is an amendment rather than a bug report: **anything
time-driven needs coverage at the runner's real cadence, not at hand-placed ticks** — the level-2
runner ticks unconditionally every 10 ms, which is why the fix is phase-independent and why the
regression now lives there. Two related facts fell out of the same investigation and are worth
keeping: the two surfaces' repaint paths are **coupled** through `rendered_this_tick_`
(`after_render` runs only for a scene that rendered on *either* surface), so the OLED blink wake was
keeping the grid-driven repeat alive; and neither `text_edit` nor `text_keyboard` can arm a
`RepaintGate` (both are render-only and own no clock), so the *scene* must arm both.

## 13. E3 carry list

Held here so the next plan's author finds it in the spec rather than in a ledger. `src/scenes/README.md`
carries the same table with pointers into the code.

| carried | why it was not done | size |
|---|---|---|
| ~~**The file/sample family** — 11 nodes still `nodes::kPending`~~ **CLOSED by E3 tasks 6 and 7.** (Accurate as E2 history: at the close of E2 these eleven were still placeholders. `kPendingCustomNodes` is now **0** — see the E3 amendments in §11.) | needed the card; every other item family was reachable without it | was the largest remaining group. `validate.h`'s `kPendingCustomNodes` names the exact set, pinned by node identity so closing it was a deliberate spec change |
| ~~**`midiDeviceMenu`'s subtree** (`gui/ui/menus.cpp:1444-1453`)~~ — **CLOSED by E3 task 8** (`menu/nodes/midi_device.{h,cpp}`): eight nodes, the four-hook per-cable family over `MenuContext::midi_cable`, the three-item MPE arm with its two onward chains, and `midi_devices`' own `select_chain`. | was size, and only size. Tasks 3 and 9 each declared a *single* chain-target page; this was several times that | landed as ~8 nodes, 3 bespoke, exactly as estimated. `whichDirection`/`whichZone` went to `MenuState` (not `MenuContext`) by the mapping rule the patch-cable trio set: a `soundEditor.current*` member is context, a menu-item member another page reads is scene state |
| ~~**Splitting session from arranger**~~ — **CLOSED by E3 task 9**, and the costing was wrong twice: it needed **two** bools, not three (nothing distinguishes the arranger from song automation, because no legacy `isRelevant` does), and the call sites were **85**, not ~62. See §15.6. | `MenuContext` cannot tell them apart, and a predicate meaning "either" would be a tautology today (the song root is unreachable from clip-minder contexts) that moved the committed JSON for nothing. It is really a **three-way** conflation: `sound_editor.cpp:1762-1782 setup` selects the song root for `sessionView`, `arrangerView` *and* `automationView` | ~half a day: one `context_for` parameter, one `MenuContext` field, one predicate, an audit of 9 `is_song_view` reads where `bindings.cpp:265` must keep meaning "either", ~62 positional-bool call-site updates, plus a JSON regen since the nodes gain `available_when`. No architectural ripple |
| **Mod-matrix pad blinking** — `PatchCables::blinkShortcuts`/`blinkShortcutsSoon`/`timerCallback`, the three `shouldBlinkPatchingSourceShortcut` overrides and `patchingSourceShortcutPress` | `MenuScene::render_grid` owns one blink cycle keyed off the focused *node*; consulting the focused `CustomItem` needs a hook on `CustomItem` and a second blink source | same shape of change as routing MIDI to `MenuScene::learn_*` |
| ~~**The row-scrolling path is untested, not missing**~~ — **CLOSED by E3 task 10**: a real focused over-long node driven through `MenuScene` itself, mutation-checked against `list.cpp`'s selected-row draw. | every over-long row in the recorded goldens is *unselected*, which legacy truncates too. (Legacy does **not** side-scroll long *titles*: `draw_screen_title` is a plain clipped `draw_string`, so a truncated title is faithful) | one level-2 script parking the cursor on a long row across quiet ticks |
| ~~**Seven self-duplicate goldens**~~ — **CLOSED by E3 task 10**, with a correction: only **five** are duplicates of both halves. `menu_int`'s two match `idle`'s `.pbm` but not its `.state`, so they did record a fact (the shortcut blink appearing) that the kept `after_gold_turn` also carries. Original row: — `menu_int/{after_select_press,after_select_turn}`, `menu_enum/{after_select_press,after_gold_turn}`, `menu_toggle/{after_select_press,after_gold_turn}`, `menu_shortcut/after_select_turn` each equal their own `idle` | pre-existing; each is named for an event it does not show | cleanup. E2 task 9 deleted three of exactly this kind, so leaving seven is an inconsistency, not a policy |
| ~~**`perform_editor`'s owed sync calls**~~ — **CLOSED by F2 task 5**, and the count was right about the view and wrong about the writer set: four calls in `PerformanceView` (`gui/views/performance_view.cpp:844`, `:928`, `:942`, `:1411`) **plus a fifth outside it**, legacy's own `EditingMode::selectButtonPress` (`gui/menu_item/performance_session_view/editing_mode.h:84`), which runs with the toggle OFF and would otherwise leave the mirror rotting until the next flip-on. Migration §10.2 item 6. | the displayed mode mirrors two `PerformanceView` bools, because a class member offers no bare-`extern` escape | five calls into `menu::performance_editing_mode()`, not four. Same failure mode as `set_shortcuts_version`, which F2 task 6 also closed |
| ~~**The action seam's dispatcher** — nothing consumes `MenuScene::take_action()`~~ — **CLOSED by plan F2** (migration §10.2 item 3). `gui::ui::dispatchShellActions` drains it from `ShellHostUI::timerCallback` after every `Shell::tick`. | it is the half that gets to say `openUI(&performanceView)`, i.e. the wiring plan's | one `switch` over **seven** enumerators, not five — F2 tasks 6 and 7 added two settings writes filed through `MenuScene::request_action` — plus the per-tick polling §12.3's mailbox contract demands. Wired for all seven; demonstrated end to end for **one**, because a legacy null dereference crashes the toggle-off leg on the gesture that opens the other six's roots (migration §10.9) |
| ~~**`CustomItem::on_focus_backward`**~~ — **CLOSED by E3 task 10, and "sound today" was false.** The blanket exclusion's "leave `MenuState` alone" is wrong for `gate::Selection`, whose backward arm restores the last real output where the exclusion leaves the cursor on the minimum-off-time row. E3 task 7 added the *other* focus discriminator too, `on_focus_is_press()`, after finding a restore silently clearing a user's linked device-definition file. | it was not sound; see §15.4 | one virtual plus a `FocusCause`, and a second field on `GateSelectionItem` |

Unchanged from §11's list and still owed: `source_shortcut_pads`; per-pad blink frequency masks;
two context-dependent blink suppressions; settings-root shortcut jumps (blocked on
`MenuContext` gaining `background_root`); the four `hid/`-side write side effects; `osc0_type`/
`osc1_type` → `dxMenu` (waits on the DX browser); `Submenu::shouldForwardButtons()` forwarding;
`Automation::selectButtonPress`'s shift-held automation delete; per-node cursor memory across
sessions; and gold-knob acceleration (`calcNextKnobSpeed`).

## 15. Amendments closing E3 (2026-09-02, Tasks 1–12 of `2026-09-02-ui-scenes-menus-3.md`)

E3 closed the menu tree. Every amendment below is an *outcome*, and several correct a premise this
spec or the plan asserted.

### 15.1 §4.4 validation reaches zero, and the opt-out is deleted

`validate_tree` over `kAllRoots`, with default options, reports **zero** `Issue::Kind::PlaceholderCustom`.
`kPendingCustomNodes` is `0`; there is no `&kPending` binder in any node table; the shipped custom
node count is **55**, behind **55** distinct items, so node count and item count are equal for the
first time.

`ValidateOptions::allow_placeholder_custom` is **gone**, and this is the amendment rather than the
count. It existed so an E1/E2 spec could say out loud, in one place, that the tree was not yet
complete. Once the count is 0 it can never suppress anything again — and a permanently-satisfiable
opt-out is precisely the shape that hides the *next* regression while reading as documentation of
this one. It had **eleven** call sites, not the three a stale grep suggested; all eleven were
removed with it. The `kPending` placeholder *mechanism* survives, so a table may still declare a
shape before its item exists; what it can no longer do is ask for silence.

### 15.2 Two extractions out of `gui/`, and why they were not optional

Both of E3's structural moves came from the same fact: **`src/scenes` may not include `gui/`**, so
code the port needed was not merely inconvenient where it sat, it was *unreachable*.

* **`storage::FileCatalogue`** (`storage/file_catalogue.{h,cpp}`, task 1) is legacy `Browser`'s
  listing machinery moved **verbatim** — `emptyFileItems`, `deleteSomeFileItems`, `getNewFileItem`,
  `cullSomeFileItems`, `readFileItemsForFolder`, `deleteFolderAndDuplicateItems`, `sortFileItems`,
  `searchFileItems`. It went to `storage/` and not, say, to a new directory, for two reasons: it *is*
  the file catalogue, and `check_layer_rules`'s own `NON_UI` glob already counts
  `src/deluge/storage/` as non-UI, so the extraction inherited the no-`gui/`, no-`hid/` rules
  without a new rule row. Legacy `Browser` now delegates into it. The only textual difference across
  every moved body is an `interpretNoteNames` parameter replacing
  `shouldInterpretNoteNamesForThisBrowser`, verified by normalised diff against the pre-move commit.
* **`deluge::sample::WaveformScan` / `WaveformNav`** (`model/sample/`, task 4) is the same move for
  `gui/waveform/`, split **arithmetic down, `PadLEDs` up**: the peak scan and the scroll/zoom
  navigator moved (`findPeaksPerCol` 243 lines for 243, `investigateWholeCluster` 73 for 73, and so
  on), `WaveformBasicNavigator` now derives from `WaveformNav` and keeps only the `PadLEDs` zoom
  transition, and the *drawing* stays behind in `gui/waveform/waveform_renderer.h`.

**Nothing enforces that a delegation stays thin.** The layer checkers constrain `src/scenes`,
`src/ui`, `src/gfx`, `src/notify`, `src/shell` and the platform boundary — none of them constrains
`gui/`, so listing logic drifting back into `Browser` would trip no ratchet. The characterisation
spec (`sim/scenes_spec/file_catalogue_spec.cpp`, written against **legacy** and re-run unchanged
after the move) is the only guard, and it is a guard on the *semantics*, not on the location. Two
new rows do guard the scenes side: `scenes-listing-through-the-seam` confines the six *dispatching*
entry points to `browser/listing.{h,cpp}`, and `scenes-no-legacy-waveform` (two rows,
`src/scenes/**` and `src/ui/**`) keeps the waveform split split. Both have a **zero** baseline.

### 15.3 The async listing seam: refusal, not overwrite — and the guard belongs to the resource

§6 assumed a browser reads a directory the way legacy does: synchronously, inside the UI's own call
stack. A scene may not block a tick, so `browser::Listing` is the seam, and two of its decisions
amend precedent set earlier in this stack.

**Delivery is exactly-once with an explicit refusal, deliberately *not* E2's `take_action()` mailbox.**
`take_action()` is a one-slot mailbox with a silent overwrite, which E2 accepted only because it was
documented. That contract is wrong here, because there is exactly one `FileCatalogue::fileItems`
vector: an overwrite would interleave two directory reads over one buffer. So the seam is a
three-state machine (`Idle` / `InFlight` / `Ready`); a second request while one is in flight is
refused (`Busy`), and a request over an uncollected result is refused too (`ResultPending`). The
trade is stated as **"a jam, not a loss"** and mitigated by polling every tick. There is **no
cancel**: `~Listing` asserts it is not in flight, the ownership rule is "own one as a scene member
and nothing else", and the destructor releases the claim when *not* in flight but deliberately not
when it is — a jam beats racing a fiber that holds a freed `this`.

**The single-flight guard is `FileCatalogue::claim`, on the resource, not on the seam.** It began as
a `Listing` member and that was a defect scheduled by this plan's own later tasks: the resource is
process-global, so two `Listing` objects are each `Idle` and both dispatch. Making it a `Listing`
static fixed that and surfaced the larger version — legacy `Browser` had its **own** `static
listingInProgress_` over the same vector, so during the coexistence a legacy listing and a scenes
listing were mutually invisible. The guard therefore moved onto the thing it guards: a CAS from
`nullptr` with an owner-checked release, claimed by **both** dispatch paths (`Listing::request` and
`Browser::beginListing`, whose static was deleted).

The contract, stated once on `claim()`: **hold from dispatch until the consumer has finished reading
`fileItems`**, not until the read ends. The two sides release at different moments because their
consumers differ — legacy consumes *inside* the op (`arrivedInNewFolder`, the selection search, the
render), so it releases on completion; the seam counts entries rather than copying them, so it
releases on `take_result()`, a tick or more later. Legacy's completion-release is therefore **not** a
second bug: it is correct given where legacy consumes, and it never had to survive a tick boundary.

Two limits recorded rather than closed: the mutating helpers do **not** check the claim (several
legacy paths call them directly and enforcing would break all of them, so the claim binds the two
*dispatch* paths); and the claim covers a **listing**, not a browsing *session* — the leaf calls
(`predictExtendedText`, `getUnusedSlot`, the save UIs' default-name search) do not claim at all.
Closing that means claim-on-open/release-on-close and a change to `Browser`'s lifecycle, which is
E4's with the load/save family.

**Honesty about coverage.** `Owner::run` is inline on the host, so no spec in this layer exercises
real fiber concurrency across this seam; the specs drive the state machine, not a race. The real
`deluge_worker_run` seam is driven by `src/bsp/rust/tests/owner_host.rs` and the TSan entry point.

### 15.4 `CustomItem` grew a backward arm and a press discriminator

§12.9 recorded that `ascend()` deliberately does **not** re-run `on_focus` when a leaf is reached
going up, and that the blanket `Kind::Custom` exclusion standing in for a backward arm "is sound
today". **It was not.** Both halves of that are now amended, by two different legacy cases.

* **`CustomItem::on_focus_backward` (task 10).** The exclusion gave every `Kind::Custom` item
  "leave `MenuState` alone" for free, and that is wrong for `gate::Selection`: its backward arm is
  `setValue(soundEditor.currentSourceIndex)`, and `selectButtonPress` never writes
  `currentSourceIndex` on the minimum-off-time row — so legacy restores the last **output** the user
  chose where "keep the cursor" leaves the cursor on MINIMUM OFF TIME. The hook has a do-nothing
  default, so the four items the exclusion used to cover are unchanged; `GateSelectionItem` is the
  one real override. It needed a **second field** (`real_output_choice`) because legacy holds two
  values the port had collapsed into one — the item's own `getValue`, which may be the non-output
  row, and `currentSourceIndex`, which never is.
* **`CustomItem::on_focus_is_press()` (task 7).** `on_focus` is `beginSession`, and `MenuScene` runs
  it on every focus transition — descent, deep link, shortcut jump, and `restore_state` rebuilding a
  saved path. For an item whose legacy body is `beginSession` that matches. For an item whose legacy
  body is `selectButtonPress` it does not, because legacy never navigates into such an item at all,
  so the port descends for one tick and acts from `on_focus`. That is right for a press and wrong
  for every other way in, and it was **live**: restoring a menu parked on `midi_device_linked`
  replayed the toggle and **cleared the user's linked definition file**, with no user action; a
  depth-0 deep link onto `start_stem_export` re-fired the export.

  The E2 ruling this looks like — that restoring a menu parked on a scene-opening item and
  reopening the scene is probably desired — does **not** extend to it, and legacy is the reason:
  `Toggle::beginSession` does not toggle, and `Toggle::selectButtonPress` carries a targeted guard
  ("if the current menu item is this one, return") whose condition **is** the restore case. The port
  had removed legacy's defence and kept the write.

  The mechanism is two pieces, and both are needed: a `FocusCause` on `enter_leaf` (`RowPress` from
  `descend_to`; `NoRowPress` from `retarget`, `restore_state` and `ascend`) **scoped by**
  `on_focus_is_press()`. A blanket skip on `NoRowPress` would regress the shipped, specced ruling that
  `EditNameItem` reopens its editor on restore; a flag with no cause can only ever mean "never act".
  **The line is "does it act without a press", not "does it do anything"**: opening a scene the user
  can back out of keeps the flag false; writing the model or firing a one-shot verb sets it.
  `SpecificOutputItem` stays false deliberately, because its repair write *is* legacy's own
  `beginSession`.

### 15.5 The `MenuState` vs `MenuContext` test, restated as the rule that governs later ports

§11's task-8 amendment already states this; it is repeated here because it is the one framework rule
E4 is most likely to apply from memory, and the memorable one-line version is **wrong**. "Legacy
stored it on a menu item, so it is `MenuState`" is a proxy that happens to agree in the MPE case and
disagrees in the counterexample sitting next to it: `MenuContext::midi_cable` is a
`soundEditor.current*` member (so context) that is nonetheless *written* by a menu item through a
mailbox. Who writes a value says nothing about where it lives. The operative test is two questions,
in order:

1. **Can a `Binding::get`/`set` — handed only a `const MenuContext&` — ever need to see it?** If yes
   it is a `MenuContext` field, whatever legacy called it and whoever writes it; a menu item that
   must set it does so through a one-shot mailbox.
2. **Does it die when the walk is re-rooted?** If yes it is `MenuState`, cleared by `retarget` with
   the rest of the walk.

`mpe_direction`/`mpe_zone` answer no to (1) and yes to (2), so `MenuState` is right — and that is
*why* the three MPE items are necessarily bespoke. A later port of a menu-item member that a **data**
node must read fails (1) and belongs in `MenuContext` with a mailbox.

### 15.6 The session/arranger split needed **two** bools, not three

§13's carry row costed this as a three-way conflation: `sound_editor.cpp`'s `setup` selects the song
root for `sessionView`, `arrangerView` **and** `automationView`, and legacy hides `song_midi_learn`
and `configure_song_macros` from all but the first. Three states, so the estimate assumed the port
needed to distinguish three.

It does not, and the reason is worth keeping: **nothing distinguishes the arranger from the song
automation view, because no legacy `isRelevant` does.** Legacy is
`getRootUI() == &sessionView` verbatim in both items. So `context_for` takes `session_view_root`
beside the existing `session_or_arranger_root` — two bools — `MenuContext::is_session_view` reports
it, and `relevance::is_session_view` guards both nodes.

Two corrections to that row's other numbers, recorded because they are the kind an estimate gets
wrong in the same direction every time: the positional-bool call sites were **85**, not the ~62
estimated; and of the nine `is_song_view` occurrences the row asked to audit, only **three** are
behavioural reads (the fourth the row named, `bindings.cpp:265`, is a comment) and all three
correctly stay wide, because legacy's `getCurrentModelStack` is verbatim the wide expression — so
narrowing them would have *been* the regression. The gain is real: `tables_3_spec`'s
`relevance_needs_ui` exclusion is gone, and both nodes now compare against legacy directly.

### 15.7 Three new scenes, and the browser family's split down the middle

`SceneKind::FirstApp + 3` `browser::BrowserScene`, `+ 4` `marker::MarkerScene`, `+ 5`
`recorder::RecorderScene`, joining E2's rename (`+ 1`) and context menu (`+ 2`).

**§7 step 3's "BrowserScene family — nine UIs → one" is amended to a split, not a shortfall.** E3
ports the **two the menu tree reaches** — `BrowserId::Sample` and `BrowserId::MidiDeviceDefinition` —
and leaves the other seven (`load_song_ui`, `load_instrument_preset_ui`, `load_pattern_ui`, all of
`gui/ui/save/`) to E4. Two structural reasons, neither of them budget: those UIs are opened **from
root UIs, not from menu items**, so this stack's legacy-vs-port oracle — the table specs, which walk
the menu tree — cannot exercise them at all; and they carry a second axis `BrowserScene` has none of,
`SlotBrowser`'s brand-new-name machinery (`mayDefaultToBrandNewNameOnEntry`, `getUnusedSlot`), which
is a *saving* concern rather than a browsing one.

Three premises this spec or the plan asserted and E3 disproved, listed together because the pattern
matters more than any one of them:

* **There is no OLED waveform.** `SampleMarkerEditor::renderOLED` draws a title, a lock badge, a
  duration and a sample count; the waveform is grid-only. No OLED waveform widget was invented.
* **`midi_device_linked` is a `Toggle`, not a browser opener.** The browser is one arm of
  `writeCurrentValue`; the OFF arm clears the linked file name. Both are ported.
* **`dynamic_children` fits no runtime-list page** (§12.6, E2) — a third premise of the same shape,
  and the reason this list is here rather than in three separate places.

`AudioRecorder`'s port carries one more general lesson. `opened()` sets
`AudioEngine::recordingSource`, which **nine** subsystems read as "the mic/line recorder UI is
live". Respelling the *condition* at the one reader the port happened to look at left the other
eight broken — input monitoring was dead for the whole ported recording, so the user heard nothing.
The fix publishes to the **same storage all nine observe**
(`AudioEngine::setForegroundRecording`/`foregroundRecordingSource`), in legacy's assignment order.
**Where a legacy flag has many readers, port the publication, not the predicate.**

### 15.8 Three defects in shipping firmware, found by porting

None introduced by the port, none fixed by it, all three recorded at their own site as well as in
`src/scenes/README.md`'s "Defects this port found in shipping firmware" — because a spec amendment
is not where a release note looks either.

> **These three are E3's, not the running total.** E4 task 3 added two more, both in
> `View`'s mod-knob half; see `src/scenes/README.md`'s ledger (now five rows) and
> `2026-08-26-ui-migration-design.md` §10.5.

1. **Use-after-free in `AudioRecorder::finishRecording`** (a crash/corruption bug). It frees through
   the member and only then clears `recorder` and `recordingSource`, so the pointer dangles while
   the channel still says LEFT/STEREO. `AudioEngine::setMonitoringMode`, reached from `renderAudio`
   on the **priority-0** audio task, tests that pair and dereferences `audioRecorder.recorder`; the
   Rust scheduler routes priority 0 onto the **preemptive** SGI executor, so the interrupt can land
   on any instruction inside `discardRecorder`. The `recorder != nullptr` double-check does not help:
   the pointer is non-null and freed for the whole window. `audio_engine.cpp`'s output/stem paths
   read the same member the same way. The port takes the correct order — channel first, then
   pointer, then free, with a **compiler** barrier between the two stores (no `dmb`: the reader is a
   handler on the same core, and a PE observes its own accesses in program order with exception
   entry as a context-synchronisation point, so a hardware barrier would order against observers
   that do not exist).
2. **`Browser`'s default-directory fallback is dead, and so is `LoadInstrumentPresetUI`'s.** Both
   test `Error::FOLDER_DOESNT_EXIST`; the read path reports a missing directory as
   `Error::FILE_NOT_FOUND`, and `FOLDER_DOESNT_EXIST` is produced only on `storage_manager.cpp`'s
   file-*open* path, which neither call reaches. `browser.cpp`'s mkdir arm behind it is dead too.
   The port accepts both codes as a documented divergence rather than patching — which folder a user
   lands in when one is missing is a product decision.
3. **The first gold-knob turn after boot is always swallowed.** `InputLowering::action_gold_turn`'s
   wiggle debounce compares `now_ms` against a `last_turn_ms_` that starts at 0, so on device — where
   `now_ms` is uptime — "has it been still for 500 ms" is always *yes* by the time a user reaches a
   knob. Consequence for this stack's own evidence: **all 22 pre-existing UI scripts turn gold before
   `t = 500`**, inside the window by luck, so every committed golden depicts the arm the device never
   presents first.

The disposal rule this stack has used throughout, restated because all three were decided by it:
**patch when both sides read through the same storage; record when the port replaces the predicate
outright.** All three replace a predicate or a renderer, so all three are recorded. (The
counter-example is `FileCatalogue::claim` in §15.3, where legacy and the port *do* read one vector —
and that one was patched.)

### 15.9 §13's carry list is closed; the E4 list is in `src/scenes/README.md`

Every row of §13 is now either done or restated in the README's "E4 roadmap", which is the live
version. Done: the file/sample family (tasks 6–7), `midiDeviceMenu`'s subtree (task 8), the
session/arranger split (task 9, §15.6), the row-scrolling test and the seven self-duplicate goldens
(task 10), and `CustomItem::on_focus_backward` (task 10, §15.4). Still carried, with E4's own
additions: the load/save browser family, the claim-covers-a-listing limit, the `take_choice()`
dispatcher and its two owed pieces, mod-matrix pad blinking, `perform_editor`'s sync calls, the
action seam's dispatcher, `model/clip/audio_clip.cpp`'s surviving renderer include, and the
`ascend()`-at-depth-0 divergence from `goUpOneLevel`'s `exitCompletely()`.

### 15.10 The final-review fix wave (Part A)

The whole-branch review returned CHANGES REQUESTED. What it changed, and why, one entry per finding
that moved code.

**A1 — `RecorderScene::after_render` frees the recorder from inside the card routine.**
`finish_recording()` reaches `AudioEngine::discardRecorder`, which frees. Legacy states the hazard on
its *other* caller, `AudioRecorder::slowRoutine` (`gui/ui/audio_recorder.cpp:168-176`): a recorder
suspended part-way through its own `cardRoutine()` is left running on freed memory and then freed a
second time. That caller is registered `RESOURCE_SD | RESOURCE_SD_ROUTINE` (`deluge.cpp:572-574`)
**and** carries the explicit `isSDRoutineActive() || deluge::sync::sd_busy()` test; `after_render()`
runs from `Shell::tick` and has neither.

The port's original note argued the guard did not apply because `AudioRecorder::process()` calls
`finishRecording` unguarded. **That argument was wrong**: `process()` pumps
`AudioEngine::routineWithClusterLoading()` as the first statement of its own loop (`:195`), so it is
the outermost thing on the stack and cannot be re-entered inside a suspended card routine. The
completion arm is now guarded in full — the attach as well as the free, because legacy does both in
one step — and `next_timer()`'s 10 ms re-poll makes the deferral cost exactly one tick.

The condition is read through a new seam, `AudioEngine::sdRoutineBusy()`, a sibling of
`setForegroundRecording` and there for the same reason: `src/scenes` may include neither
`OSLikeStuff/scheduler_api.h` nor `sync/sd_access.h`, and `audio_engine.cpp` already reaches both.
`discardRecorder`'s own ALPHA_OR_BETA diagnostic now reads it too, so the precondition has one
spelling. The spec drives the **real** flag rather than a stub: `recorder_scene_spec`'s
`in_sd_routine` runs the poll tick inside `yieldingRoutineForSD`, the BSP's own card busy-wait hook
and the only thing anywhere that makes `isSDRoutineActive()` true.

**A8 — `check_layer_rules.py` skipped only `//` lines.** A Doxygen block-comment continuation
(` * waveformRenderer`) would have counted against the symbol rows task 12 added. The skip now
covers block-comment continuations and closes as well, and is deliberately *not* a general comment
stripper: a `/*`...`*/` state machine, or removing trailing `// ...` text, would change what the
existing baseline counts, and this checker's baseline may only shrink. Verified both ways — the
baseline is byte-identical after the change (no count added, removed or moved), and a probe file
carrying the mention inside a block comment counts 1 under the old skip and 0 under the new one.

**A6/A7 — the focus discriminator said "Restored" for a press, and there are two mechanisms for one
apparent question.** `FocusCause`'s values were `Descended`/`Restored`, and a shortcut-pad jump —
which *is* a press, just not one on the focused node's row — took the `Restored` arm. The behaviour
was right (legacy reaches a shortcut jump through `beginSession`, never `selectButtonPress`), so the
fix is the names, not a third cause: `RowPress` / `NoRowPress`, which state the question
`on_focus_is_press()` actually asks and make its contract readable from the enum alone. `Node`'s doc
follows.

A7 is documentation by design: `CustomItem::on_focus_backward` is a separate virtual while
`FocusCause` is a discriminator, and a contributor has to learn both. `CustomItem`'s doc in
`menu/node.h` now carries the table — direction is two virtuals because legacy's own `beginSession`
arms share no code, so an item writes only the arm it has; the press question is a discriminator
because its answer is never "run a different body", only "run the body you have, or don't". A third
virtual would make every press-shaped item write an empty override, which is exactly the thing a
later contributor fills in by mistake.

**A4 — `Browser::beginListing`'s refusal was discarded at nineteen call sites.** The `bool` return
this branch added to a legacy function was ignored everywhere but one site
(`LoadInstrumentPresetUI`'s output-type reload), and three of the ignoring callers
(`setFileByFullPath`, `goIntoFolder`, `goUpOneDirectoryLevel`) returned `Error::NONE` regardless.
The branch's "behaviour-preserving" claim for that edit is **conditional** — it holds only because
the sole other claimant of `FileCatalogue` is `deluge::scenes::browser::Listing` and nothing links
`deluge_scenes` — and nothing at those sites said so.

Every deliberately-ignoring site now calls `Browser::beginListingOrRetryLater`, a one-line inline
wrapper whose doc carries the whole account: the two ways a dispatch can be refused, why each is
inert today, what state a refused dispatch leaves behind and how it reconciles, and — the part that
matters for E4 — exactly what would have to change if a scenes seam ever holds the claim in shipping
firmware, since it holds until `take_result()` by design. The name is the record at the site; the
doc is the reasoning. **Not one line was added to `browser.cpp`**: 158 citations in this tree
resolve into that file by line, and the rename is length-neutral.

**A5 — the `setMonitoringMode` read side was unfenced.** Task 5 fix round 2 ruled "a compiler
barrier and nothing more" and fenced the **writer** only. The reader — `setMonitoringMode`, running
from `renderAudio` on the priority-0 audio task, i.e. the preemptive SGI executor — still performed
two plain loads of `audioRecorder.recordingSource` and `audioRecorder.recorder`, so the compiler was
free to reload the pointer *after* the `doMonitoring &&` branch had already tested it. That is
exactly the dereference the double-check exists to prevent, and shipping half the pairing made the
invariant look enforced when it held in one direction only. The reader now loads the pair once,
channel first, under an `acquire` `std::atomic_signal_fence` pairing the writer's `release`, and
uses the locals throughout. The reasoning for a signal fence rather than a `dmb` is the writer's and
is unchanged: handler and thread on the same core, so the reordering risk is the compiler's.

**A2/A3 — step 8b armed from scenes that structurally cannot paint.** A covered `MenuScene` with
source pads kept the grid gate at 20 ms, and `RecorderScene` under its own opaque clipping popup
armed both OLED panels every 10 ms. Both are shell-side; the full account is the shell design spec's
§15.3, and `src/scenes/README.md`'s "Repaint and the timer fold" carries the contributor-facing
version. What matters here is the rule a scene author now relies on: **a timer arms the surfaces the
scene claims and is not covered on, and a covered scene is still woken** — `Shell::tick_stack`
advances every scene's clock whatever the render pass decides, so `next_timer()` remains a promise
about `tick(dt)` even when nothing is drawn for it.

## 16. Amendments closing E4 (2026-09-07, Tasks 1–14 of `2026-09-06-ui-scenes-services-and-roots-4.md`)

E4 is the plan that **broke up `View`**. It shipped three `shell::Service` implementations
(`services::ModKnobs`, `services::GlobalControls`, `services::MidiLearn`), the three missing overlay
scenes (`overlay::ParamPopupScene`, `overlay::CountInScene`, `overlay::ScreensaverScene`), the
shared `timeline::TimelineState`, and the first two application scenes that own the grid
(`slicing::SlicerScene`, `audio_clip::AudioClipScene`). Its coverage number — what the legacy
deletion can be scheduled against — is the migration spec's §10.7, in the shape §10.6 established
for E3.

This section records **every ruling the plan made and every divergence it found**, including the
ones that contradict the plan's own prose. The plan is committed history and is not being rewritten;
this is where the true account goes.

### 16.1 Two things the plan's prose asserts that are false

**"The first two root scenes."** The plan's title, goal paragraph and Architecture paragraph all
call `SlicerScene` and `AudioClipScene` "the first two root scenes". **The slicer is not a root view
at all**: `class Slicer final : public UI` (`gui/ui/slicer.h:33`), `oledShowsUIUnderneath = true`
(`:35`), opened by `openUI(&slicer)` from `sample_browser.cpp:378` and
`context_menu/sample_browser/kit.cpp:77` — i.e. it is pushed *over* whichever root is beneath.
`shell/scene.h` already recorded that only `TimelineView` and `KeyboardScreen` are `RootUI`. Found
by task 8's implementer, verified in full by its review. The plan's task TABLE was right (it lists
`TimelineState` under task 11 only, since only `AudioClipScene` consumes it); only the prose was
wrong. `AudioClipScene` is the **first and only** `is_root() == true` scene in the library.

**"`View::buttonAction`'s arms are every UI's fallback."** Task 4's table said so. `UI::buttonAction`'s
base returns `NOT_DEALT_WITH` (`gui/ui/ui.h:98-100`) and forwards nothing; only the three MOD hooks
forward to `view`. `View::buttonAction` has exactly two callers in the tree —
`TimelineView::buttonAction`'s final `else` (`gui/views/timeline_view.cpp:120`) and
`keyboard_screen.cpp:591` — and `grep "public RootUI"` returns exactly two classes,
`TimelineView` (`timeline_view.h:27`) and `KeyboardScreen` (`keyboard_screen.h:35`), **both of which
forward**. So `getCurrentUI() == getRootUI()` is not an approximation of the reachability rule, it
*is* the rule, exactly. That is what `Scene::is_root()` exists for (§16.3).

### 16.2 Three defects in the shell's own contracts, each invisible until a consumer existed

All three were latent for three plans, and all three were found by building the first thing that
would have exercised them. That is the argument for building the services before more scenes, and
also the argument against trusting a contract no consumer has ever used.

1. **The routing chain was inverted.** Plan D put `mod_knobs` before the top scene. Legacy is
   uniformly *current UI first, `View` as the base-class fallback*: `UI::modEncoderAction`
   (`gui/ui/ui.cpp:35-37`), `UI::modButtonAction` (`:39-41`) and `UI::modEncoderButtonAction`
   (`:43-45`) are bare forwards to `view`, overridden by six, two and five UIs respectively. The
   concrete loss was `SoundEditor::modEncoderAction`'s MIDI-learn interception, which
   `MenuScene` is the port of and which could never have fired. Corrected to **top scene →
   `mod_knobs` → `global_controls`**, then extended by task 5 with `midi_learn` last on
   LED-composition grounds (`midiLearnFlash` overwrites three LEDs `GlobalControls` declares). Shell
   spec §15.4.
2. **Plan D also excluded `ModEnc0`/`ModEnc1` from `is_mod_or_gold` entirely**, which sounds right
   under the inverted chain and is wrong under the corrected one — it would have left task 3's port
   of `modEncoderButtonAction` unreachable dead code. Found by the implementer reading the same file
   the controller's ruling had only half-read. Shell spec §15.5.
3. **A declared LED blink was not a wake source.** `compute_next_wake` had no blink input and the
   phase comes from `now_ms`, so a scene whose `next_timer()` returns `nullopt` — `MenuScene` on a
   node with neither source pads nor a shortcut position — froze its own `Blink(110ms)` Back and
   Learn LEDs on a quiet system. A shipped bug from plan D/E1, fixed in task 4 fix round 1.
4. **A service timer could not repaint anything.** `Service` had four virtuals and no
   `timer_repaints()`; `Shell::tick`'s fold iterated `stack_.scenes()` only, so a service's
   `next_timer()` reached `compute_next_wake` and nothing else. `MidiLearn`'s flash phase would have
   advanced on schedule with no pad ever repainting. `Service::timer_repaints()` + `Shell::tick`
   step 8c, defaulting `{false, false}` so no existing service changed. Shell spec §15.6.

A fifth, found in task 10 and fixed as its own interlude: **`SceneStack` both over- and
under-notified `on_resume`** — a popup expiring over a scene fired `on_resume()` on it (in legacy an
OLED popup is not a UI at all, so `closeUI`'s `focusRegained` never fires for one), which wiped
forty hand-placed slices 800 ms after a pad tap; and `pop`/`remove` resumed the RAW top rather than
the input top, so a scene that genuinely regained the input top got nothing. Now fires only when
`&input_top()` differs across the mutation. Shell spec §15.7.

### 16.3 Rulings this plan made, with what each cost

* **`Scene::is_root()`**, a virtual with a `false` default, gating **only** the `View`-arm group of
  `GlobalControls` and the whole of `MidiLearn`'s `handle`. The `Buttons`-tail group (PLAY, RECORD,
  TEMPO_ENC) stays ungated, because `hid/buttons.cpp:134-141` runs the tail for any current UI that
  returns `NOT_DEALT_WITH`, root or not. Two legacy things, two reachability rules, one service. It
  had to be a `Scene` virtual rather than a stack-position rule so a spec-local scene can override
  it — registry scenes are process singletons and a rig's stack may be empty.
* **The MIDI-learn button boundary belongs to `MidiLearn`**, and `GlobalControls` consults
  `LearnMode::active()`. The rule is "the service that owns the MODE owns the arm, and the other
  asks it"; the alternative, duplicating the mode test in both, is how `View` got to 2,908 lines.
* **`GlobalControls`' SAVE/LOAD arms are a documented no-op with a `@warning`, not a second
  outbox.** A mailbox with no consumer is exactly the shape migration §10.2 already lists as a
  blocker; adding a second before the first has a consumer doubles the wiring plan's surface for no
  gain. The *decisions* are counted through `GlobalControls::unported_opens(UnportedOpen)`, a tally
  per site, so an assertion about one arm cannot be satisfied by another.
* **Scene kinds are `FirstApp + 6`** (param popup), **`+ 7`** (count-in), **`+ 8`** (slicer),
  **`+ 9`** (audio clip); `ScreensaverScene` registers at the shell's own `SceneKind::Screensaver`,
  because the service pushes it by name and `shell/shell.cpp:176-183` deliberately leaves that slot alone.
* **`SlicerScene`'s five predicates**, which took two corrections: `is_root()` false,
  `is_grid_overlay()` true, `occludes_grid()` **false**, `oled_transparent(Main)` true,
  `dims_grid()` **overridden** to false. The `occludes_grid()` ruling was `true` twice before it was
  read properly: `Slicer` overrides `renderMainPads` only, `UI::renderSidebar`'s base returns false
  (`gui/ui/ui.h:148-151`), and `doAnyPendingGridRendering` keeps walking, so the sidebar beneath
  paints. `Shell::tick_stack`'s own comment defines the predicate as "covers the grid COMPLETELY".
  `dims_grid()` needed the override because its default is `is_grid_overlay()`, which would have
  halved a sidebar legacy never greys (`Slicer` has no `getGreyoutColsAndRows`).
* **The directory is `slicing/`, not the plan's mandated `slicer/`** — because
  `scenes-no-root-ui` matches `slicer` as a whole word and the include path tripped it. Ratified in
  task 9 and unchanged; task 14 then removed the underlying cause (§16.5).
* **`TimelineState` keeps `adopt`/`publish` and the triplet arithmetic; `take_tick_square` moved
  out** to `AudioClipScene`, on the rule that a symbol belonging to one consumer but living in a
  shared header never gets moved later.
* **The four inventions the 2026-09-06 audit found** (`Shell::dim_grid`'s overlay dim,
  `GestureEvent::Hold`, the widened `SecondPad`) **and the two legacy bug-fixes are out of scope for
  every task here.** They are Kate's call and stay recorded.
* **`Services{}` for every pre-existing level-2 script.** Only task 13's own ten scripts get the
  real services. E1–E3's goldens were recorded against null services and are the regression baseline
  for three plans; re-recording 47+ goldens as a side effect of adding a service is a change nobody
  could review.

### 16.4 Two divergences the port ships deliberately, both recorded at the site

* **`ClipView`'s `Y_ENC` shift arm is a silent no-op for a gesture that really shifts the clip on
  the device.** It is *claimed*, because legacy claims it (`clip_view.cpp:249`) and letting it fall
  through would hand a `Y_ENC` turn to `GlobalControls`, which legacy never does — and then it does
  nothing, because two of the three arguments `Clip::shiftHorizontally` takes are decided by
  `getCurrentUI() == &automationView` (`:203-206`), a `scenes-no-root-ui` read with no answer while
  no `AutomationScene` exists, and its undo consequence `alloc_sdram`s
  (`:240-245`). **It must not survive E5 in that state**: whoever writes `AutomationScene` ports
  this arm in the same change. Recorded at the arm, in `src/scenes/README.md`'s carry table, and in
  migration §10.4 — three places, because a fact recorded only in a task report is a fact this plan
  lost three times.
* **`AudioClipScene`'s `clip_resynced` is written from a citation and driven by nothing.** It
  carries a `@warning` saying so on its declaration.

### 16.5 The citation checker, measured four times rather than argued

Task 3's review found ~20 wrong line numbers in one task, the **third round running**, and
`check_citations.py` reported 0 because the lines resolved. What followed was four changes, each
one measured on the live tree before it was kept, and each measurement is the reason the change
stands:

1. **A symbol predicate**: a citation naming its symbol beside its line must have that symbol inside
   the cited range. Found 36 more, tree-wide, on its first run.
2. **Single-line citations judged.** The skip had been hiding 534 citations carrying a checkable
   symbol; coverage went 25.8% → 44.1% tree-wide. Of the 30 hits, 22 were plain range corrections
   and 8 were a *shape* rather than a defect, rewritten so every symbol sits beside its own citation.
3. **`--strict` in CI.** The premise correction that made this necessary: the checker was
   warning-only and `main()` returned 0 without `--strict`, which CI did not pass. That is why three
   rounds of drift shipped past it.
4. **A brace-matched body-extent test replacing the direction narrowing.** The narrowing was
   *vindicated by measurement* for the drift it could find (0 of 37 missed, all upward) and then
   defeated by a concrete case it declined to judge — a citation 1,173 lines BELOW its symbol, which
   looks line-for-line like a legitimate body citation. The extent test found **33 real defects,
   zero false positives**, including a class nothing else could catch: citations naming **the wrong
   function entirely**, which read perfectly (`marker_scene.h` attributed two `WaveformRenderer`
   helpers to `PadLEDs::renderExplodeAnimation` when their real single callers were
   `renderAudioClipCollapseAnimation` / `renderAudioClipExplodeAnimation`).

Task 14 added a fifth: **`SCAN_ROOTS` now covers `tests/ui-scripts`**, which had been invisible to a
blocking gate — task 13 found two wrong citations there by hand. Measured before it was done, since
it sweeps 27 pre-existing scripts nothing had ever checked, and **one flag** came out — a citation
pinned to a past revision, reworded at its site rather than excepted. The sub-counts are stated in
the checker's own comment with the derivation that produces them (`CITATION_RE` 74 + `CONTINUATION_RE`
15, of which 49 resolve and 12 carry an extractable symbol), and with the note that the same script
counted 90 and 50 on the tree *before* the flag was fixed: re-run it against the exact tree you are
reporting on, which is this plan's own lesson from task 9. `src/scenes/README.md` and the three design specs are still outside, and the
comment says so rather than leaving it to be rediscovered.

### 16.6 Four ratchet rows and one narrowing (task 14)

All zero-baseline, each probed separately — the violation made, the checker confirmed to exit 1
naming the right file and rule, then reverted. A row recorded as "probe-verified" whose probe tested
a different alternative is worse than no row; that happened in E2.

* **`scenes-no-legacy-hid-drivers`** (`src/scenes/**`, `src/ui/**`): the five legacy output-driver
  spellings `uiNeedsRendering(`, `renderUIsForOled(`, `indicator_leds::`, `PadLEDs::`,
  `uiTimerManager.`. This is `model-calls-ui`'s list minus everything `scenes-no-display` and
  `scenes-no-root-ui` already cover, and it matters here because the three services are the port of
  `View`, which is *the* legacy caller of all five. `PadLEDs::flashCursor` is excluded by name (a
  persisted setting byte, not a driver call); `currentUIMode`/`isUIModeActive(` are excluded because
  trailing citation comments would have given the row a non-zero baseline.
* **`scenes-no-input-lock`** (`src/scenes/**`): Global Constraint 5, asserted in prose by three
  plans and by six `@warning`s, with no mechanism until now.
* **`services-no-oled-timer-claim`** (`src/scenes/services/**`): `.oled = true`. A service has no
  stack position, so `Shell::tick` step 8c cannot narrow its claim the way step 8b narrows a
  scene's, and an OLED surface blits unconditionally once it has rendered — so the claim *is* the
  blit rate. A ratchet, not a ban.
* **A symbol row no longer counts `#include` lines.** The row that made the case is
  `scenes-no-root-ui` and the cost was real: it is why the slicer's directory is `slicing/`.
  Nothing is lost — every symbol row is scoped to a directory an include row already gates, so a
  real `#include "gui/ui/slicer.h"` still fails under `scenes-includes-gui`, which names the actual
  violation. Zero of the baseline's 120 counts moved.

The plan's own two suggested rows were **already enforced** and were not added: services naming
`getCurrentUI`/`soundEditor`/a view global is `scenes-no-root-ui`, whose glob is `src/scenes/**`;
new scenes naming `waveformRenderer`/`waveformBasicNavigator` is `scenes-no-legacy-waveform`, whose
globs are `src/scenes/**` and `src/ui/**`. Both already covered `services/` and the four new scene
directories on the day they were created.

### 16.7 The two failure modes this plan kept repeating

Recorded because they are process findings, not code findings, and the next plan will have both.

**A claim broader than its mechanism.** Task 11 shipped "every render example is guarded" in three
places — spec, README and report — when four of twelve `expect_pads_match` calls had no guard, and
the three unguarded ones that mattered were exactly the cases where both sides legitimately go
black. Demonstrated rather than argued: forcing a failed waveform scan made a guarded example fail
on its guard and left the unguarded count-in example **27/27 passing on two black grids**. The fix
that stuck was structural rather than a fourth copy of the guard — `expect_pads_match` now takes a
`PadExpectation` with no default constructor, no default argument and no "nothing" value, applied
before the comparison so a failure names the vacuity — because round one's fix was still a
discipline, and task 12 was about to add more examples to the same file.

**A fact recorded only in a task report when it needed to be in code.** Three times. The sharpest
was the screensaver's boot wiring: nothing maps `ScreensaverMode::OFF` or
`FlashStorage::screensaverTimeoutMinutes` onto `ShellConfig::screensaver_idle_ms`, so as shipped the
value stays 0 and **the screensaver can never activate** — a scene the shell had been pushing at
nothing for three plans would have gone on never being pushed. It is now an entry in
`registry.h`'s boot-wiring list carrying legacy's actual formula, why `shell::Screensaver` cannot
compute it itself, and that it must be re-supplied on every settings change. A task report is not a
living document.

A third, worth naming for the next controller: **a true finding about one symbol generalised into a
premise about a class of symbols**, arriving in the next brief with the authority of something
established. Four instances — E3's zoom ceiling carried into task 8's brief where `initiateXZoom`
has no such wait; the count-in checklist's "right-aligned, blinking at speed 2" carried from a
tester-facing document into task 6's brief, describing behaviour the firmware cannot produce; task
9's voice-walk finding about `Slicer::graphicsRoutine` carried into task 11's brief about a
different class's function of the same name; and task 8's accurate "legacy clears above its SD,
rendering-lock and UI-mode gates" widened into "above every gate", which would have cleared the
restore point on three gestures legacy leaves alone. The countermeasure is one line: **re-read the
cited code in the new context before restating it, and prefer quoting the original sentence to
paraphrasing it.**

### 16.8 Ten firmware defects, and where the count now lives

E3 left three. E4 added seven, all found by porting and none fixed: `View::modButtonAction`'s two
unguarded `getModKnobMode()` dereferences (`view.cpp:1534`, `:1549`),
`View::setKnobIndicatorLevel`'s uninitialised `knobPos`, `ClipView::getTickSquare` comparing a
scroll against the zoom (`clip_view.cpp:382`), `ClipView::lengthenClip`/`shortenClip`'s unguarded
`actionLogger.firstAction[BEFORE]` (`:98`, `:129` — established by a spec *segfaulting*),
`Slicer::renderMainPads`' missing image null-check, `Slicer`'s 432 bytes of dead stack declared
twice, and `Slicer::selectEncoderAction`'s equality-only count wrap. Full write-ups with severities
are in `src/scenes/README.md`'s "Defects this port found in shipping firmware"; migration §10.5 is
the release-note pointer. **None of the four in `view.cpp`/`clip_view.cpp` is annotated in the legacy
file**, because this stack cites those two files by line more than 130 times and inserting a comment
would shift every citation.

## 14. Citations into legacy source: the convention

Every claim in `src/scenes/**`, `src/ui/**` and the spec trees is justified by a citation into
`src/deluge/**`. Those citations **rot**, and this plan is the proof: E2 task 9 corrected **176** in
one sweep, task 11's review found **92** more resolving inside GPL header blocks across nine files,
task 11b corrected **125** — and roughly **50 of those were exactly correct when written** and were
broken by this plan's *own* edit to `gui/ui/menus.cpp`, which wrapped two constructor calls and
shifted every line below `menus.cpp:1647` by two. Task 13 swept the whole surface again and corrected
**238** more.

**The convention, applied from task 13 onward: a citation names its SYMBOL as well as its line** —
`` `command.cpp:115 learnNoteOn` `` or `` `Command::learnNoteOn` (`command.cpp:115`) ``. The symbol
is the only part of a citation that survives an edit to the file it points at, and this plan edits
legacy files routinely.

**A checker was built after all — warning-only** (`scripts/check_citations.py`, fix wave R2; the
ruling it reverses is recorded in the amendments below). The original ruling said no checker,
because verifying that a line *means* what the comment claims is not mechanizable. That is true and
it argues against a *complete* checker; it does not argue against the partial one task 13 had
already built and measured. What that sweep mechanized is the **certainly-wrong** set — a citation
whose whole range is past EOF, inside a licence block, or nothing but
`#include`/`namespace`/brace/blank lines, because no claim in these comments refers to those — and
that is exactly the class of breakage this plan inflicted on itself: ~50 of task 11b's 125 were
correct when written and were broken by the `menus.cpp` edit two paragraphs up. A script catches
that for free on the commit that causes it; a convention cannot, because the commit that breaks the
citation does not touch the file the citation is written in.

It runs in CI (`.github/workflows/tests.yml`) and as a pre-commit hook, and it **always exits 0**.
That is the whole design: its blind spot is the commonest drift there is — a citation four lines low
still resolves to plausible code, which is the shape of all 176 task 9 found by reading — so a clean
run is not a clean bill of health, and gating on it would sell one. Known false positives are named
in `scripts/citation_exceptions.txt` with the reason each is the predicate's fault rather than the
citation's. The symbol convention above sits on top of it and remains the part that actually
survives a line shift.
