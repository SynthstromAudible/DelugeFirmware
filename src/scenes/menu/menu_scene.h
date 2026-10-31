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
/// `SoundEditor::selectEncoderAction` (`gui/ui/sound_editor.cpp:1072`), `buttonAction` (`:371`,
/// back -> `goUpOneLevel` `:710`, which exits at depth 0 via `exitCompletely` `:716`),
/// `horizontalEncoderAction` (`:1046`), `padAction` (`:1460`) with `potentialShortcutPadAction`
/// (`:1166`), `enterSubmenu` (`:362`), `setupShortcutBlink` (`:997`) / `blinkShortcut`
/// (`:1012-1038`), `getGreyoutColsAndRows` (`:252`), and the `MultiRange` redirect -> spec E
/// §4.1's `Permission::MustSelectRange`.
///
/// **What this scene may not do.** It never calls an `OledFrame` drawing primitive
/// (`pixel`/`hline`/`rect`/`glyphs`/...): everything on screen comes from a `ui::oled` widget, so
/// the layout rules live in one place and Task 12's ratchet can count primitive calls at zero.
/// It never allocates, never reads a wall clock (the frame's `now_ms()` is the only time it
/// knows), and never mutates the model from `render_oled`/`render_grid` -- `after_render()` and
/// `handle()` are the two places a write may happen.

#include "deluge-project/mapping_table.h"
#include "gfx/oled/icon.h"
#include "notify/dirty_set.h"
#include "scenes/menu/action.h"
#include "scenes/menu/context.h"
#include "scenes/menu/node.h"
#include "scenes/menu/shortcut_blink.h"
#include "shell/scene.h"
#include "ui/core/encoder_acceleration.h"
#include "ui/core/response.h"
#include "ui/core/state.h"
#include "ui/grid/effects.h"
#include "ui/oled/columns.h"

#include <algorithm>
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

/// @brief The most columns one horizontal page materialises: a group lists all its members' columns as one run
///        (`HorizontalMenuGroup::preparePaging`, `horizontal_menu_group.cpp:167-205`), and the largest, the oscillator
///        group, has 41. `menu_scene.cpp` checks every group against it.
inline constexpr size_t kMaxMenuColumns = 48;

/// @brief The debounce shared by SYNTH/KIT/MIDI/CV/CROSS_SCREEN/SCALE on a horizontal page --
///        `HorizontalMenu::buttonAction`'s `navigation_buttons_debounce_threshold`
///        (`horizontal_menu.cpp:78`), 0.14 s as milliseconds. A press is accepted only when
///        strictly more time than this has passed since the last accepted one
///        (`MenuState::nav_press_ms`), matching legacy's `time - last > threshold` (`:81-82`)
///        rather than `>=`.
inline constexpr uint32_t kNavButtonDebounceMs = 140;

/// @brief Storage for one node's name or title with its placeholder filled; keep it alive as long
///        as the view `node_text` returns.
struct NodeText {
	/// The filled text. Longer than any title the OLED can show; a longer one is cut short.
	char chars[40];
	/// The digits fallback for an id with no text (`BoardProfile::text_or_digits`).
	deluge::ui::shell::TextScratch digits;
};

/// @brief The storage one horizontal column's drawing points into; it outlives the `columns` call that draws it.
struct ColumnSlot {
	deluge::ui::oled::MeterColumn meter;
	deluge::ui::oled::TextColumn text;
	deluge::ui::oled::ToggleColumn toggle;
	deluge::ui::oled::ArtColumn art;
	deluge::ui::oled::DrawColumn draw;
	char value[16]{};
	char label[16]{};
	NodeText label_scratch;
	/// `option_list_column`'s call: the node, its context and the value.
	struct OptionList {
		const Node* node = nullptr;
		const MenuContext* ctx = nullptr;
		int32_t value = 0;
	} list;
};

/// @brief `Column::render` for a `Node::option_list_column`: `drawHorizontalLine(kScreenTitleSeparatorY, ...)` and
///        `Selection::drawPixelsForOled` across the panel (`filter_route.h:75-78`, `mod_fx/type.h:73-76`).
/// @param slot_frame The column's value region; the drawing covers the panel below the title.
/// @param focused    Unused.
/// @param ctx        Must point at a `ColumnSlot::OptionList`.
void option_list_column(deluge::ui::oled::OledFrame& slot_frame, bool focused, void* ctx);

/// @brief `cpu::VoiceUsage`'s counters (`cpu/voice_usage.h:94-111`): the last sample, the maximum, and a whole-number
///        average over every sample since the page opened.
class VoiceUsageSampler {
public:
	/// @brief Forget every sample (`VoiceUsage::beginSession`, `:70-73`).
	void reset() { *this = {}; }
	/// @brief Take one sample of @p live voices (`VoiceUsage::sample`, `:94-99`).
	void sample(int32_t live) {
		live_ = live;
		total_ += static_cast<uint64_t>(std::max<int32_t>(live, 0));
		count_++;
		maximum_ = std::max(maximum_, live);
	}
	[[nodiscard]] int32_t live() const { return live_; }
	[[nodiscard]] int32_t maximum() const { return maximum_; }
	/// @brief `averageVoiceCount` (`:101-103`): 0 before any sample.
	[[nodiscard]] int32_t average() const { return count_ == 0 ? 0 : static_cast<int32_t>(total_ / count_); }
	[[nodiscard]] uint32_t samples() const { return count_; }

private:
	int32_t live_ = 0;
	int32_t maximum_ = 0;
	uint64_t total_ = 0;
	uint32_t count_ = 0;
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
	/// A `Binding::adjust` node (spec E-note-family §5.2): the select turn's detents, accumulated
	/// since the last commit rather than folded into a `moved` value the way `edit_int`/
	/// `enum_index` are -- there is no absolute value to stage, only an offset to hand
	/// `bindings::adjust`. `render_horizontal`'s edit block is the only writer; `commit_value`
	/// clears it back to 0 once the offset has been applied.
	int32_t edit_offset = 0;
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
	/// focused. This field's old doc attributed the notification to that call by mistake; a select
	/// press on a value column raises no notification (spec E-note-family §3.2's third bullet).
	bool column_press = false;
	/// Whether shift was held on this tick's frame: a select press with it on an automation item deletes the item's
	/// param's automation instead of navigating (`MenuScene::after_render`).
	bool select_shift = false;
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
static_assert(sizeof(MenuSavedState) <= deluge::ui::shell::kSceneStateBytes); // shell/scene.h:61 -- 256

/// @brief The icon legacy's `renderSubmenuItemTypeForOled` draws at the right of @p node's row: the arrow, or a
///        checkbox for a toggle.
/// @param node The row's node.
/// @param ctx  The context its value is read in.
/// @return The icon, or null for none.
const deluge::gfx::oled::Icon* row_icon(const Node& node, const MenuContext& ctx);

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

	/// @brief Forget every submenu's remembered row, as a boot does: each legacy `Submenu` keeps its
	///        `current_item_` for the life of the firmware, and this scene is as long-lived.
	void forget_rows();

	deluge::ui::shell::SceneKind kind() const override;
	void render_oled(deluge::ui::shell::SurfaceRole role, deluge::ui::oled::OledFrame& frame) override;
	void render_grid(deluge::ui::grid::GridFrame& frame) override;
	void leds(deluge::ui::LedFrame& frame) override;
	deluge::ui::shell::Result after_render() override;
	deluge::ui::shell::Result handle(const deluge::ui::shell::UiEvent& event,
	                                 const deluge::ui::shell::UiContext& ctx) override;
	bool on_enter(const deluge::ui::shell::SceneId& id) override;
	void on_resume() override;
	/// @brief Select acts on its release, as `SoundEditor::buttonAction` does (`sound_editor.cpp:609-614`).
	/// @return True.
	bool select_acts_on_release() const override { return true; }
	/// @brief True in learn mode, which `SoundEditor::buttonAction`'s Select arm does not act in
	///        (`sound_editor.cpp:609-614`).
	bool select_release_blocked() const override { return learn_mode_; }
	void on_model_changed(const deluge::notify::DirtySet& dirty) override;

	/// @brief True: the menu draws over the base view's pads rather than replacing them.
	bool is_grid_overlay() const override { return true; }
	/// @brief False: the base view keeps painting under the menu.
	bool occludes_grid() const override { return false; }
	/// @brief `SoundEditor::padAction`'s gate at `gui/ui/sound_editor.cpp:1467`, as a declaration.
	///
	/// Three predicates there, `!inSettingsMenu() && !inNoteEditor() && !inNoteRowEditor()`, decide
	/// whether `potentialShortcutPadAction` even runs; on any of the three failing, every pad --
	/// shift or not -- falls through to the forwarding switch and the tail instead. `inSettingsMenu`
	/// is `Root::Settings`; Task 6 gives the other two their roots.
	deluge::ui::shell::GridPadClaim grid_pad_claim() const override {
		return (ctx_.root == MenuContext::Root::Settings || ctx_.root == MenuContext::Root::NoteEditor
		        || ctx_.root == MenuContext::Root::NoteRowEditor)
		           ? deluge::ui::shell::GridPadClaim::None
		           : deluge::ui::shell::GridPadClaim::ShortcutsOnly;
	}
	/// @brief False for every role: a menu covers the Main panel.
	bool oled_transparent(deluge::ui::shell::SurfaceRole) const override { return false; }
	// `input_transparent()` stays at its default false: `MenuScene` IS the `input_top()`
	// (`shell/scene.h:445`), which is what makes its frames the focused ones.

	deluge::ui::grid::GreyoutMask greyout_mask() const override;
	void save_state(deluge::ui::shell::SceneState& state) const override;
	void restore_state(const deluge::ui::shell::SceneState& state) override;
	std::optional<uint32_t> next_timer() const override;
	/// @brief Grid only (E3 task 10 fix round 1): `next_timer()`'s deadline here is purely the
	///        shortcut/source pad blink cadence -- a grid concern -- and the submenu's OLED content
	///        never depends on it. Arming the OLED gates from it too (the fold's original default)
	///        cost nothing in pixels but blitted both panels unconditionally, forever, whenever the
	///        focused node had shortcut or source pads. See `Scene::timer_repaints()`'s own doc.
	/// On the voice usage page the timer is also its 500 ms sample, which the panel shows.
	deluge::ui::shell::Scene::RepaintSurfaces timer_repaints() const override;
	/// @brief Advance the voice usage page's sampling clock (`VoiceUsage::timerCallback`, `cpu/voice_usage.h:77-87`).
	void tick(uint32_t dt_ms) override;
	/// @brief The voice usage page's counters.
	const VoiceUsageSampler& voice_usage() const { return voice_usage_; }

	/// @brief The node currently on screen (`path[depth]`), or `nullptr` before `open()`.
	const Node* focused() const;

	/// @brief The value node the select/gold encoders would actually edit right now.
	///
	/// On a `Layout::Horizontal` submenu this is the focused column, *if* that column is not
	/// itself a submenu -- `Submenu::shouldForwardButtons` (`gui/menu_item/submenu.cpp:181-185`):
	/// legacy's same-param popup test (`View::modEncoderAction_forSomeReason`'s guard,
	/// `gui/views/view.cpp:893-900`) forwards through a horizontal page to whichever item the
	/// buttons would actually reach, not the page itself. On any other leaf this is `focused()`
	/// unchanged -- there the leaf IS what the encoders edit. On a vertical submenu, or a
	/// horizontal page focused on a submenu column, there is nothing to edit and this is null.
	///
	/// Added for `services::ModKnobs::popup_suppressed` (`scenes/services/mod_knobs.cpp`), whose
	/// same-param test named `focused()` before this task and forwarded no further than the page
	/// itself on a horizontal menu -- wrong by the same measure `shouldForwardButtons` names.
	const Node* focused_value() const;

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
	///       `GlobalMIDICommand` transport action and never touches a param. The twelve
	///       `midi_command_*` nodes have no `bind.param`, so this returns none for every one of them.
	std::optional<deluge::project::ParamTarget> learn_target() const;

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
	/// @note All twelve entries are filled in (`menu_scene.cpp`'s `kRoots`, Tasks 6-8): the sound
	///       root, the drum/MIDI-drum/gate-drum roots, MIDI-or-CV, the audio-clip, performance,
	///       song and kit-global-FX roots, the settings root, and (Task 6) the note-editor and
	///       note-row-editor roots. `on_enter(arg == 0)` therefore resolves a real root from
	///       `MenuContext::root`; a caller may still name a node directly through `open()` or
	///       through `SceneId::arg`.
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
	///        that item focused -- the sample claim (`SampleClaim`)'s return affordance
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
	///     them -- and, on the shortcut-pad route, out of the menu altogether.
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
	/// caller is the application, between ticks (`gui::ui::shellSampleClaimDone`), and the shape
	/// for that is `open()` -- which already installs a `MenuContext` and re-roots `path`/`depth`
	/// from `gui/ui/shell_menu_entry.cpp` with no mailbox in sight. A mailbox would also be wrong
	/// rather than merely heavier: a pending re-root would sit in the field until the next
	/// `after_render`, one frame later, and `retarget` clears the mailboxes (see its body), so any
	/// re-root filed across a re-root would be silently dropped.
	///
	/// @warning Re-rooting abandons the walk that reached the item, exactly as `retarget` does for
	///          `open()` and the shortcut-pad jump -- every per-visit mailbox goes with it.
	///
	/// @return True if the menu moved. False when there is nothing to move: the `HorizontalMenus` feature off
	///         (`MenuContext::horizontal_menus`), no scene opener recorded, no path (`on_model_changed`'s song swap), a
	///         root out of range, or an item no horizontal page in this root lists.
	bool reroot_to_scene_opener_page();

	/// @brief Prepare the next `on_enter` to open a shortcut pad's @p target where
	///        `SoundEditor::setup(clip, item, sourceIndex)` lands it, and name the node to open.
	///
	/// The page is `shortcut_landing_page(menu_context(), target)`, so `open()` must already
	/// have run. Only the column to focus is recorded; the path moves in `on_enter`.
	///
	/// @param target The node the pad names.
	/// @return The `SceneId::arg` to open: the page's id when the target lands on a page, else the
	///         target's own; 0 when that node has no id.
	uint16_t arm_shortcut_landing(const Node& target);

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
	/// @brief True when `focused()` is a `Layout::Horizontal` submenu -- the gate
	///        `HorizontalMenu::buttonAction`'s six-button arm ports (`handle()`'s button code).
	bool on_horizontal_page() const;
	/// @brief True when @p node renders and edits as a horizontal page: a `Layout::Horizontal` submenu with
	///        `MenuContext::horizontal_menus` on (`HorizontalMenu::renderingStyle()`).
	bool is_horizontal(const Node& node) const;
	/// @brief SHIFT + CROSS_SCREEN/SCALE at depth 0: replace the page with the next (or previous) menu of the root's
	///        chain that has columns, landing on its first page -- mod FX's second -- as `switchHorizontalMenu` does.
	/// @param direction +1 or -1.
	/// @return Whether the page changed.
	bool switch_chained_page(int32_t direction);

public:
	/// @brief The focused horizontal page's paging (empty off one): the page counter and the SCALE/CROSS_SCREEN rule
	///        read the same.
	/// @return Page, total pages and position, as `ui::oled::paging_of` answers for the focused column.
	deluge::ui::oled::Paging horizontal_paging() const;
	/// @brief The focused page's title: on a group (`Node::is_group`) the focused column's member's, as
	///        `HorizontalMenuGroup::getTitle` (`horizontal_menu_group.cpp:77-79`).
	/// @param scratch Storage the returned view may point into.
	/// @return The title text.
	std::string_view page_title(NodeText& scratch) const;

private:
	/// @brief Turn @p node's visible children into a `Column` span carrying label, slots,
	///        relevance and `is_submenu` -- the part of a horizontal page's presentation
	///        `handle()`'s button code needs as well as `render_horizontal`'s full render, factored
	///        so the two read one list rather than two copies of the same rule
	///        (`kid.relevant == nullptr || kid.relevant(ctx_)`, `kid.kind == Kind::Submenu`).
	///        `render`/`ctx` are left null; only the renderer fills those in, and only when
	///        actually drawing.
	/// @param node          The horizontal submenu to list.
	/// @param kids          Written with @p node's visible children, parallel to @p cols.
	/// @param cols          Written with one `Column` per child.
	/// @param label_scratch Backing storage for each column's label text; must outlive @p cols.
	/// @return How many children were written (`visible_children`'s own count).
	size_t build_column_list(const Node& node, std::span<const Node*> kids, std::span<deluge::ui::oled::Column> cols,
	                         std::span<NodeText> label_scratch) const;
	/// @brief Emit the horizontal-page notification for @p column -- the pair
	///        `HorizontalMenu::displayNotification` prints (`horizontal_menu.cpp:335-343`), name
	///        and formatted value on one line. Shared by `after_render`'s value-edit arm and
	///        `handle()`'s button-focus code, which is where legacy's own three call sites are:
	///        `selectEncoderAction` (`:328`), `handleInstrumentButtonPress`'s new-focus tail
	///        (`:437`), and `handleItemAction`'s plain-column arm (`:666`). `handleItemAction`'s
	///        other arm -- a submenu, or an `allowToBeginSessionFromHorizontalMenu` column, already
	///        focused -- calls `enterSubmenu` instead (`:677`) and never reaches
	///        `displayNotification` at all; that is `enter_horizontal_item`, not this method.
	void notify_column(const Node& column);
	/// @brief `checkPermissionToBeginSessionForRangeSpecificParam`'s allocation
	///        (`sound_editor.cpp:1993-1997`) plus legacy's `MUST_SELECT_RANGE` substitution --
	///        `multiRangeMenu.menuItemHeadingTo = newItem; newItem = &multiRangeMenu;`
	///        (`:1857-1858`) -- the one piece `enter_child` (a descent) and `handle()`'s button
	///        focus (legacy's `initializeItem` call, `horizontal_menu.cpp:424`, `:669`) both run,
	///        because both are ways into `MenuItem::checkPermissionToBeginSession`'s range branch.
	///
	///        **Not `Permission::No`.** `enter_child`'s own switch still owns that arm: a descent
	///        refuses outright on `No`, but `HorizontalMenu::handleInstrumentButtonPress` never
	///        consults `initializeItem`'s return for anything but `MUST_SELECT_RANGE` -- a button
	///        focus proceeds regardless of `Yes`/`No`, which is why this helper answers only the
	///        range question and leaves `No` to whichever caller cares.
	/// @param child               The node a session is about to begin on.
	/// @param suppress_when_allowed When true, and `child.allow_session_from_horizontal` is set,
	///         skip the `MUST_SELECT_RANGE` check entirely and answer false -- the port of
	///         `handleInstrumentButtonPress`'s own extra condition
	///         (`!(*current_item_)->allowToBeginSessionFromHorizontalMenu()`, `:427-428`), which
	///         no other caller of this helper has. See `Node::allow_session_from_horizontal`'s own
	///         doc for the two legacy call sites and why only one of them suppresses.
	/// @return True if @p child must not be entered/focused as itself this tick -- the range
	///         picker was opened instead, or the allocation failed and a notice was shown. False
	///         (permission `Yes` or `No`, or the range check was suppressed) means the caller
	///         proceeds with its own next step.
	bool enter_range_picker_if_needed(const Node& child, bool suppress_when_allowed);
	/// @brief `HorizontalMenu::handleItemAction`'s else-path (`horizontal_menu.cpp:669-677`): a
	///        submenu, or an `allow_session_from_horizontal` value column, pressed while already
	///        focused. Runs the range check unconditionally (unlike the button-focus arm) and then
	///        always enters -- `handleItemAction` never reads `Permission::No` at all, unlike
	///        `enter_child`'s general descend.
	/// @param child The already-focused column being pressed again.
	void enter_horizontal_item(const Node& child);

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
	/// The voice usage page's counters, and the time since its last sample; `voice_usage_open_` is whether the
	/// page was focused at the last tick, so that arriving on it starts a fresh session.
	VoiceUsageSampler voice_usage_;
	uint32_t voice_usage_elapsed_ms_ = 0;
	bool voice_usage_open_ = false;
	/// What `render_horizontal`'s columns draw from, here rather than on the stack: a group page has up to
	/// `kMaxMenuColumns` of them, and the scene lives in SDRAM.
	ColumnSlot column_slots_[kMaxMenuColumns];
	/// `HorizontalMenu::acceleration()`: one select-turn acceleration for every horizontal page, as legacy's static is.
	deluge::ui::EncoderAcceleration horizontal_acceleration_{};
	MenuContext ctx_{};

	/// @brief The one-slot outbox `take_action()` empties. Not part of `MenuState`: `retarget`
	///        clears the whole of that on a re-root, and an action already filed must still be
	///        delivered even if the path moves in the same tick.
	MenuActionRequest taken_action_{};

	/// @brief The column the next `on_enter` focuses on the page it opens, set by
	///        `arm_shortcut_landing` (from `handle()`'s jump, alongside the `Replace` it returns, or
	///        from the application before it opens the guest). Consumed by `on_enter`, cleared by
	///        `open()` and `on_resume()`. Not part of `MenuState`: the shell restores that between the
	///        `Replace` and `on_enter`.
	const Node* landing_column_ = nullptr;

	/// @brief The child each submenu was last left on: legacy's per-`Submenu` `current_item_` (`submenu.cpp:9-18`),
	///        which a fresh visit lands on. Once all are in use the least recently recorded entry is overwritten; the
	///        submenus on the current path are recorded on every render, so a root is never the one evicted.
	struct RememberedRow {
		const Node* submenu = nullptr;
		const Node* child = nullptr;
		uint32_t recorded = 0; ///< `remembered_clock_` when last recorded; 0 for an empty entry
	};
	static constexpr size_t kRememberedRows = 128;
	RememberedRow remembered_[kRememberedRows]{};
	uint32_t remembered_clock_ = 0;
	/// @brief `ascend()` went up from depth 0: legacy's `exitCompletely()`, drained by `after_render`.
	bool close_requested_ = false;
	/// @brief Use up the current Select press at the next `after_render`: legacy's `SoundEditor::opened` and
	///        `focusRegained` (`sound_editor.cpp:513-516`, `:531-533`), so the release of a press that opened or
	///        returned to the menu does not act.
	bool use_up_select_ = false;
	/// @brief Select was held during the last render: a value edited under it uses the press up
	///        (`Value::selectEncoderAction`, `menu_item/value.h:64-67`).
	bool select_held_ = false;
	/// @brief Legacy's `UI_MODE_MIDI_LEARN`: entered on a LEARN press with Shift up, no other UI mode on, and an item
	///        that allows learn; left on LEARN's release (`sound_editor.cpp:716-745`). Select's release does nothing in
	///        it.
	bool learn_mode_ = false;

	/// @brief `getCurrentMenuItem()->allowsLearnMode()`: on a page, its selected child's (`Submenu::allowsLearnMode`,
	///        `submenu.cpp:233-238`), which a child page never allows.
	bool allows_learn_mode() const;

	/// @brief Record the selected child of every submenu on the current path; only while `ctx_` is the one the path
	///        was walked with, since reading which children show runs their relevance tests.
	void remember_path();
	/// @brief Put the submenu at the current depth on the child it was last left on, if it still shows it.
	void restore_row();

	/// @brief The node whose shortcut pad blinks and toggles the second layer: a horizontal page's
	///        focused column (`Submenu::updatePadLights`), else `focused()`.
	const Node* pad_subject() const;

	/// @brief Focus @p column on the page `focused()` shows, when the page lists it visibly;
	///        otherwise leave the focus where it is (`Submenu::focusChild`).
	void focus_column(const Node& column);

public:
	/// @name The two navigation steps a caller outside the scene takes (note-family Task 7a)
	///
	/// Declared here, after the members, only so that adding them moved no line of this header that
	/// the tree already cites. They belong beside `read_value_again()` in spirit.
	/// @{
	/// @brief One level up the path -- `SoundEditor::goUpOneLevel` (`gui/ui/sound_editor.cpp:710-729`), for a
	///        caller outside the scene.
	///
	/// For the note-mode handlers that call `soundEditor.goUpOneLevel()` themselves
	/// (`gui/views/instrument_clip_view.cpp`'s `nudgeNotes`, and `SoundEditor::toggleNoteEditorParamMenu`'s
	/// release arm). The step is `ascend()`, which is
	/// already this scene's port of the loop at `:714-722` and of `beginScreen(oldItem)` at `:728`.
	///
	/// @return **false where legacy exits completely instead**, having moved nothing in the first case:
	///         the loop's `if (navigationDepth == 0) { exitCompletely(); return; }` (`:715-717`) fires
	///         either on the first pass, at depth 0, or on a later pass, when the loop has stepped down to
	///         depth 0 and `:720-722` refuses that level too. Closing the whole editor is the
	///         application's act, not the scene's -- `gui::ui::closePortedMenu()` -- so the caller does it
	///         on false. `ascend()` at depth 0 asks for the scene's own close instead, which these callers
	///         do not want, so this takes that request back on false.
	bool go_up_one_level();
	/// @brief Push @p node one level below the focused node -- `SoundEditor::enterSubmenu`
	///        (`gui/ui/sound_editor.cpp:362-369`), for a caller outside the scene.
	///
	/// Added by note-family Task 7a for `SoundEditor::toggleNoteEditorParamMenu`'s press arm
	/// (`:1967-1969`). Legacy's body is `endScreen()`, `navigationDepth++`, the record write and
	/// `beginScreen(navigatedBackwardFrom)` (`:364-368`), and `beginScreen` begins the item's session
	/// without asking `checkPermissionToBeginSession` (`:957-976`) -- so this is `descend_to`, which
	/// asks no permission either, and nothing more. **`menu_context()` is untouched**: unlike `open()`,
	/// this never replaces `ctx_`.
	///
	/// @note **A recorded divergence: the focus the entered page lands on.** Legacy's `beginScreen`
	///       begins the entered item's session (`gui/ui/sound_editor.cpp:959`), and the toggle names no
	///       item to navigate back from (`:1968`). For a `HorizontalMenu` that is
	///       `HorizontalMenu::beginSession` (`gui/menu_item/horizontal_menu.cpp:40-48`) ->
	///       `Submenu::beginSession` (`gui/menu_item/submenu.cpp:11-19`) -> `focusChild(nullptr)`
	///       (`:21-37`), which keeps the process-lifetime object's `current_item_`: a second toggle hold
	///       lands on the column last focused. `descend_to` resets `st_.list[depth]` and
	///       `st_.columns[depth]` (`scenes/menu/menu_scene.cpp:1510-1511`), so this always lands on the
	///       first. It is `src/scenes/README.md:1709-1735`'s carry 5 -- focus kept per depth for a
	///       session, not per menu object for the process -- met by a descend rather than a re-open.
	///       Recorded, not changed.
	void enter_submenu(const Node& node);
	/// @}
};

/// @brief Whether shift + select on @p item deletes its param's automation: a param node, a node whose value is learned
///        as a param (the transposes), or a custom item that says so (the cable strength pages) -- legacy's
///        `Automation` items.
bool is_automation_item(const Node& item);

/// @brief The horizontal page a named @p target opens on under @p root, or `nullptr` when it opens
///        alone -- `SoundEditor::maybeGetParentMenu`.
///
/// Null for the two sample start markers, the audio clip's waveform and the two file selectors,
/// which open their own scenes. Otherwise the first page of @p root's horizontal-menu chain that
/// lists it, the chain transcribed from `SoundEditor::getCurrentHorizontalMenusChain`'s (each group
/// as its member pages; pinned against legacy's search by `shortcuts_spec`).
/// @param root   The menu root.
/// @param target The named node.
/// @return The page, or `nullptr`.
const Node* shortcut_landing_page(MenuContext::Root root, const Node& target);

/// @brief Does @p node render and edit as a horizontal page: a `Layout::Horizontal` submenu with
///        `MenuContext::horizontal_menus` on (`HorizontalMenu::renderingStyle()`, `horizontal_menu.cpp:592-597`)?
/// @param node The node.
/// @param ctx  The menu context.
/// @return Whether it is a horizontal page.
bool renders_horizontal(const Node& node, const MenuContext& ctx);

/// @brief `shortcut_landing_page` for @p ctx's root, and null with `MenuContext::horizontal_menus` off
///        (`getCurrentHorizontalMenusChain` answers nothing).
/// @param ctx    The menu context.
/// @param target The named node.
/// @return The page, or `nullptr`.
const Node* shortcut_landing_page(const MenuContext& ctx, const Node& target);

/// @brief Does @p page list @p column among its children -- a group (`Node::is_group`) through its member pages'
///        children, never the member pages themselves?
/// @param page   A submenu.
/// @param column The child to look for.
/// @return Whether it is listed, relevant or not.
bool page_lists(const Node& page, const Node& column);

/// @brief Whether a shortcut pad's @p target may open in @p ctx, the question `SoundEditor::setup`
///        asks of a named item before it opens anything (`checkPermissionToBeginSession`; on `NO`
///        it shows "PARAMETER NOT APPLICABLE" and opens nothing).
///
/// The base rule is relevance. The file selectors answer for themselves and open on any
/// subtractive sound whatever their relevance (`FileSelector::checkPermissionToBeginSession`), so
/// they are let through here and the browser's own check decides.
/// @param target The node the pad names.
/// @param ctx    What the menu would edit.
/// @return False where legacy refuses.
bool shortcut_applies(const Node& target, const MenuContext& ctx);

/// @brief The address of the param whose automation shift + select on @p item deletes, on the owner @p ctx edits;
///        nullopt where it edits none.
std::optional<deluge::project::ParamAddress> automation_address(const Node& item, const MenuContext& ctx,
                                                                const MenuState& state);

/// @brief @p string_id's text, with `node.title_arg` written into its first `*` the way legacy's
///        `FormattedTitle` writes its argument (`asterixToInt`, `util/functions.h`): a single `*`
///        becomes one digit, `**` becomes the number in at least two digits and ends the text.
/// @param string_id One of @p node's l10n ids, its `name` or its `title`.
/// @param node      The node the text is for; its `title_arg` is the number.
/// @param ctx       The menu context, for a `kTitleArgSourceIndex` argument.
/// @param scratch   Storage the returned view may point into.
/// @return The text, unchanged when @p node has no argument or the text no `*`.
std::string_view node_text(uint16_t string_id, const Node& node, const MenuContext& ctx, NodeText& scratch);

/// @brief The label a horizontal column draws for @p node: legacy's `getColumnLabel` -- its own string
///        (`Node::column_name`), the short form of the name on a node that shortens it (`Node::short_column_label`),
///        or the name.
/// @param node    The column's node.
/// @param ctx     The menu context.
/// @param scratch Storage the returned view may point into.
/// @return The label text.
std::string_view column_label(const Node& node, const MenuContext& ctx, NodeText& scratch);

/// @brief @p node's name or title as legacy's `getName()`/`getTitle()` answer: its `Binding::name_text`, else the
///        static string (with any `title_arg`).
/// @param node    The node.
/// @param use     `ValueTextUse::Name` or `ValueTextUse::Title`.
/// @param ctx     The menu context.
/// @param scratch Storage the returned view may point into.
/// @return The text.
std::string_view display_text(const Node& node, ValueTextUse use, const MenuContext& ctx, NodeText& scratch);

/// @brief The song's input tick magnitude, which legacy's sync-level text is spelled against
///        (`Song::getInputTickMagnitude`); 0 with no model.
/// @param ctx The menu context.
/// @return The magnitude.
int32_t input_tick_magnitude(const MenuContext& ctx);

/// @brief Fill @p col's label and value drawing for @p node showing @p value: legacy's `getColumnLabel`,
///        `showColumnLabel` and `renderInHorizontalMenu` for that item.
/// @param node  The column's node.
/// @param ctx   The menu context.
/// @param value The value to show, in the node's own units (a toggle's is 0 or 1).
/// @param slot  Storage @p col's label and `ctx` point into.
/// @param col   The column; its label, `show_label`, `render` and `ctx` are written.
void present_column(const Node& node, const MenuContext& ctx, int32_t value, ColumnSlot& slot,
                    deluge::ui::oled::Column& col);

/// @brief The value text of the notification an edit on @p column raises: legacy's `getNotificationValue` for that
///        item -- its own (`Binding::value_text`), or its kind's.
/// @param column The column's node.
/// @param ctx    The menu context.
/// @param value  The value to spell, in the node's own units.
/// @param out    Storage the returned view may point into.
/// @return The text; empty for a `Toggle`, whose legacy item spells none.
std::string_view column_notification(const Node& column, const MenuContext& ctx, int32_t value, std::span<char> out);

} // namespace deluge::scenes::menu
