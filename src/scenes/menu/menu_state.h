#pragma once

/// @file
/// @brief The menu's state: the path, the widgets' own state, the mailboxes `after_render` drains, and what
///        `save_state` keeps.

#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/shortcut_blink.h"
#include "shell/scene.h"
#include "ui/core/response.h"
#include "ui/core/state.h"

#include <cstdint>
#include <span>

/// @brief Forward declared for `MenuWalkState::patch_polarity` -- see the note there. Defined in
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

/// @brief The most columns one horizontal page materialises: a group lists all its members' columns as one run
///        (`HorizontalMenuGroup::preparePaging`, `horizontal_menu_group.cpp:167-205`), and the largest, the oscillator
///        group, has 41. `menu_scene.cpp` checks every group against it.
inline constexpr size_t kMaxMenuColumns = 48;

/// @brief What one tick's render observed, for `MenuScene::after_render` to act on: written by the render and the
///        hooks it calls, drained and reset by value at the end of every `after_render`, and on `on_resume` and
///        `retarget`.
struct MenuTick {
	/// A `Binding::adjust` node: the select turn's detents, accumulated
	/// since the last commit rather than folded into a `moved` value the way `edit_int`/
	/// `enum_index` are -- there is no absolute value to stage, only an offset to hand
	/// `bindings::adjust`. `render_horizontal`'s edit block is the only writer; `commit_value`
	/// clears it back to 0 once the offset has been applied.
	int32_t edit_offset = 0;
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
	/// claims the press only for a submenu column, `ui/oled/columns.cpp:451-455`). `after_render`
	/// answers it the way a leaf's own select press is answered, not the way this field's name
	/// suggests: `Submenu::selectButtonPress` forwards to the focused item
	/// (`gui/menu_item/submenu.cpp:187-194`), whose own `select_chain` is entered if it names one
	/// and `ascend()` otherwise -- `SoundEditor::buttonAction`'s SELECT arm
	/// (`gui/ui/sound_editor.cpp:394-424`). **Not** `HorizontalMenu::handleItemAction`
	/// (`gui/menu_item/horizontal_menu.cpp:663-679`), which never runs from a select press at all --
	/// its one caller is the *instrument-button* path, `buttonAction`'s `:96-97`
	/// (`handleInstrumentButtonPress`) reaching it when the pressed button's column is already
	/// focused. A select
	/// press on a value column raises no notification.
	bool column_press = false;
	/// Whether shift was held on this tick's frame: a select press with it on an automation item deletes the item's
	/// param's automation instead of navigating (`MenuScene::after_render`).
	bool select_shift = false;
};

/// @brief What belongs to one walk through the tree from a root: the range picker, the selectors' choices, the
///        patch-cable and MPE pages' selection, and the requests a custom item files. Reset by value whenever the
///        menu re-roots (`MenuScene::retarget`).
struct MenuWalkState {
	/// The `MustSelectRange` permission outcome: `enter_child` descends into `nodes::multi_range` instead, which is
	/// legacy's own substitution at `sound_editor.cpp:1857-1858` (`multiRangeMenu.menuItemHeadingTo = newItem` followed
	/// by `newItem = &multiRangeMenu`).
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
	/// `Kind::Toggle`: `toggle()` takes a `bool&` (`ui/oled/values.h:216-217`).
	bool edit_bool = false;
	/// `Kind::Enum`: `enum_value()` takes an `int32_t& index` ...
	int32_t enum_index = 0;
	/// ... plus its own `ListState` ...
	deluge::ui::ListState enum_list{};
	/// ... plus its own `ScrollText` (`ui/oled/values.h:190-191`).
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
	/// `render_grid`: when the current shortcut-blink cycle started, legacy's
	/// `shortcutBlinkCounter = 0` (`setupShortcutBlink`, `sound_editor.cpp:1190-1196`).
	///
	/// Re-anchored whenever `blink_node` or `blink_plan` changes: legacy sets the blink up again,
	/// zeroing the counter, on every screen change and every re-plan, so a newly focused pad starts
	/// its cycle then rather than wherever a free-running clock happens to be.
	uint32_t blink_anchor_ms = 0;
	/// Which node `blink_anchor_ms` was set for; `render_grid` is the only writer of both.
	const Node* blink_node = nullptr;
	/// What blinks for `blink_node`, as `render_grid` last planned it.
	BlinkPlan blink_plan{};
	/// The counter step `render_grid` last flashed, so each step flashes once however many frames
	/// fall inside it; `UINT32_MAX` for none since the anchor.
	uint32_t blink_fired_step = UINT32_MAX;

	/// Legacy's `secondLayerShortcutsToggled` (`sound_editor.cpp:1364-1371`): pressing the focused
	/// item's own shortcut pad a second time takes the second shortcut layer, and pressing any
	/// other pad clears it. `handle()` is the only writer.
	bool second_layer = false;

	/// The `now_ms` of the last accepted SYNTH/KIT/MIDI/CV/CROSS_SCREEN/SCALE press -- legacy's
	/// `last_navigation_buttons_press_time`, a `static double` shared by all six buttons
	/// (`gui/menu_item/horizontal_menu.cpp:77`). `handle()`'s button arm is the only writer,
	/// updated only on a press it accepts (`kNavButtonDebounceMs` below governs the accept test),
	/// exactly where legacy's own assignment sits (`:102`, after the two `if` arms and before
	/// returning `DEALT_WITH`). Zero-initialised, matching legacy's own `0.0`, so the first press
	/// of a session is always accepted.
	uint32_t nav_press_ms = 0;

	// --- what this tick's render observed, for after_render to act on ---
	/// What this tick's render observed; `after_render` drains it and resets it by value.
	MenuTick tick{};
	/// What belongs to the current walk from a root; `retarget` resets it by value.
	MenuWalkState walk{};

	/// @brief The last row `render_output_selection` (`nodes/dynamic.cpp`) saw pressed that named a
	///        real physical output -- NOT a mailbox, and deliberately not the same field as
	///        `source_choice` above (`CvSelectionItem` writes it too, harmlessly, since both
	///        its rows are real outputs, so the name is not gate-specific).
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

	/// @}

	/// @}
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
static_assert(sizeof(MenuSavedState) <= deluge::ui::shell::kSceneStateBytes); // shell/scene.h:61 -- 256

} // namespace deluge::scenes::menu
