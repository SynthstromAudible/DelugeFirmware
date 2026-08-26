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
