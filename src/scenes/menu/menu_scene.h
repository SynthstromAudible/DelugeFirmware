#pragma once

/// @brief `MenuScene` -- one shell `Scene` that interprets any `Node` tree (spec E §4, amended by
///        `.superpowers/sdd/2026-08-27-ui-scenes-menus-1/task-5-brief.md`).
///
/// There is exactly one of these for the whole menu system. It renders whatever `Node` is at the
/// top of its own path through the toolkit widgets, consumes input the toolkit way (a widget
/// claims what it owns while it is being drawn), and commits every edit through
/// `menu::bindings`. What legacy spread across `SoundEditor` plus ~200 `MenuItem` subclasses is
/// here: the data is `node.h`'s, the model access is `bindings.h`'s, and this file is the
/// interpreter between them.
///
/// **Ports.** `MenuItem::drawItemsForOled` (`gui/menu_item/menu_item.cpp:47`),
/// `Submenu::drawSubmenuItemsForOled` / `focusChild` (`gui/menu_item/submenu.cpp:99`, `:21`),
/// `HorizontalMenu::renderOLED` / `renderMenuItems` / `switchVisiblePage` / `selectMenuItem` /
/// `preparePaging` (`gui/menu_item/horizontal_menu.cpp:477`, called at `:114` and `:378`) -- all four already
/// pinned by the `list` / `columns` widgets, which this scene composes rather than re-implements;
/// `SoundEditor::selectEncoderAction` (`gui/ui/sound_editor.cpp:1072`), `buttonAction` (`:333`,
/// back -> `goUpOneLevel` `:672`, which exits at depth 0 via `exitCompletely` `:678`),
/// `horizontalEncoderAction` (`:1008`), `padAction` (`:1405`) with `potentialShortcutPadAction`
/// (`:1111`), `enterSubmenu` (`:324`), `setupShortcutBlink` (`:959`) / `blinkShortcut`
/// (`:974-1000`), `getGreyoutColsAndRows` (`:214`), and the `MultiRange` redirect -> spec E
/// §4.1's `Permission::MustSelectRange`.
///
/// **What this scene may not do.** It never calls an `OledFrame` drawing primitive
/// (`pixel`/`hline`/`rect`/`glyphs`/...): everything on screen comes from a `ui::oled` widget, so
/// the layout rules live in one place and Task 12's ratchet can count primitive calls at zero.
/// It never allocates, never reads a wall clock (the frame's `now_ms()` is the only time it
/// knows), and never mutates the model from `render_oled`/`render_grid` -- `after_render()` and
/// `handle()` are the two places a write may happen.

#include "notify/dirty_set.h"
#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "shell/scene.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/grid/effects.h"

#include <cstdint>
#include <optional>
#include <span>

/// @brief Forward declared for `MenuState::patch_polarity` -- see the note there. Defined in
///        `modulation/patch/patch_cable.h`; a scoped enum with an explicit underlying type is
///        complete enough to store by value from its declaration alone.
enum class Polarity : uint8_t;

namespace deluge::scenes::menu {

/// @brief How deep a menu path may go, and how many `ListState`s the scene therefore keeps.
///
/// Unrelated to `ui::shell::kStackDepth` (`shell/scene.h:59`), which counts *scenes*: the whole
/// menu is one scene however deep the user walks into it. Eight is legacy's own
/// `menuItemNavigationRecord` depth (`gui/ui/sound_editor.h`).
inline constexpr uint8_t kMenuDepth = 8;

/// @brief The most children one menu level materialises in a frame.
///
/// Matches `kMaxDynamicChildren` (`node.h`), so a `dynamic_children` hook that fills its whole
/// window is never truncated by the renderer. The row array this sizes is the scene's one large
/// stack allocation (~4 KB in `render_vertical`); it is a frame local rather than a member so it
/// costs nothing between ticks.
inline constexpr size_t kMaxMenuChildren = kMaxDynamicChildren;

/// @brief The most columns one horizontal page materialises. Legacy pages four slots at a time
///        and a `Column` may claim several, so sixteen is well past any real page
///        (`horizontal_menu.cpp:477`'s `preparePaging`).
inline constexpr size_t kMaxMenuColumns = 16;

/// @brief The two phases of `render_grid`'s shortcut-blink cycle, in milliseconds.
///
/// **Not two independent blinks, and not one symmetric period.** Legacy runs *one* alternating
/// cycle off `shortcutBlinkCounter` (`sound_editor.cpp:1012-1038`): `counter & 1` selects the phase,
/// the param phase flashes the focused item's pad and re-arms `TimerName::SHORTCUT_BLINK` at
/// **180 ms** (`:981-986`), and the source phase flashes the patching-source pads and re-arms at
/// **20 ms** (`:989-999`). So the cycle is `kSourceBlinkMs + kShortcutBlinkMs` = 200 ms, of which
/// the source phase is the short leading slice and the param phase is the rest. The two pads are
/// deliberately never lit at the same instant -- legacy's own comment at `:971` says why ("blinking
/// two pads on the same row at same time doesn't work").
///
/// One cycle needs one anchor, not two `Blink`s: `MenuState::blink_anchor_ms` is it, and
/// `blink_phase` / `ms_to_phase_change` (`menu_scene.cpp`) are what read it.
/// @{
inline constexpr uint16_t kShortcutBlinkMs = 180;
inline constexpr uint16_t kSourceBlinkMs = 20;
inline constexpr uint32_t kBlinkCycleMs = uint32_t{kSourceBlinkMs} + kShortcutBlinkMs;
/// @}

/// @brief Live interpreter state: the path, the widgets' own POD state, and what this tick's
///        render observed for `after_render()` to act on.
///
/// **Not** what `save_state` writes -- see `MenuSavedState`. This is a member of `MenuScene` and
/// is handed to a `CustomItem` by reference, which is why `node.h` forward declares it.
///
/// @par The one-shot mailboxes, and the order `after_render` must drain them in
///
/// Six fields below are **one-shot mailboxes**: a hook with no `Result` of its own (a
/// `CustomItem`'s `on_focus`, `handle` or `render`; a widget's response) writes a request, and
/// `MenuScene::after_render` -- the first hook each tick that owns both a mutable `MenuContext`
/// and a `Result` -- performs it and clears the slot. They are `range_choice`, `source_choice`,
/// `midi_cable_choice`, the `value_changed`/`value_node` pair, `pending_action` and
/// `pending_scene`, and between them they use four "empty" conventions (`-1` for the two signed
/// indices, `nullptr` for the two pointers, `MenuAction::None` for the request, `SceneKind::Null`
/// for the scene id) because each field's own type supplies the sentinel. `range_picker_pending`
/// / `pending_after_range` are *not* mailboxes -- they are a two-tick handshake the picker's
/// `select_chain` reads, and they outlive a single `after_render`.
///
/// **The contract, in one sentence: everything that changes what the *next* navigation step will
/// see must drain before the navigation arm, and everything that is merely a consequence of this
/// tick may drain after it.** Concretely, in `after_render`'s own order, with each mailbox's band:
///
///   1. **`range_choice`** -- above navigation. `select_range` writes `ctx_`'s multi-range and a
///      descent decided below reads it; legacy commits it inside `selectButtonPress`
///      (`gui/ui/sound_editor.cpp:1983-1987 setCurrentMultiRange`).
///   2. **`source_choice`** -- above navigation, and this is the load-bearing one.
///      `ctx_.source_index` is what `gate_selection_chain` picks its target from and what
///      `gate_mode_names` formats, so draining it below navigation chains off the *previous*
///      output. Legacy assigns `soundEditor.currentSourceIndex` on the way *through*
///      `selectButtonPress` (`gui/menu_item/gate/selection.h:43-54 selectButtonPress`).
///   3. **`midi_cable_choice`** -- above navigation, same shape: `ctx_.midi_cable` is read by the
///      device subtree a descent would enter (`gui/menu_item/midi/devices.cpp:55 beginSession`).
///   4. **`value_changed` / `value_node`** -- above navigation: the commit must happen before an
///      ascend leaves the node that owns the value. It also sets `repaint`.
///   5. **`pending_action`** -- *below* navigation but *above* the repaint check. Four of the five
///      actions `ascend()` on the tick they are filed, so they are themselves navigation; sitting
///      above the repaint check is what makes that ascend repaint.
///   6. **`pending_scene`** -- below the repaint check: a `Push` puts another scene on top, so this
///      scene's own repaint is decided without it.
///
/// So the rule is not "mailboxes drain first". It is: **`ctx_` mutations first, then navigation,
/// then the repaint decision, then scene pushes** -- and a new mailbox belongs in whichever of
/// those four bands its effect falls into. The load-bearing case is `source_choice`, and
/// `sim/scenes_spec/tables_3_spec.cpp`'s `drains the selector's output index BEFORE the navigation
/// arm, not after` is the spec that fails if it moves -- it opens the gate selector from a context
/// whose `source_index` names the *other* page, in both directions, so a stale read lands on the
/// wrong page either way. The other five are ordered by the argument above rather than by a test,
/// which is why the argument is written down.
struct MenuState {
	/// The menu path, root first. `path[depth]` is what is on screen.
	const Node* path[kMenuDepth] = {};
	uint8_t depth = 0;

	/// One per menu depth, so walking back up restores the level's own cursor -- legacy's
	/// `Submenu::current_item_` being a member of each submenu (`submenu.cpp:21`).
	///
	/// @note `list[depth].editing` IS the editing flag (`ui/core/state.h:15`): a reserved bool no
	///       widget reads, which is exactly what a scene-owned "the leaf is being edited" bit
	///       wants to be.
	deluge::ui::ListState list[kMenuDepth]{};
	/// One per menu depth, like `list`: a horizontal page nested under another must not lose the
	/// parent's column focus when the child is entered and left again.
	deluge::ui::ColumnsState columns[kMenuDepth]{};
	/// One per menu depth, for the same reason -- and because `render_leaf_toggle` borrows this
	/// for its own two-row list, which would otherwise fight the parent menu's label scroller.
	deluge::ui::ScrollText scroll[kMenuDepth]{};

	// --- typed per-kind edit storage: the widgets do not share one int32 ---
	/// `Kind::Int` and `Kind::Decimal`: `int_value` / `decimal_value` take an `int32_t&`.
	int32_t edit_int = 0;
	/// `Kind::Toggle`: `toggle()` takes a `bool&` (`ui/oled/values.h:169`).
	bool edit_bool = false;
	/// `Kind::Enum`: `enum_value()` takes an `int32_t& index` ...
	int32_t enum_index = 0;
	/// ... plus its own `ListState` ...
	deluge::ui::ListState enum_list{};
	/// ... plus its own `ScrollText` (`ui/oled/values.h:143`).
	deluge::ui::ScrollText enum_scroll{};
	/// `Kind::Range`: `range_value` takes two `int32_t&`s -- `edit_int` is the **lower** edge and
	/// this is the upper (`node.h`'s `RangeSpec`).
	int32_t edit_upper = 0;
	/// `Kind::Range`: which end the horizontal encoder has selected.
	///
	/// Legacy keeps the same fact in `SoundEditor::editingRangeEdge`, one global shared by every
	/// range item (`gui/menu_item/range.cpp:32`). One field here for the same reason: only one leaf
	/// is on screen at a time, and `enter_leaf` re-seeds it per visit exactly where
	/// `Range::beginSession`/`IntegerRange::beginSession` do.
	deluge::ui::oled::RangeEdge edit_edge = deluge::ui::oled::RangeEdge::Off;
	/// `decimal_value`'s in/out cursor position.
	int8_t decimal_pos = 0;
	/// `decimal_value`'s OLED cursor blink ONLY (`ui/core/state.h:38`; `until_ms` is a phase
	/// anchor, not a deadline).
	deluge::ui::Blink cursor_blink{};
	/// `render_grid`: when the current shortcut-blink cycle started.
	///
	/// One anchor for both phases, because legacy has one counter for both
	/// (`sound_editor.cpp:1012-1038`). Not a `Blink`: a `Blink` carries a single symmetric period,
	/// which is precisely the shape this cycle is not.
	///
	/// Re-anchored whenever `blink_node` changes, which is legacy's `shortcutBlinkCounter = 0` in
	/// `setupShortcutBlink` (`sound_editor.cpp:997-1004`) -- reached from `updatePadLightsFor` on every
	/// screen change (`:806`, via `beginScreen` `:1015`). Without it the port would free-run from
	/// t = 0 and a newly focused pad could light mid-cycle.
	uint32_t blink_anchor_ms = 0;
	/// Which node `blink_anchor_ms` was set for; `render_grid` is the only writer of both.
	const Node* blink_node = nullptr;

	/// Legacy's `secondLayerShortcutsToggled` (`sound_editor.cpp:1364-1371`): pressing the focused
	/// item's own shortcut pad a second time takes the second shortcut layer, and pressing any
	/// other pad clears it. `handle()` is the only writer.
	bool second_layer = false;

	// --- what this tick's render observed, for after_render to act on ---
	/// The widget `Response` the render pass produced (`changed` / `entered`).
	deluge::ui::Response render_resp{};
	/// Whether the render pass moved the value in the edit storage above.
	bool value_changed = false;
	/// Which node `value_changed` belongs to.
	///
	/// An addition to the brief's field list, forced by the horizontal layout: there the node on
	/// screen is the *submenu* and the edited value belongs to the focused column's child, so
	/// "the current node" is not enough to commit through.
	const Node* value_node = nullptr;
	/// Whether `render_oled` read (and therefore claimed) the back button this tick. Only ever
	/// set at `depth > 0` -- see `MenuScene::render_oled`.
	bool back_claimed = false;
	/// A select press on a *value* column of a horizontal page, which `columns` does not own (it
	/// claims the press only for a submenu column, `ui/oled/columns.cpp:419-423`). Legacy's
	/// `HorizontalMenu::handleItemAction` answers it with the notification popup rather than any
	/// navigation (`gui/menu_item/horizontal_menu.cpp:657-661`).
	bool column_press = false;
	/// Spec E §4.1's `MustSelectRange` outcome. E1 recorded it and left the node unentered; E2 task
	/// 9 acts on it -- `enter_child` descends into `nodes::multi_range` instead, which is legacy's
	/// own substitution at `sound_editor.cpp:1857-1858` (`multiRangeMenu.menuItemHeadingTo = newItem`
	/// followed by `newItem = &multiRangeMenu`).
	///
	/// Stays true for as long as the picker is the reason the deeper item has not been opened; the
	/// picker's `select_chain` reads `pending_after_range` to know where to head.
	bool range_picker_pending = false;
	/// The node that will be entered once a range is chosen -- legacy's
	/// `MultiRange::menuItemHeadingTo` (`gui/menu_item/multi_range.h:34`), which is exactly what its
	/// `selectButtonPress` returns (`multi_range.cpp:360-362`).
	const Node* pending_after_range = nullptr;

	/// @brief A range index the focused picker chose this tick, or -1 for none.
	///
	/// The picker is a `CustomItem` and is handed a **`const MenuContext&`**, so it cannot do
	/// legacy's `soundEditor.setCurrentMultiRange(...)` itself; it records the index here and
	/// `after_render` -- which owns the scene's own mutable `ctx_` -- applies it through
	/// `menu::select_range`. Same shape as `pending_scene`: the item asks, `after_render` does.
	int16_t range_choice = -1;

	/// @brief An output index a focused selector chose this tick, or -1 for none.
	///
	/// The same shape as `range_choice`, for the same reason and for the two items `settings.h`'s
	/// own note said were owed one: `cv::Selection` and `gate::Selection` assign
	/// `soundEditor.currentSourceIndex` from their own `selectButtonPress` (`cv/selection.h:42`,
	/// `gate/selection.h:47`), and a `CustomItem` is handed a **`const MenuContext&`**. So
	/// `CvSelectionItem`/`GateSelectionItem` (`nodes/dynamic.h`) record the cursor here and
	/// `after_render` -- which owns the scene's own mutable `ctx_` -- writes
	/// `MenuContext::source_index`, *before* the same tick's select press consults `select_chain`.
	int8_t source_choice = -1;

	/// @brief The last row `render_output_selection` (`nodes/dynamic.cpp`) saw pressed that named a
	///        real physical output -- NOT a mailbox, and deliberately not the same field as
	///        `source_choice` above (task 10, fix round 1 renamed it from `gate_last_real_output`:
	///        `CvSelectionItem` writes it too, harmlessly, since both its rows are real outputs, so
	///        a gate-only name lied about who touches it).
	///
	/// `source_choice`/`ctx.source_index` legitimately becomes `NUM_GATE_CHANNELS` -- the
	/// minimum-off-time sentinel -- for the one tick `gate_selection_chain` routes off it
	/// (`nodes/settings.cpp`), and that value must survive to be read there. But `GateSelectionItem`
	/// ALSO needs "the last real output chosen" once the user has navigated away and come back, for
	/// its `on_focus_backward` -- legacy's own `setValue(soundEditor.currentSourceIndex)` backward
	/// arm, which only ever sees a real output because `selectButtonPress` never writes
	/// `currentSourceIndex` on the off-time branch (`gate/selection.h:44-47`). One field cannot mean
	/// both "whatever was just routed on" and "the last real output, off-time excluded" at once, so
	/// this is the second one: updated on every real-output press and nothing else, read only by
	/// `GateSelectionItem::on_focus_backward`.
	int8_t real_output_choice = 0;

	/// @brief A MIDI cable the focused device list chose this tick, or null for none.
	///
	/// `midi::Devices` assigns `soundEditor.currentMIDICable` from both `beginSession`
	/// (`devices.cpp:55`) and `selectEncoderAction` (`devices.cpp:102`); the port's counterpart is
	/// `MenuContext::midi_cable`, written by `after_render` from what `MidiDevicesItem` records
	/// here. A pointer rather than an index because that is what the context field is, and because
	/// legacy's own index space (`lowestDeviceNum`.. , `devices.cpp:36`) is not the row order the
	/// list renders.
	::MIDICable* midi_cable_choice = nullptr;

	/// @name The patch-cable pages' shared selection -- legacy's three globals, as scene state.
	///
	/// Legacy spreads these across `SoundEditor::patchingParamSelected` (`gui/ui/sound_editor.h`),
	/// `source_selection::regularMenu.s` and `source_selection::rangeMenu.s`
	/// (`gui/menu_item/source_selection.h:36`, one `PatchSource` member per singleton page). They are
	/// genuinely shared *navigation* state -- which param a cable is being made to, which source it
	/// comes from, and which source modulates that source's depth -- read by four different pages,
	/// so they live on `MenuState` beside the path rather than on any one item.
	///
	/// @note `PatchedParam::selectButtonPress` (`gui/menu_item/patched_param.cpp:32-40`) writes
	///       `patching_param` in legacy, on the way out of the param item. `SelectChainFn` takes a
	///       **`const MenuState&`**, so the port cannot write it there; `SourceSelectionItem::on_focus`
	///       does it instead, reading the param out of `path[depth - 1]` -- the node the chain came
	///       from, which `enter_child` has just pushed this page under. Same fact, one hop later.
	/// @{
	/// `soundEditor.patchingParamSelected`; `deluge::modulation::params::kNoParamID` for none.
	uint8_t patching_param = 255;
	/// `source_selection::regularMenu.s` -- the cable's own source.
	PatchSource patch_source = PatchSource::NOT_AVAILABLE;
	/// `source_selection::rangeMenu.s` -- the source modulating *that* cable's depth.
	PatchSource patch_range_source = PatchSource::NOT_AVAILABLE;

	/// @brief `PatchCableStrength::polarity_in_the_ui_` (`gui/menu_item/patch_cable_strength.h:78`):
	///        the polarity the page is *showing*, which a cable that does not exist yet has no home
	///        for.
	///
	/// Legacy keeps it on the item because a `PatchCableStrength` is a singleton with one visit's
	/// worth of state; here the item is `const`, so it lives beside the rest of the page's
	/// navigation state. `on_focus` seeds it (from the cable, or `PatchCable::getDefaultPolarity`),
	/// the render writes it, and `commit` applies it to a cable it has just created -- which is
	/// legacy's `getModelStack(..., allowCreation = true)` doing `setPatchCablePolarity` on the
	/// `!patch_cable_exists_` path (`patch_cable_strength.cpp:223-227`).
	///
	/// @note `Polarity` is **forward declared** at the top of this file rather than included:
	///       `modulation/patch/patch_cable.h` drags in `modulation/automation/auto_param.h`, which
	///       the `scenes-model-stack` ratchet confines to `bindings.cpp` and `context.cpp`, and
	///       which every translation unit that includes this header would otherwise inherit. A
	///       scoped enum with an explicit underlying type is completely specified by its
	///       declaration, so storing one by value needs nothing more.
	Polarity patch_polarity = Polarity{1}; // Polarity::BIPOLAR
	/// @}

	/// @name The MPE pages' shared selection -- legacy's two menu-item members, as scene state.
	///
	/// `mpe::DirectionSelector::whichDirection` (`gui/menu_item/mpe/direction_selector.h:39`) and
	/// `mpe::ZoneSelector::whichZone` (`mpe/zone_selector.h:41`) are members of two singleton menu
	/// items that a **third** page reads: `ZoneNumMemberChannels::getPort()` indexes
	/// `currentMIDICable->ports[directionSelectorMenu.whichDirection]`
	/// (`mpe/zone_num_member_channels.cpp:29-31`) and its read/write/bounds all branch on
	/// `zoneSelectorMenu.whichZone` (`:38`, `:58`, `:68`).
	///
	/// Exactly the shape `patching_param`/`patch_source` above have, and placed here by the same
	/// two-part test (`nodes/midi_device.h` argues it in full, and says why "legacy kept it on a
	/// menu item" is only a proxy -- `MenuContext::midi_cable` is a `soundEditor.current*` member
	/// that a menu item nonetheless writes):
	///
	///   1. Could a `Binding::get`/`set`, handed only a `const MenuContext&`, ever need to see it?
	///      If yes it must be a `MenuContext` field. These two: **no** -- their only readers are
	///      `CustomItem`s, which are handed `MenuState` as well.
	///   2. Does it die when the walk is re-rooted? If yes it belongs here. These two: **yes**.
	///
	/// Which is also why the three MPE items are `CustomItem`s rather than data nodes.
	///
	/// @note Both zero values are 0 -- `MIDI_DIRECTION_INPUT_TO_DELUGE`
	///       (`io/midi/midi_device.h:33`) and `MPE_ZONE_LOWER_NUMBERED_FROM_0` (`:36`) -- which is also
	///       what each selector's `beginSession` forward arm assigns, so the initialiser here and
	///       `retarget`'s reset say the same thing legacy's own entry does.
	/// @{
	/// `directionSelectorMenu.whichDirection`: 0 = into the Deluge, 1 = out of it.
	uint8_t mpe_direction = 0;
	/// `zoneSelectorMenu.whichZone`: 0 = the lower zone, 1 = the upper.
	uint8_t mpe_zone = 0;
	/// @}

	/// @brief A scene a focused `CustomItem` asked the shell to push, or `SceneKind::Null` for
	///        none.
	///
	/// The port of the `openUI(...)` call every legacy bespoke item makes from its own
	/// `beginSession` (`gui/menu_item/edit_name.cpp:54`, `file_selector.cpp`, `loop_point.cpp`,
	/// `sample_marker_editor.cpp`, `dx/browse.cpp`, `osc/audio_recorder.h`). `CustomItem::on_focus`
	/// IS `beginSession` (`node.h`) but returns `void` -- and it has to, because it is reached from
	/// `retarget()` and `restore_state()` as well as from `after_render`, neither of which has a
	/// `Result` to put an `Action` in. So the item writes its request here and `after_render`, which
	/// does have one, drains it into a `Push`.
	///
	/// @note Written by a `CustomItem`, read and cleared by `MenuScene::after_render`, and dropped
	///       by `retarget` along with the rest of the per-visit state.
	deluge::ui::shell::SceneId pending_scene{};

	/// @brief A root-UI action a focused `CustomItem` asked to have performed, or
	///        `MenuAction::None` for none -- the **verb** counterpart of `pending_scene`
	///        (`scenes/menu/action.h`).
	///
	/// `pending_scene` works because everything it can name is inside `src/scenes`. The five items
	/// in `nodes/actions.h` run legacy root-UI behaviour instead -- `openUI(&performanceView)`,
	/// `audioClipView.setClipLengthEqualToSampleLength()`, `stemExport.startStemExportProcess(...)`
	/// -- every one of which names a symbol this layer may not include. So the item names an
	/// *intent* and the application-side wiring performs it.
	///
	/// @note Written by a `CustomItem` (from `on_focus` for the four pure verbs, from `render` for
	///       `PerformEditorItem`), read and cleared by `MenuScene::after_render` into
	///       `MenuScene::take_action()`, and dropped by `retarget` along with the rest of the
	///       per-visit state -- exactly `pending_scene`'s lifecycle.
	MenuActionRequest pending_action{};

	/// @brief Step up one level the next time this scene resumes -- legacy's
	///        `SoundEditor::shouldGoUpOneLevelOnBegin` (`gui/ui/sound_editor.h:113`).
	///
	/// Set when `pending_scene` is pushed, exactly where legacy sets the flag (beside its own
	/// `openUI`), and read in `on_resume`, exactly where legacy reads it
	/// (`SoundEditor::focusRegained`, `sound_editor.cpp:318-321`). Without it, closing the pushed
	/// scene would land back on the item that opened it and `on_focus` would open it again on the
	/// next descent -- which is precisely why legacy has the flag.
	bool go_up_on_resume = false;

	/// @brief The item whose `on_focus` pushed the scene that is now on top -- legacy's
	///        `sampleBrowser.menuItemHeadingTo` (`gui/ui/sound_editor.cpp:2092`).
	///
	/// **Not `MultiRange::menuItemHeadingTo`.** That name resolves on two different objects and
	/// `pending_after_range` above is the *other* one (`gui/menu_item/multi_range.h:34`, written at
	/// `sound_editor.cpp:1857`). This field is the browser's/recorder's pair member, written at
	/// `sound_editor.cpp:2092` and `:2101`, and it is spelled `scene_opener` precisely so the two
	/// never read as the same thing.
	///
	/// Written beside `go_up_on_resume`, exactly where legacy's is written on the way *in* to the
	/// browser, and read by `reroot_to_scene_opener_page()` on the way back out. Legacy stores the
	/// item's *page* alongside it (`parentMenuHeadingTo`, `sound_editor.cpp:2093`) because
	/// `SampleBrowser` cannot reach the horizontal-menus chain from where it reads the pair back;
	/// this scene can walk its own tables, so only half the pair is stored and the other half is
	/// resolved at read time. Dropped by `retarget` with the rest of the per-visit state.
	const Node* scene_opener = nullptr;
};

/// @brief What survives a `replace_root`.
///
/// Child *indices*, not pointers, so the block stays small and a table edit between the save and
/// the restore cannot resurrect a stale pointer -- it can only land on a different child, which
/// the restore clamps.
struct MenuSavedState {
	/// `node_id()` of `path[0]` (`node_id.h`).
	uint16_t root_node_id = 0;
	/// The child index taken at each level: `path_child[i]` indexes `path[i]`'s visible children
	/// to give `path[i + 1]`.
	uint8_t path_child[kMenuDepth] = {};
	uint8_t depth = 0;
	/// `list[depth].cursor` (vertical) or `columns.focused` (horizontal) at save time.
	int16_t focus = 0;
};
static_assert(sizeof(MenuSavedState) <= 32);
static_assert(sizeof(MenuSavedState) <= deluge::ui::shell::kSceneStateBytes); // shell/scene.h:60 -- 256

/// @brief The menu interpreter. One instance for the whole application (`registry.h`'s
///        `menu_scene()`).
class MenuScene final : public deluge::ui::shell::Scene {
public:
	/// @brief Point the scene at @p root, editing what @p ctx resolved, and reset the path.
	///
	/// The entry point for a scene that opens a menu: the caller resolves a `MenuContext`
	/// (`context_for`) and names the root. `on_enter` re-targets afterwards from `SceneId::arg`,
	/// so a push whose argument names a deeper node lands there instead.
	///
	/// @warning **`root` is dead weight for every caller that then pushes with `arg == 0`, and both
	///          of this tree's callers do.** `on_enter` re-roots unconditionally: from
	///          `node_by_id(arg)` when `arg != 0`, and otherwise from `ctx_.root` -- the copy this
	///          function just installed -- so whichever node was passed here is overwritten one
	///          call later. Measured, not reasoned: F1 task 9 mutated
	///          `gui/ui/shell_menu_entry.cpp`'s call to pass `roots[0]` instead of the resolved
	///          root and **nothing in the suite noticed**. The parameter is kept because this is
	///          the only entry point that installs a `MenuContext` at all, and because a scene left
	///          pointing at the wrong root between this call and `on_enter` is a real (if brief)
	///          inconsistency for anything that reads `focused()`. What it is not is a second
	///          chance to get the root right. Splitting the context install from the re-root -- or
	///          asserting here that `root` is the one `ctx.root` names -- is the fix, and it is a
	///          `MenuScene` API change rather than a caller's.
	///
	/// @param root The node to show. See the warning: `on_enter` decides the final one.
	/// @param ctx  What the menu edits.
	void open(const Node& root, const MenuContext& ctx);

	deluge::ui::shell::SceneKind kind() const override;
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	void leds(deluge::ui::LedFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_resume() override;
	void on_model_changed(const deluge::notify::DirtySet& dirty) override;

	/// @brief True: the menu draws over the base view's pads rather than replacing them.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: the base view keeps painting under the menu.
	bool occludes_grid() const override { return false; }
	/// @brief False: legacy greys through `getGreyoutColsAndRows` (`greyout_mask()`), it does not
	///        halve every pad.
	bool dims_grid() const override { return false; }
	/// @brief `SoundEditor::padAction`'s gate at `gui/ui/sound_editor.cpp:1467`, as a declaration.
	///
	/// `inSettingsMenu()` (`:1912-1914`) is one of that gate's three predicates, so on a settings
	/// root legacy never reaches `potentialShortcutPadAction` and every pad -- shift or not --
	/// falls through to the forwarding switch and the tail. The other two predicates name roots
	/// this port does not have yet (`MenuContext::Root` has no note-editor value; sites 12 and 13
	/// are migration §10.2 item 2's remaining work), so `None`'s second producer is future work.
	deluge::ui::shell::GridPadClaim grid_pad_claim() const override {
		return ctx_.root == MenuContext::Root::Settings ? deluge::ui::shell::GridPadClaim::None
		                                                : deluge::ui::shell::GridPadClaim::ShortcutsOnly;
	}
	/// @brief False for every role: a menu covers the Main panel.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	// `input_transparent()` stays at its default false: `MenuScene` IS the `input_top()`
	// (`shell/scene.h:370`), which is what makes its frames the focused ones.

	deluge::ui::grid::GreyoutMask greyout_mask() const override;
	void save_state(deluge::ui::shell::SceneState& state) const override;
	void restore_state(const deluge::ui::shell::SceneState& state) override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief Grid only (E3 task 10 fix round 1): `next_timer()`'s deadline here is purely the
	///        shortcut/source pad blink cadence -- a grid concern -- and the submenu's OLED content
	///        never depends on it. Arming the OLED gates from it too (the fold's original default)
	///        cost nothing in pixels but blitted both panels unconditionally, forever, whenever the
	///        focused node had shortcut or source pads. See `Scene::timer_repaints()`'s own doc.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override { return {.oled = false, .grid = true}; }

	/// @brief The node currently on screen (`path[depth]`), or `nullptr` before `open()`.
	const Node* focused() const;

	/// @brief Re-read the focused node's value from the model -- `MenuItem::readValueAgain`.
	///
	/// The port of the one call legacy makes to it from outside the menu:
	/// `View::modEncoderButtonAction_changeModControllable`'s
	/// `soundEditor.getCurrentMenuItem()->readValueAgain()` (`gui/views/view.cpp:1391-1393`), run
	/// after a gold-knob **push** has changed a `ModControllable` param behind the menu's back. The
	/// edit storage this scene keeps (`MenuState::edit_int`, `enum_index`, …) is loaded on focus,
	/// so without this the menu would keep showing the value it read on the way in.
	///
	/// Spelled as `enter_leaf(node, FocusCause::NoRowPress)` because that is exactly what
	/// `readValueAgain` is -- reload the storage, with nothing pressed on this row (legacy's
	/// `beginScreen`/`focusRegained` shape). A no-op when nothing is focused.
	void read_value_again();
	/// @brief The `ParamDescriptor` MIDI/CC learn (E3) binds to the focused node.
	///
	/// @note **Param learn, not global-command learn.** This is `PatchedParam`/`UnpatchedParam`'s
	///       `getLearningThing()` (`bindings.h`'s `learn_target`): "which automatable parameter
	///       would a CC bind to". The `learn_*` entry points below are the other learn path
	///       entirely -- `midi::Command`'s, which binds a whole MIDI *message* to a
	///       `GlobalMIDICommand` transport action and never touches a `ParamDescriptor`. The
	///       twelve `midi_command_*` nodes have no `bind.param`, so this returns a null descriptor
	///       for every one of them.
	ParamDescriptor learn_target() const;

	/// @name MIDI-learn entry points -- the port of `SoundEditor`'s own three delivery members.
	///
	/// `SoundEditor::noteOnReceivedForMidiLearn` / `midiCCReceived` / `pcReceivedForMidiLearn`
	/// (`gui/ui/sound_editor.cpp:1605-1625`) each forward to `getCurrentMenuItem()->learnX(...)`
	/// and report whether the message was used; `unlearn` is `unlearnAction()` at `:478`. These do
	/// the same, to `focused()`'s `CustomItem` -- the item decides, exactly as legacy's virtual
	/// dispatch let it.
	///
	/// @warning **Nothing calls these yet.** Routing MIDI into the scene stack belongs to the
	///          wiring plan; legacy's `currentUIMode == UI_MODE_MIDI_LEARN` gate and its shift
	///          exclusion (`:1550`, `:1562`) are that plan's, not this layer's -- a scene may not
	///          read a legacy UI mode. What is here is the half a menu owns: which item the message
	///          reaches, and what that item does with it.
	///
	/// @warning **`wants_learn_blink()` is NOT the learn gate.** Legacy enters learn mode on
	///          `getCurrentMenuItem()->allowsLearnMode()` (`sound_editor.cpp:511`) and only then
	///          asks `shouldBlinkLearnLed()` for the LED (`:488`, `:911-916`). They are different
	///          predicates over different sets: `allowsLearnMode` is true on every
	///          `MenuItemWithCCLearning` -- all the patched/unpatched params, the transpose items,
	///          the MIDI-follow items -- every one of which blinks *false*. A wiring author who
	///          gates delivery on `wants_learn_blink()` would silently drop param CC learn and its
	///          `CANT_LEARN` popup along with it. The port has no `allows_learn_mode()` hook yet
	///          because nothing needs one until that plan exists; adding it is that plan's first
	///          job, not a synonym for this.
	/// @{
	/// @brief Offer a note-on to the focused item. @return Whether it was used.
	bool learn_note_on(::MIDICable& cable, int32_t channel, int32_t note);
	/// @brief Offer a CC to the focused item. @return Whether it was used.
	bool learn_cc(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value);
	/// @brief Offer a program change to the focused item. @return Whether it was used.
	bool learn_program_change(::MIDICable& cable, int32_t channel, int32_t program);
	/// @brief Ask the focused item to forget what it learned.
	void unlearn();
	/// @brief Does the focused item want the LEARN LED blinking? `MenuItem::shouldBlinkLearnLed`.
	///
	/// Read by `leds()`, which is the port of `SoundEditor::beginScreen`'s own tail
	/// (`sound_editor.cpp:949-954`). Public because the LED is also legacy's own answer at `:488`,
	/// on the way *out* of learn mode -- **not** because it is the gate into it. See the second
	/// `@warning` above: that gate is `allowsLearnMode()`, which this is not a synonym for.
	bool wants_learn_blink() const;
	/// @}
	/// @brief The application's root menus, indexed by `MenuContext::Root`.
	///
	/// @note All ten entries are filled in (`menu_scene.cpp`'s `kRoots`, Tasks 6-8): the sound
	///       root, the drum/MIDI-drum/gate-drum roots, MIDI-or-CV, the audio-clip, performance,
	///       song and kit-global-FX roots, and the settings root. `on_enter(arg == 0)` therefore
	///       resolves a real root from `MenuContext::root`; a caller may still name a node
	///       directly through `open()` or through `SceneId::arg`.
	static std::span<const Node* const> roots();

	/// @brief The live interpreter state.
	///
	/// Exposed for specs and for E2's range picker (which has to see `range_picker_pending`);
	/// nothing outside may write it.
	const MenuState& state() const { return st_; }

	/// @brief What the menu is currently editing -- the `MenuContext` the last `open()` installed.
	///
	/// Added by F1 task 9, and the reason is a real gap rather than convenience: the application's
	/// entry points resolve this context themselves (`gui/ui/shell_menu_entry.cpp`), and two of its
	/// fields -- `is_session_view` and `is_performance` -- change nothing about which root is opened
	/// and everything about which items are relevant inside it. Without this accessor a caller
	/// passing the wrong one is invisible from outside the scene, so "the session and the arranger
	/// open the same root" could not be told apart from "the session and the arranger are the same
	/// thing". Read-only, like `state()`.
	///
	/// @note Spelled `menu_context` rather than `context` on purpose: CppSpec's `cppspec.hpp`
	///       defines `context` as a macro (`#define context self.context`), so a member of that
	///       name is unusable from every spec in this tree -- which is precisely where this
	///       accessor is meant to be read.
	const MenuContext& menu_context() const { return ctx_; }

	/// @brief Collect the root-UI action a menu item asked for, if any, and clear it.
	///
	/// The application-facing half of the action seam (`scenes/menu/action.h`). `after_render`
	/// drains `MenuState::pending_action` into a one-slot outbox here -- beside where it drains
	/// `pending_scene` into a `Push`, and for the same reason: an item's hook has no `Result` to put
	/// anything in. `gui::ui::dispatchShellActions` (`gui/ui/shell_action_dispatch.h`) polls this
	/// after ticking the shell, and is where each verb's legacy statement lands.
	///
	/// @note **All seven of these actions are performed as of F2 task 7.** The consumer arrived with
	///       F2 task 1 and polls once per guest tick; task 2 gave `OpenPerformanceView` a body,
	///       task 3 `StartStemExport`, and task 4 the three one-line verbs
	///       (`SetClipLengthToSampleLength`, `OpenSongMidiLearn`, `OpenSongMacros`). Tasks 6 and 7
	///       then added two enumerators that did not exist when this note first said "five" --
	///       `SetShortcutsVersion` and `ScreensaverSettingsChanged` -- each with its arm. What each
	///       arm still owes is narrower than "an empty arm" -- it is a named residue per arm -- and
	///       is recorded at the arms themselves (`gui/ui/shell_action_dispatch.h`) rather than here.
	///
	/// @warning **This is a one-slot mailbox with a silent overwrite, not an exactly-once queue.**
	///          Two requests filed between two polls lose the first, and a request nobody polls for
	///          is delivered whenever someone finally does -- arbitrarily later, against whatever
	///          state exists then. `after_render` returns no `Action` announcing that one is
	///          waiting, so nothing enforces the polling: **poll after every tick, or requests are
	///          dropped or stale.**
	///
	///          That is deliberate. Making it exactly-once would mean either a queue (this layer
	///          allocates nothing, so a fixed ring with its own overflow policy -- the same problem
	///          one level down) or a new `Action` kind for the router to carry, which would put a
	///          concept the shell has no business knowing about (a legacy root-UI call) into
	///          `shell/scene.h`. An honest mailbox plus this warning was judged better than a
	///          guarantee that only half holds. **No shipped item can lose a request today, but the
	///          reason is narrower than it used to be and the arithmetic does not carry it.** This
	///          warning used to argue "one tick delivers at most one select press", which was a
	///          complete argument while a select press was the only route in. It is not any more.
	///          There are two: a `CustomItem` writing `pending_action` from `on_focus`/`render`, and
	///          `request_action` below, reached from a `Binding::set` hook by way of `commit_value`.
	///          Each fires at most once per `after_render`, so the ceiling is **two**, not one --
	///          "once each" is exactly the pair this paragraph needs to rule out, and it does not
	///          rule it out.
	///
	///          What rules it out is which items exist. The only caller of `request_action` is
	///          `nodes::set_shortcuts_version` (`menu/nodes/settings.cpp`), and its node is an
	///          ordinary `Kind::Enum` leaf with no `select_chain` and no `CustomItem` under it --
	///          so the `after_render` that commits its value has nothing to descend into, and
	///          cannot also reach an `on_focus` that files. **That is a fact about today's tables,
	///          not an invariant**: an item that filed from both routes in one tick would silently
	///          lose the first, and nothing here would say so.
	///
	/// @return The request, or `MenuAction::None` when none was filed since the last poll. Reading
	///         clears the slot.
	MenuActionRequest take_action();

	/// @brief File @p request on behalf of a hook that is handed no `MenuState` to write.
	///
	/// **The one route into `pending_action` that is not an item writing the field itself**, and it
	/// exists because `Binding::set` is `void (*)(const MenuContext&, int32_t)` (`SetFn`, `node.h`):
	/// a value-write hook gets the context and the value and nothing else. Every other filer is a
	/// `CustomItem`, whose `on_focus`/`render` are handed `MenuState&` and write the field directly
	/// (`nodes/actions.cpp`).
	///
	/// Its only caller today is `nodes::set_shortcuts_version` (`nodes/settings.cpp`), which files
	/// `MenuAction::SetShortcutsVersion` beside its own mirror write -- see `scenes/menu/action.h`'s
	/// "the sixth item" for why that item's write, rather than its press, is what crosses the wall.
	///
	/// @par The timing, which is what makes this safe rather than merely convenient
	///
	/// A hook reaches this through the `menu_scene()` singleton (`scenes/registry.h`) -- the same
	/// route `nodes/context_menu_items.cpp` already takes to `context_menu_scene()`. The only path
	/// that calls a `Binding::set` hook is `commit_value` (`bindings::write`), and `commit_value`'s
	/// one call site is inside `after_render`, **above** the `pending_action` drain. So a request
	/// filed here is drained on the same tick it was filed, by the same `after_render` call, and
	/// never sits in the field across a tick boundary where `retarget` could drop it.
	///
	/// @warning This overwrites whatever is in the field, exactly as an item's own assignment does.
	///          The one-slot mailbox's warning above applies unchanged.
	void request_action(MenuActionRequest request) { st_.pending_action = request; }

	/// @brief Re-root the menu onto the horizontal page that lists `MenuState::scene_opener`, with
	///        that item focused -- `SampleBrowser::claimCurrentFile`'s return affordance
	///        (`gui/ui/browser/sample_browser.cpp:1116-1118`).
	///
	/// **The one thing `MenuState::path`/`depth` may be written for from outside this class**, and
	/// it exists because legacy's answer to "where does accepting a file land you" is spelled in
	/// two `MenuItem*`s no port carries. Legacy's three statements, in order, and what each becomes:
	///
	///   * `parentMenuHeadingTo->focusChild(menuItemHeadingTo)` (`:1116`) -> `columns[0].focused`
	///     set to the item's index among the page's visible children. Legacy's `focusChild` writes
	///     `Submenu::current_item_`, a member of the page object (`gui/menu_item/submenu.cpp:21`);
	///     the port's focus is per *depth* (`MenuState::columns`), so moving the page to depth 0
	///     is what makes this a separate statement rather than something the move carries along.
	///   * `soundEditor.menuItemNavigationRecord[0] = parentMenuHeadingTo` (`:1117`) -> `path[0]`.
	///   * `soundEditor.navigationDepth = 0` (`:1118`) -> `depth = 0`. This is the whole of the
	///     "one extra BACK press" this port used to cost: without it the user is left on the file
	///     selector at whatever depth it was opened from, one level further in than legacy leaves
	///     them -- and, on the shortcut-pad route, on the selector's own bare title screen.
	///
	/// @par Why the page is resolved here rather than carried in
	///
	/// Legacy writes `parentMenuHeadingTo` on the way in because `SoundEditor::maybeGetParentMenu`
	/// (`gui/ui/sound_editor.cpp:2073`) is the only place that has the chain in scope. Its body is
	/// `find_if(chain, [](HorizontalMenu* m) { return m->hasItem(item); })` (`:2084`) over
	/// `getCurrentHorizontalMenusChain(false)` (`:2079`, dispatched on the root at `:2108-2130`).
	/// This scene has the root (`MenuContext::root`) and the tables, so it answers the same
	/// question by walking the current root's own subtree for the `Layout::Horizontal` submenu that
	/// lists the item -- which is `hasItem` with the chain spelled as reachability instead of as a
	/// curated `deluge::vector` (`gui/ui/menus.cpp:2270`). Carrying a resolved page in would have
	/// meant paying that walk on every scene push, including the four that have no return
	/// affordance at all.
	///
	/// @par Why an immediate mutator, and not `request_action`'s mailbox
	///
	/// `request_action` above is a mailbox because its caller is a `Binding::set` hook running
	/// *inside* `after_render`, where the drain it files into is two statements below. This one's
	/// caller is the application, between ticks (`gui::ui::runShellSampleClaimOp`), and the shape
	/// for that is `open()` -- which already installs a `MenuContext` and re-roots `path`/`depth`
	/// from `gui/ui/shell_menu_entry.cpp` with no mailbox in sight. A mailbox would also be wrong
	/// rather than merely heavier: a pending re-root would sit in the field until the next
	/// `after_render`, one frame later, and `retarget` clears the mailboxes (see its body), so any
	/// re-root filed across a re-root would be silently dropped.
	///
	/// @warning Re-rooting abandons the walk that reached the item, exactly as `retarget` does for
	///          `open()` and the shortcut-pad jump -- every per-visit mailbox goes with it.
	///
	/// @return True if the menu moved. False when there is nothing to move: no scene opener
	///         recorded, no path (`on_model_changed`'s song swap), a root out of range, or an item
	///         no horizontal page in this root lists.
	bool reroot_to_scene_opener_page();

private:
	/// @brief Fill @p out with @p node's children that `relevant` admits, resolving
	///        `dynamic_children` first.
	/// @return How many were written.
	size_t visible_children(const Node& node, std::span<const Node*> out) const;
	/// @brief The focused node's `CustomItem`, or `nullptr` when it is not a `Kind::Custom` leaf.
	const CustomItem* focused_custom() const;
	/// @brief @p node's @p index'th visible child, or `nullptr`.
	const Node* child_at(const Node& node, int32_t index) const;
	/// @brief The child the current layout has focus on, or `nullptr`.
	const Node* selected_child(const Node& node) const;

	void render_vertical(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_horizontal(deluge::ui::oled::OledFrame& f, const Node& node);
	// One helper per kind: each owns the widget's own argument shapes rather than sharing an
	// `edit_value` the widgets do not agree on.
	void render_leaf_int(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_decimal(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_enum(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_toggle(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_sync_level(deluge::ui::oled::OledFrame& f, const Node& node);
	void render_leaf_range(deluge::ui::oled::OledFrame& f, const Node& node);

	/// @brief Why a leaf is being focused -- the discriminator legacy spells as
	///        `soundEditor.getCurrentMenuItem() == this` (`gui/menu_item/toggle.h:35-37`).
	///
	/// Legacy reaches a bespoke item's work by two routes and treats them differently:
	/// `selectButtonPress()` (a press on the parent's row) *acts*, while `beginSession()` -- which
	/// `focusRegained` -> `beginScreen` also runs on every re-entry, `sound_editor.cpp:330`,
	/// `:1015` -- only (re-)initialises. `CustomItem::on_focus` is `beginSession`, so a port that
	/// ran it on every focus transition ran *both* meanings, and an item whose `on_focus` stands in
	/// for a press fired without one. This is that lost distinction, restored.
	/// **The question is "a select press on THIS node's own row?", not "a press at all"** (E3 final
	/// review, A6). The values used to be `Descended`/`Restored`, and `Restored` was a lie for one
	/// of the routes it covers: a **shortcut-pad jump is a real press**, just not one on this node's
	/// row. The behaviour was right -- legacy reaches a shortcut jump through `beginSession`, never
	/// `selectButtonPress` -- so the names moved to say what the two values have always meant,
	/// rather than a third cause being invented for a distinction nothing acts on.
	/// `CustomItem::on_focus_is_press()`'s whole contract is then readable from this enum alone:
	/// such an item is given `on_focus` for `RowPress` and withheld it for `NoRowPress`.
	enum class FocusCause : uint8_t {
		/// A select press on this node's own row in its parent's list: `enter_child` ->
		/// `descend_to`, the one path with a press behind it. Legacy's `selectButtonPress` path.
		RowPress,
		/// Focus reached this node by any other route, so nothing pressed its row: `retarget` (a
		/// `SceneId` deep link, the shortcut-pad jump, `open()`), `restore_state`'s rebuild, and
		/// `ascend`'s re-seed. Legacy's `beginScreen`/`focusRegained` path, and exactly the case
		/// `Toggle::selectButtonPress`'s guard exists for. A shortcut jump belongs here **because
		/// its press was on a grid pad, not on this row** -- it is not a "restore", which is what
		/// the old name claimed.
		NoRowPress,
	};

	/// @brief Load @p node's value into the edit storage its kind uses.
	/// @param cause Why this focus is happening -- see `FocusCause`. A `Kind::Custom` node whose
	///              item answers `CustomItem::on_focus_is_press()` is given `on_focus` only for
	///              `FocusCause::RowPress`.
	void enter_leaf(const Node& node, FocusCause cause);
	/// @brief Commit the edit storage for @p node through `bindings::write`.
	void commit_value(const Node& node);
	/// @brief Descend into @p child if its permission allows, opening the range picker for
	///        `MustSelectRange`.
	void enter_child(const Node& child);
	/// @brief Push @p child onto the path with no permission question asked -- the half of
	///        `enter_child` the range-picker substitution reuses.
	void descend_to(const Node& child);
	/// @brief Go up one level, skipping levels whose permission is now `No`
	///        (`sound_editor.cpp:710-721`).
	void ascend();
	/// @brief Re-root the path at @p node (the shortcut jump and `on_enter`'s re-target).
	void retarget(const Node& node);

	MenuState st_{};
	MenuContext ctx_{};

	/// @brief The one-slot outbox `take_action()` empties. Not part of `MenuState`: `retarget`
	///        clears the whole of that on a re-root, and an action already filed must still be
	///        delivered even if the path moves in the same tick.
	MenuActionRequest taken_action_{};
};

} // namespace deluge::scenes::menu
