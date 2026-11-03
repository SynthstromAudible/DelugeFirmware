/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 *
 * The Synthstrom Audible Deluge Firmware is free software: you can redistribute it and/or modify it under the
 * terms of the GNU General Public License as published by the Free Software Foundation,
 * either version 3 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 * without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program.
 * If not, see <https://www.gnu.org/licenses/>.
 */

/// @file
/// @brief The menu interpreter. See `menu_scene.h` for what it is and which legacy routines it
///        ports.
///
/// **Where each thing happens, and why.** The toolkit's rule is that a widget draws and consumes
/// its inputs in the same call (`ui/core/frame_context.h`, rule 2: *reading is claiming*), so
/// `render_oled` is where input is observed -- but it may not touch the model. `after_render()`
/// is therefore where a `Response` becomes navigation or a `bindings::write`, and `handle()` is
/// left with only the two things a widget can never claim: a pad press (positional, never masked
/// -- rule 4) and a button nothing owned.

#include "scenes/menu/menu_scene.h"

#include "scenes/menu/nodes/nodes.h" // `roots()`' table

#include "scenes/bind/dispatch.h"
#include "scenes/browser/browser_scene.h" // on_resume: a chosen file's claim re-roots the menu
#include "scenes/menu/node_id.h"

#include "notify/notice_queue.h"
#include "shell/input_lowering.h" // claim_covers -- MenuScene::owns

#include "gui/l10n/strings.h"
#include "notify/dirty_set.h"
#include "scenes/menu/bindings.h"
#include "scenes/menu/chains.h"

#include <span>
#include <utility>

namespace deluge::scenes::menu {

using deluge::ui::shell::Action;
using deluge::ui::shell::ButtonId;
using deluge::ui::shell::Result;
using deluge::ui::shell::SceneId;
using deluge::ui::shell::SceneKind;
using deluge::ui::shell::SceneState;
using deluge::ui::shell::SurfaceRole;
using deluge::ui::shell::TextScratch;
using deluge::ui::shell::UiContext;
using deluge::ui::shell::UiEvent;

using namespace detail;

// ---------------------------------------------------------------------------------------------
// Lifecycle

void MenuScene::open(const Node& root, const MenuContext& ctx) {
	ctx_ = ctx;
	st_ = MenuState{};
	visit_ = {}; // `blinkLed(IndicatorLED::BACK)` as the editor opens (`sound_editor.cpp:571`), among the rest
	pads_.forget_landing();
	retarget(root);
}

SceneKind MenuScene::kind() const {
	return SceneKind::FirstApp;
}

std::span<const Node* const> MenuScene::roots() {
	// One slot per `MenuContext::Root`, and every one of them filled.
	// This array is `on_enter`'s "arg 0 means the root the context chose" lookup, and adding or
	// re-pointing a root is a change to this array alone.
	// One node per `MenuContext::Root`, in enumerator order. `Root::MidiOrCv` holds the
	// MIDI-titled spelling of that root -- the title `menus.cpp`'s constructor gives it; a caller
	// on a CV output takes `nodes::midi_or_cv_root(ctx)` instead (`nodes/roots.h`'s note).
	// `NoteEditor`/`NoteRowEditor` are the last two: a menu opened with `ctx.root` set to either
	// (`openShellMenu`'s `NoteMode`) re-roots here through `on_enter`'s `arg == 0` path
	// (`MenuScene::open`'s warning), exactly as every other root does.
	static const Node* const kRoots[static_cast<size_t>(MenuContext::Root::Count)] = {
	    &nodes::sound_root,         &nodes::root_drum,       &nodes::root_midi_or_cv,  &nodes::root_midi_drum,
	    &nodes::root_gate_drum,     &nodes::root_audio_clip, &nodes::root_performance, &nodes::root_song,
	    &nodes::root_kit_global_fx, &nodes::root_settings,   &nodes::note_editor_root, &nodes::note_row_editor_root,
	};
	return kRoots;
}

void MenuScene::retarget(const Node& node) {
	walk_.retarget(core(), visit_.session, node);
	voice_usage_.leave();
}

void MenuScene::reset_process_state() {
	walk_.forget_rows();
	view_.forget_acceleration();
	voice_usage_ = VoiceUsageClock{};
}

bool MenuScene::on_enter(const SceneId& id) {
	// `SceneId::arg` is deliberately NOT part of the `SceneState` cache key (`shell/scene.h:68`),
	// so a re-entry restores the kind's last saved state and *then* lands here: this must
	// overwrite `path`/`depth` from `arg` rather than trust what `restore_state` put there.
	const Node* target = nullptr;
	const Node* page = nullptr;
	const Node* const column = pads_.take_landing();
	if (id.arg != 0) {
		target = node_by_id(id.arg);
		// A shortcut landing names the page; the permission and range gate below are the column's,
		// as `setup()` asks them of the item before it looks at the page.
		if (target != nullptr && column != nullptr && page_lists(*target, *column)) {
			page = target;
			target = column;
		}
		// **The allocation legacy's permission check performs before it opens a named item.**
		// `SoundEditor::setup(clip, item, sourceIndex)` runs
		// `item->checkPermissionToBeginSession(...)` on the item it was asked for
		// (`gui/ui/sound_editor.cpp:1845-1846`), and for every range-dependent item that lands in
		// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`, whose **first
		// statement** is `source->getOrCreateFirstRange()` (`:1993`) -- a *create*, not a read,
		// which fails only with `INSUFFICIENT_RAM` (`:1994-1997`).
		//
		// `enter_child` (below) already runs it for a descent, which covers everything
		// reached by walking the menu. A deep link does not descend: it re-roots. So site 8 of
		// `gui/ui/shell_menu_entry.cpp`'s table -- `soundEditor.setup(getCurrentInstrumentClip(),
		// &menu_item::file0SelectorMenu, 0)`, `gui/views/instrument_clip_view.cpp:515` -- and the
		// shortcut-pad jump, which comes back through here as an `Action::Kind::Replace`
		// (`handle`'s `Replace` arm), both arrived with no range selected on a source with no
		// ranges yet. **Measured** at level 3 on a fresh synth clip: the toggle-OFF leg opened the
		// sample browser and the toggle-ON leg did not, because `browser::sample_can` refuses a
		// null `AudioFileHolder` (`scenes/browser/browser_scene.cpp:152-164`) and the push was
		// declined, leaving the menu parked on the file selector's own title page.
		//
		// **The range gate in full, but NOT `permission_for` around it.** Legacy's gate here is the
		// *item's own* `checkPermissionToBeginSession` override, and the five scene-opener items
		// override it in a way that never consults `isRelevant`:
		// `FileSelector::checkPermissionToBeginSession` (`gui/menu_item/file_selector.cpp:68-86`)
		// answers YES for any SUBTRACTIVE sound and defers the rest to the range check, while
		// `FileSelector::isRelevant` (`:54-67`) additionally demands `oscType == SAMPLE`.
		// `permission_for` is the port of the *base* `MenuItem::checkPermissionToBeginSession`
		// (`gui/menu_item/menu_item.cpp:27-31`, relevance and nothing else) plus the range rule, so
		// calling it here would refuse site 8 on a default SAW synth that legacy opens. That gap --
		// `Node` has no port of an item-specific `checkPermissionToBeginSession` that bypasses
		// relevance -- is recorded in `src/scenes/README.md` rather than half-closed here.
		//
		// What is NOT skipped is the range half, both of its arms. Paying only the allocation and
		// dropping `MUST_SELECT_RANGE` would be a partial port of a two-armed gate, which is worse
		// than paying neither: the half that is there makes it look complete. It is latent on a
		// fresh source (0 ranges becomes 1, so the item just opens) and fires the moment a user
		// presses LOAD on a multisampled oscillator.
		if (target != nullptr && target->range_dependent) {
			if (!ensure_first_range(ctx_)) {
				deluge::notify::notice_string(
				    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ERROR_INSUFFICIENT_RAM));
				return false;
			}
			// `:2005-2014`, the two arms after that allocation. `ensure_first_range` has already
			// applied the first -- a kit row or a single range is unambiguous, so it points
			// `ctx_.range_index` at range 0 (`:2005-2007`). What is left is legacy's own
			// `MUST_SELECT_RANGE` (`:2014`): a range-dependent item on a source with more than one
			// range and none remembered. Spelled as "the allocation did not resolve one" rather
			// than by calling `permission_for`, whose first statement is the relevance fold above.
			// A context with no sound took `ensure_first_range`'s no-op success and must not land on a
			// picker.
			if (ctx_.addresses.slot && ctx_.range_index < 0) {
				// `sound_editor.cpp:1855-1859`: `multiRangeMenu.menuItemHeadingTo =
				// horizontal_menu_child; newItem = &multiRangeMenu;` -- the picker is opened
				// *instead of* the item, and the item is remembered for the picker's own select
				// press. `enter_child` ports the same two statements for a descent
				// (`descend_to(nodes::multi_range)` below); this is the deep link's copy, and it
				// re-roots rather than descending because there is no path to descend from.
				// The two writes come after `retarget`, which resets both fields.
				visit_ = {};
				retarget(nodes::multi_range);
				st_.walk.range_picker_pending = true;
				st_.walk.pending_after_range = target;
				return true;
			}
		}
	}
	else {
		// Arg 0 means "the root this context chose" -- the fallback through
		// `roots()`. It refuses (returns false below) only if `ctx_.root` is out of range.
		const std::span<const Node* const> r = roots();
		const size_t i = static_cast<size_t>(ctx_.root);
		target = i < r.size() ? r[i] : nullptr;
	}
	if (target == nullptr) {
		return false;
	}
	if (page != nullptr) {
		visit_ = {};
		retarget(*page);
		walk_.focus_column(core(), *target);
		return true;
	}
	// `beginScreen`: a new screen, with `blinkLed(IndicatorLED::BACK)` (`sound_editor.cpp:571`). Reset only on a
	// screen that opens: a refused id leaves the menu the visit it was, as the shell rolls the move back.
	visit_ = {};
	retarget(*target);
	return true;
}

uint16_t MenuScene::arm_shortcut_landing(const Node& target) {
	return pads_.arm_landing(ctx_, target);
}

void MenuScene::on_resume() {
	visit_.blink_restart =
	    true; // `focusRegained`'s `setLedStates` -> `blinkLed(IndicatorLED::BACK)` (`sound_editor.cpp:552`)
	// `SoundEditor::focusRegained` (`gui/ui/sound_editor.cpp:317-327`): "If just came back from a
	// deeper nested UI", step up one level. The flag is set where the push was requested (see
	// `after_render`), and it is what stops the item that opened the pushed scene from opening it
	// again the instant the user gets back.
	// A landing armed by a jump whose `Replace` never reached `on_enter` is stale.
	pads_.forget_landing();
	visit_.use_up_select = true;
	if (st_.walk.go_up_on_resume) {
		st_.walk.go_up_on_resume = false;
		// A browser that closed on a chosen file has a claim coming, and the claim lands the menu on the source page
		// (`SampleClaim::stepFinish` closes the browser and reopens the editor there, `sample_claim.cpp:563-577`;
		// here `reroot_to_scene_opener_page`). Going up from depth 0 first would close the menu under it.
		if (!(st_.depth == 0 && browser::browser_scene().has_choice())) {
			walk_.ascend(core(), visit_.session);
		}
	}
	// Otherwise the path is kept exactly as it was. Reset only the per-tick observations, which
	// belong to a render pass that never happened.
	st_.tick = {};
}

bool MenuScene::reroot_to_scene_opener_page() {
	// Read before `retarget` runs, which drops it along with the rest of the abandoned walk.
	const Node* item = st_.walk.scene_opener;
	if (item == nullptr || st_.path[0] == nullptr) {
		// No opener recorded, or no menu at all (`on_model_changed`'s song swap empties the path).
		// Legacy's own guard is the same shape -- the pair is only read when both halves are
		// non-null (`gui/ui/browser/sample_browser.cpp:1111`).
		return false;
	}
	// With the feature off legacy's chain is `nullopt`, so `maybeGetParentMenu` writes no pair and `claimCurrentFile`
	// leaves the menu where the browser was opened from.
	if (!ctx_.horizontal_menus) {
		return false;
	}
	const std::span<const Node* const> r = roots();
	const size_t i = static_cast<size_t>(ctx_.root);
	if (i >= r.size() || r[i] == nullptr) {
		return false;
	}
	// `maybeGetParentMenu`'s search (`gui/ui/sound_editor.cpp:2079-2087`). Legacy answers `nullptr`
	// when no menu in the chain lists the item, and its caller then does nothing at all -- which is
	// this `false`.
	const Node* page = horizontal_page_listing(*r[i], *item);
	if (page == nullptr) {
		return false;
	}
	// `soundEditor.menuItemNavigationRecord[0] = parentMenuHeadingTo; soundEditor.navigationDepth =
	// 0` (`gui/ui/browser/sample_browser.cpp:1117-1118`). `retarget` is the port's spelling of both
	// at once, and it is the right one rather than a convenient one: legacy's two statements throw
	// away everything above index 0 of the record, which is exactly the per-visit state `retarget`
	// drops (see its body).
	retarget(*page);
	// `parentMenuHeadingTo->focusChild(menuItemHeadingTo)` (`:1116`). It is a separate statement
	// here for the reason the header gives -- legacy's focus lives on the page object, the port's
	// lives at the page's *depth* -- and it runs after `retarget`, which resets `columns[0]`.
	// Legacy discards `focusChild`'s bool at this site (it is only consulted at
	// `gui/ui/sound_editor.cpp:1860`), so an item its page no longer shows simply leaves the page
	// focused on its first column, which is what falling out of this loop does.
	walk_.focus_column(core(), *item);
	return true;
}

void MenuScene::on_model_changed(const deluge::notify::DirtySet& dirty) {
	// A song swap invalidates every pointer in `ctx_`. The application re-resolves the context and
	// calls `open()` again; until it does, refuse to render anything by dropping the path, which
	// is strictly safer than drawing a menu over a freed `Sound`.
	if (dirty.has(deluge::notify::Dirty::SongSwapped)) {
		st_ = MenuState{};
		ctx_ = MenuContext{};
	}
}

const Node* MenuScene::focused() const {
	return menu::focused(st_);
}

void MenuScene::read_value_again() {
	visit_.session.read_again(core());
}

std::optional<deluge::project::OwnerParamTarget> MenuScene::learn_target() const {
	return visit_.learn.target(ctx_, st_);
}

bool MenuScene::learn_note_on(::MIDICable& cable, int32_t channel, int32_t note) {
	return visit_.learn.note_on(st_, cable, channel, note);
}

bool MenuScene::learn_cc(::MIDICable& cable, int32_t channel, int32_t cc, int32_t value) {
	return visit_.learn.cc(st_, cable, channel, cc, value);
}

bool MenuScene::learn_program_change(::MIDICable& cable, int32_t channel, int32_t program) {
	return visit_.learn.program_change(st_, cable, channel, program);
}

void MenuScene::unlearn() {
	visit_.learn.unlearn(ctx_, st_);
}

bool MenuScene::wants_learn_blink() const {
	return visit_.learn.wants_blink(st_);
}

// ---------------------------------------------------------------------------------------------
// Tree walking

const Node* MenuScene::selected_child(const Node& node) const {
	return menu::selected_child(ctx_, st_, node);
}

const Node* MenuScene::focused_value() const {
	return menu::focused_value(ctx_, st_);
}

// ---------------------------------------------------------------------------------------------
// Rendering. Draws AND consumes input (the toolkit's rule); never touches the model.

void MenuScene::render_oled(SurfaceRole role, deluge::ui::oled::OledFrame& f) {
	// One `oled_role_by_index` entry on this board, so every other role is somebody else's panel.
	// Returning before anything is read keeps the claims clean too.
	if (role != SurfaceRole::Main) {
		return;
	}
	visit_.select_held = f.select_held();
	// A horizontal page's `initializeItem` on every focus move and `beginSession`, read before its columns draw.
	if (const Node* const value = focused_value(); value != visit_.session.value()) {
		visit_.session.begin(core(), value);
	}
	const Node* node = focused();
	if (node == nullptr) {
		return;
	}
	// `beginScreen`'s `blinkLed(LEARN)` for an item that wants it (`sound_editor.cpp:1167-1169`) restarts the shared
	// phase, BACK's too, at this tick's `leds()`; the open's and LEARN's release are filed the same way.
	if (node != visit_.blink_screen) {
		visit_.blink_screen = node;
		visit_.blink_restart = visit_.blink_restart || wants_learn_blink();
	}

	view_.render(core(), f, *node);
}

void MenuScene::render_grid(deluge::ui::grid::GridFrame& f) {
	// The shift level, latched or not, kept in step here as well as from `handle`'s events. A covered frame reads no
	// input, and a menu under another scene shows no overlay.
	if (!f.covered) {
		visit_.shift_held = f.shift();
		if (overlay_up()) {
			render_overlay(f, ctx_, st_, focused_value());
		}
	}
	// The blinks flash over the overlay: the control surface plays them over whatever the pads hold.
	pads_.render_grid(core(), f);
}

void MenuScene::leds(deluge::ui::LedFrame& f) {
	// Button LEDs only: `LedFrame` has no pad concept whatever, so the pad blink above is a pad
	// flash rather than an LED declaration. The set is `SoundEditor::setLedStates`
	// (`sound_editor.cpp:339-352`), minus the parts that belong to services (`playbackHandler`,
	// `view.setKnobIndicatorLevels`).
	// Nothing to declare while the path is empty: `on_model_changed(SongSwapped)` drops it and the
	// scene draws nothing until the application re-`open()`s, so announcing the menu's LED set
	// would light a menu that is not on screen.
	if (focused() == nullptr) {
		return;
	}
	constexpr deluge::ui::LedState kOff{deluge::ui::LedState::Kind::Off, 0, 0};
	// `Save` is never a horizontal-page LED in legacy either -- unaffected by anything below.
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Save), kOff);

	page_button_leds(ctx_, st_, f);

	// `blinkLed`'s default blink, on the phase every normal blink shares (`indicator_leds.cpp:78-95`).
	if (visit_.blink_restart) {
		visit_.blink_restart = false;
		f.restart(deluge::ui::BlinkRate::Normal);
	}
	const deluge::ui::LedState shared_blink = deluge::ui::LedState::shared(deluge::ui::BlinkRate::Normal);
	(void)f.set(static_cast<deluge::ui::LedId>(ButtonId::Back), shared_blink);
	visit_.learn.leds(st_, f, shared_blink);
}

// ---------------------------------------------------------------------------------------------
// Navigation and commits: the one place the render pass's observations touch the model.

MenuActionRequest MenuScene::take_action() {
	const MenuActionRequest taken = taken_action_;
	taken_action_ = MenuActionRequest{};
	return taken;
}

Result MenuScene::after_render() {
	Result r;
#ifdef DELUGE_HOST
	drain_trace_len_ = 0;
#endif
	// A `Value`-derived bespoke item's cursor moved under a held Select is a value edited under it.
	const Node* const shown = focused();
	const bool value_turned = shown != nullptr && shown->kind == Kind::Custom && shown->custom != nullptr
	                          && shown->custom->is_value() && st_.tick.render_resp.changed;
	if (std::exchange(visit_.use_up_select, false)
	    || ((st_.tick.value_changed || value_turned) && visit_.select_held)) {
		(void)r.actions.push_back(Action{.kind = Action::Kind::UseUpSelect});
	}
	if (walk_.take_close_request()) {
		// `ascend()` went up from depth 0 since the last render (`on_resume`'s go-up): `exitCompletely()`.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = SceneKind::FirstApp});
		return r;
	}
	const Node* node = focused();
	if (node == nullptr) {
		return r;
	}

	// What was on screen when this tick's render ran. Navigation below moves the path *after* the
	// render, so the panel is still showing the old screen and the next tick has to be asked for.
	const Node* const drawn = node;
	bool repaint = false;

	// The range picker's own commit -- `soundEditor.setCurrentMultiRange(...)`
	// (`sound_editor.cpp:1983-1986`), which `MultiRangeItem` cannot do itself: it is handed a
	// `const MenuContext&` and this is the scene's own mutable one. Applied on every tick the picker
	// republishes its cursor, which is exactly when legacy calls it (`multi_range.cpp:54`, `:249`).
	if (st_.walk.range_choice >= 0) {
		trace_drain(DrainBand::RangeChoice);
		select_range(ctx_, st_.walk.range_choice);
		st_.walk.range_choice = -1;
	}

	// The CV/gate selectors' own commit -- `soundEditor.currentSourceIndex = this->getValue()`
	// (`cv/selection.h:42`, `gate/selection.h:47`) -- and the device list's
	// `soundEditor.currentMIDICable = getCable(...)` (`midi/devices.cpp:55`, `:102`). Same shape as
	// `range_choice`, and deliberately **above** the `entered` arm below: legacy assigns
	// `currentSourceIndex` on the way through `selectButtonPress`, so `gate_selection_chain` and
	// `gate_mode_names` must both see the output the user just picked, not the previous one.
	if (st_.walk.source_choice >= 0) {
		trace_drain(DrainBand::SourceChoice);
		ctx_.source_index = st_.walk.source_choice;
		st_.walk.source_choice = -1;
	}
	if (st_.walk.midi_cable_choice != nullptr) {
		trace_drain(DrainBand::MidiCableChoice);
		ctx_.midi_cable = st_.walk.midi_cable_choice;
		st_.walk.midi_cable_choice = nullptr;
	}

	if (st_.tick.value_changed && st_.tick.value_node != nullptr) {
		trace_drain(DrainBand::ValueCommit);
		visit_.session.commit(core(), *st_.tick.value_node);
		// The write raises its own `notify::Dirty` facts (`bindings.cpp`), which repaint the
		// surfaces those name; `RepaintAll` covers what a commit changed *off* this surface (a
		// mod-knob indicator, the automation grid). Never `Action{Repaint, SurfaceRole::None}`,
		// which is a documented no-op (`shell/scene.h:206`).
		repaint = true;
	}

	// The horizontal menu's notification popup, `HorizontalMenu::displayNotification`
	// (`gui/menu_item/horizontal_menu.cpp:335-343`): it shows the item's *name* alongside its
	// value. Legacy raises it on a value change (`:326-328`, `selectEncoderAction`) and on a
	// button focus move or an already-focused button press that does not open anything
	// (`handleInstrumentButtonPress`'s tail `:437`, `handleItemAction` `:666` -- both ported in
	// `handle()`'s button code, which calls `notify_column` itself). **Not** on a select press on
	// a value column (`Submenu::selectButtonPress` never calls `displayNotification` at all --
	// see `MenuTick::column_press`'s doc).
	//
	// `notify_column` is `notice_text`'s call, factored out so `handle()`'s button code raises the
	// same notification through the same text-building rules -- see its own doc for why the
	// legacy call sites split across two functions of this class.
	const bool horizontal = is_horizontal(*node);
	if (horizontal && (st_.tick.render_resp.changed || st_.tick.value_changed)) {
		if (const Node* column = selected_child(*node); column != nullptr && column->kind != Kind::Submenu) {
			notify_column(ctx_, *column);
		}
	}

	// `Automation::selectButtonPress` (`gui/menu_item/automation/automation.cpp:38-58`): shift + select on an
	// automation item deletes its param's automation, raises the popup where there was a param, and stays put -- on a
	// leaf, and on a horizontal page's value column, which `Submenu::selectButtonPress` forwards the press to.
	bool deleting_automation = false;
	if (st_.tick.select_shift && (st_.tick.render_resp.entered || st_.tick.column_press)) {
		const Node* item =
		    st_.tick.render_resp.entered ? (node->kind == Kind::Submenu ? nullptr : node) : selected_child(*node);
		if (item != nullptr && is_automation_item(*item)) {
			deleting_automation = true;
			const std::optional<deluge::project::ParamAddress> address = automation_address(*item, ctx_, st_);
			if (address && ctx_.sink != nullptr && ctx_.sink->delete_automation(*address)) {
				(void)r.actions.push_back(
				    Action{.kind = Action::Kind::Popup,
				           .popup_string = static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_AUTOMATION_DELETED)});
				repaint = true;
			}
		}
	}

	if (deleting_automation) {
		// No navigation: legacy's shift arm answers NO_NAVIGATION.
	}
	else if (st_.tick.render_resp.entered) {
		trace_drain(DrainBand::Navigation);
		if (node->kind == Kind::Submenu) {
			if (const Node* child = selected_child(*node); child != nullptr) {
				walk_.enter_child(core(), visit_.session, *child);
			}
		}
		else if (const Node* chained = node->select_chain != nullptr ? node->select_chain(ctx_, st_) : nullptr) {
			// `MenuItem::selectButtonPress` returning a menu: `SoundEditor` navigates *into* it
			// rather than going up (`gui/ui/sound_editor.cpp:394-424` -> `enterSubmenu`, which is
			// `navigationDepth++` and a record write, exactly what `enter_child` does). The node's
			// own hook answers, because the answer is value-dependent for two of the seven ported
			// overrides -- see `node.h`'s `SelectChainFn`.
			//
			// `enter_child` applies legacy's `maybeGetParentMenu`/`focusChild` reparenting: an item a chained page
			// lists is entered on that page, focused on it.
			walk_.enter_child(core(), visit_.session, *chained);
		}
		else {
			// A leaf's select press goes up, it does not re-enter: `MenuItem::selectButtonPress`
			// returns nullptr and `SoundEditor` reads that as "up". `toggle`'s own doc says the
			// same thing from the widget's side -- a press reports `entered` and leaves `changed`
			// false, so this must not flip the value (`ui/oled/values.h:202-206`).
			walk_.ascend(core(), visit_.session);
		}
	}
	else if (st_.tick.column_press) {
		// `Submenu::selectButtonPress` forwarding to the focused item, on a horizontal page's value
		// column (`gui/menu_item/submenu.cpp:187-194`): the column's own `selectButtonPress()`
		// override runs FIRST, as the horizontal forward's own call -- not a separate arm bolted on
		// after. No notification either way: neither `Toggle`'s/`Selection`'s `selectButtonPress`
		// nor the chain/ascend arms below call `displayNotification` (see `MenuState::
		// column_press`'s doc).
		if (const Node* column = selected_child(*node); column != nullptr && column->kind != Kind::Submenu) {
			if (column->kind == Kind::Toggle || column->selection_is_toggle) {
				// `Toggle::selectButtonPress` (`toggle.h:38-42`) and `Selection::selectButtonPress`
				// (`gui/menu_item/selection.h:54-63`, when `isToggle()`) have the identical shape: flip and
				// return `NO_NAVIGATION`, so `SoundEditor::buttonAction`'s SELECT arm does nothing
				// further (`sound_editor.cpp:394-419`) -- no chain, no ascend, the same "stays put"
				// shape the already-focused button press's own flip has. `Node::
				// selection_is_toggle`'s own doc names the three live examples -- this is not
				// `Toggle`-only.
				bindings::write(*column, ctx_, bindings::read(*column, ctx_) == 0 ? 1 : 0);
				repaint = true;
			}
			else if (column->custom != nullptr && column->custom->opens_directly_on_horizontal_select()) {
				// `FileBrowserItem` (`nodes/sample.h`): `FileSelector::selectButtonPress` opens the
				// browser outright with no permission or range check at all (`file_selector.cpp:
				// 49-52`) -- `enter_child`/`enter_range_picker_if_needed` must not run for this one,
				// see `CustomItem::opens_directly_on_horizontal_select`'s doc.
				walk_.descend_to(core(), visit_.session, *column);
			}
			else if (column->allow_session_from_horizontal) {
				// `Submenu::selectButtonPress` returns the item itself when it begins its own session from the
				// horizontal page (`submenu.cpp:191-193`), and the select arm enters it the general way.
				walk_.enter_child(core(), visit_.session, *column);
			}
			else if (const Node* chained =
			             column->select_chain != nullptr ? column->select_chain(ctx_, st_) : nullptr) {
				// `PatchedParam::selectButtonPress` (`patched_param.cpp:32-40`) records the param it patches. The
				// source page reads it from the level above it, which here is the page, not the column.
				if (column->bind.param.kind == deluge::modulation::params::Kind::PATCHED) {
					st_.walk.patching_param = column->bind.param.index;
				}
				walk_.enter_child(core(), visit_.session, *chained);
			}
			else {
				walk_.ascend(core(), visit_.session);
			}
		}
	}
	else if (st_.tick.back_claimed) {
		// Only ever set at depth > 0 (see `render_oled`); at depth 0 the back was never claimed
		// and `handle()` pops the scene instead.
		walk_.ascend(core(), visit_.session);
	}

	// The verb half of the `pending_scene` seam (`scenes/menu/action.h`): a `CustomItem` that asked
	// for a root-UI action. Drained here, beside its noun counterpart below, for the same reason --
	// an item's hook has no `Result` to put anything in.
	//
	// Two things happen, and both are legacy's:
	//
	//   * The request goes into the one-slot outbox `take_action()` empties. There is no `Action`
	//     kind for it and there must not be: the shell knows about scenes, and what these items run
	//     is not a scene (`openUI(&performanceView)`, `stemExport.startStemExportProcess(...)`).
	//     `gui::ui::dispatchShellActions` is the consumer.
	//   * The four `ActionItem`s ascend straight back out. Legacy never descends into them at all --
	//     `shouldEnterSubmenu()` is false, so `SoundEditor` calls `selectButtonPress()` and stays on
	//     the parent list (`gui/ui/sound_editor.cpp:420-423`). Ascending on the tick the request is
	//     filed is what makes the port's descend-then-leave look like legacy's never-descend.
	//     `PerformEditorItem` is excluded because legacy *does* stay on that page
	//     (`editing_mode.h:96`, `NO_NAVIGATION`); it files its request from `render`, not `on_focus`,
	//     which is exactly the discriminator -- see below.
	//
	// Placed **above** the repaint check so the ascend is covered by it.
	if (st_.walk.pending_action.engaged()) {
		trace_drain(DrainBand::PendingAction);
		taken_action_ = st_.walk.pending_action;
		// `on_focus` fires from `enter_leaf`, i.e. before this tick's render; a request filed from
		// `render` (`PerformEditorItem`) arrives with the item already drawn. The four that ascend
		// are the four that file from `on_focus`, which is the same set as "legacy never enters
		// this item" -- so the test is on the action's own identity rather than on a flag, and a
		// sixth action added without deciding this question fails the `switch`'s coverage warning.
		//
		// **The sixth, `SetShortcutsVersion`, does NOT ascend**, and the rule above is what decides
		// it rather than a preference. `SetShortcutsVersion` is filed from a `Binding::set` hook,
		// which runs from `commit_value` a few lines above -- not from `on_focus`, and not from any
		// press. It is also the other half of the same rule: legacy DOES enter this item. It is a
		// plain `menu_item::Selection` (`gui/menu_item/shortcuts/version.h:23`) and overrides
		// nothing, so `shouldEnterSubmenu()` is the base's `return true`
		// (`gui/menu_item/menu_item.h:271`) -- `SoundEditor` navigates into its page and
		// `writeCurrentValue` (`shortcuts/version.h:27`) runs on every detent while the user stands
		// on it. Ascending here would close that page under the user on the first turn, which is UI
		// the port would be inventing.
		//
		// **The seventh, `ScreensaverSettingsChanged`, has the sixth's shape exactly, so it takes the sixth's answer.**
		// `ScreensaverSettingsChanged` is filed from the same `Binding::set` route, by two hooks
		// (`nodes::set_screensaver_mode`, `nodes::set_screensaver_timeout`); and legacy enters both
		// of their items too. `ScreensaverModeMenu` is a `Selection` and `ScreensaverTimeout` an
		// `Integer` (`gui/menu_item/defaults/screensaver.h:34`, `:54`), neither overrides
		// `shouldEnterSubmenu()`, and both reach `writeCurrentValue` from
		// `Value<T>::selectEncoderAction` (`gui/menu_item/value.h:69`) -- once per detent, with the
		// user still standing on the page. Same discriminator, same conclusion: no ascend.
		switch (st_.walk.pending_action.action) {
		case MenuAction::StartStemExport:
		case MenuAction::SetClipLengthToSampleLength:
		case MenuAction::OpenSongMidiLearn:
		case MenuAction::OpenSongMacros:
			walk_.ascend(core(), visit_.session);
			break;
		case MenuAction::OpenPerformanceView:
		case MenuAction::SetShortcutsVersion:
		case MenuAction::ScreensaverSettingsChanged:
		// The settings writes below are the screensaver's shape: filed by a `Binding::set` hook on every detent of an
		// item legacy enters.
		case MenuAction::PadBrightnessChanged:
		case MenuAction::HighlightingChanged:
		case MenuAction::ShiftStickyOff:
		case MenuAction::FlashStatusLeftSlow:
		case MenuAction::PadColoursChanged:
		case MenuAction::None:
			break;
		}
		st_.walk.pending_action = MenuActionRequest{};
		r.handled = true;
	}

	// A descend or an ascend replaces the whole screen, but it happened *after* this tick's render
	// drew the old one -- and nothing else arms a gate for it, because no widget was involved. Ask
	// for the next frame explicitly, or the panel keeps showing the menu the user just left until
	// some unrelated input happens to repaint it.
	if (focused() != drawn) {
		repaint = true;
	}
	if (repaint) {
		trace_drain(DrainBand::Repaint);
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}

	// A `CustomItem` that asked, from `on_focus`, for another scene to be opened over the menu --
	// the port of `openUI(...)` from a legacy bespoke item's `beginSession` (`MenuWalkState::pending_scene`).
	// Drained here because this is the first hook after `enter_leaf` that has a `Result` to put the
	// `Push` in, and `go_up_on_resume` is set alongside it exactly where legacy sets
	// `shouldGoUpOneLevelOnBegin` -- beside its own `openUI` call.
	if (st_.walk.pending_scene.kind != SceneKind::Null) {
		trace_drain(DrainBand::PendingScene);
		visit_.shift_held = false; // the pushed scene is the one SHIFT now reaches

		(void)r.actions.push_back(Action{.kind = Action::Kind::Push, .id = st_.walk.pending_scene});
		st_.walk.pending_scene = SceneId{};
		st_.walk.go_up_on_resume = true;
		// `sampleBrowser.menuItemHeadingTo = item` (`gui/ui/sound_editor.cpp:2092`), and
		// `osc::AudioRecorder::menuItemHeadingTo` (`:2101`) for the recorder half. Legacy writes it
		// on the way in, from `maybeGetParentMenu`, which is exactly here: the statement beside its
		// own `openUI`. The four items with no return affordance record one too and nothing ever
		// reads it -- `reroot_to_scene_opener_page()` is called only from the sample claim -- which
		// is cheaper than teaching this arm which items have a pair.
		st_.walk.scene_opener = focused();
		r.handled = true;
	}

	// This tick's observations are spent, an uncommitted select-turn offset included: none may survive into a later
	// tick's accumulation.
	st_.tick = {};
	// Every tick, while `ctx_` is known good: a re-root or a song swap may leave the path naming nodes whose
	// relevance can no longer be read, so the rows are never recorded from those.
	walk_.remember_path(ctx_, st_);
	if (walk_.take_close_request()) {
		// This render's own navigation went up from depth 0: `exitCompletely()`, in place of the step it asked for.
		r.actions.clear();
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = SceneKind::FirstApp});
	}
	return r;
}

deluge::ui::InputClaims MenuScene::owned_input() const {
	return menu::owned_input(ctx_, st_);
}

bool MenuScene::owns(const UiEvent& event) const {
	return deluge::ui::shell::claim_covers(owned_input(), event);
}

Result MenuScene::handle(const UiEvent& event, const UiContext& ctx) {
	// The SHIFT overlay follows the shift level each event brings; a change takes or gives back the pads and the
	// greyout, and while it shows a source pad's press or release can change the source column.
	const bool overlay_was_up = overlay_up();
	visit_.shift_held = ctx.shift;
	Result r = handle_event(event, ctx);
	if (overlay_up() != overlay_was_up
	    || (overlay_up() && event.kind == UiEvent::Kind::Pad && is_patching_source_pad(event.x, event.y))) {
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}
	return r;
}

Result MenuScene::handle_event(const UiEvent& event, const UiContext& ctx) {
	// Legacy's `UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR` as a level rather than a mode
	// (`gui/menu_item/unpatched_param.cpp:52`). The AFFECT-ENTIRE button is an ordinary button, so
	// its own press and release both reach here and keep this in step before any write path runs;
	// `bindings::write` is the only reader.
	ctx_.affect_entire_held = ctx.held.has(ButtonId::AffectEntire);
	ctx_.addresses.affect_entire = bindings::writes_whole_kit(ctx_);

	// `SoundEditor::buttonAction`'s LEARN arm (`sound_editor.cpp:718-745`), observed only: the event goes on as before.
	if (visit_.learn.observe(ctx_, st_, event, ctx)) {
		visit_.blink_restart = true; // the release's `blinkLed(LEARN)` (`sound_editor.cpp:736-738`)
	}

	// A custom item owns its input while it is focused: it sees every event first and, if it
	// consumes it, the scene does nothing else this call -- the same precedence legacy gives a
	// `MenuItem`'s own `buttonAction` override over the rest of `SoundEditor::buttonAction`
	// (`gui/ui/sound_editor.cpp:636`: `currentMenuItem->buttonAction(...)` runs before anything else
	// in that branch looks at the result). Checked before the shortcut-pad jump and the root `Back`
	// below, not after: a bespoke item's own input (the patch-cable pages' pad presses, a file browser's
	// scroll) is exactly the kind of thing this layer cannot interpret generically, so it must not
	// be shadowed by a case that happens to match the same event shape.
	if (const Node* focused_node = focused();
	    focused_node != nullptr && focused_node->kind == Kind::Custom && focused_node->custom != nullptr) {
		if (Result r = focused_node->custom->handle(event, ctx, ctx_, st_); r.handled) {
			return r;
		}
	}

	// The six page buttons on a horizontal page, ahead of the shortcut-pad jump, as legacy's `buttonAction` override
	// is reached before `SoundEditor::buttonAction`'s own switch.
	if (std::optional<Result> r = handle_page_button(core(), walk_, visit_.session, event, ctx)) {
		return *r;
	}

	if (std::optional<Result> r = pads_.handle_pad(core(), walk_, visit_.session, event, ctx)) {
		return *r;
	}

	// Back at the root. `owns` leaves it to this arm at depth 0, which closes the editor --
	// `sound_editor.cpp:456`'s BACK case -> `:471` `goUpOneLevel()` -> `:716` `exitCompletely()`.
	//
	// `Remove{.pop_to = SceneKind::FirstApp}`, NOT `Pop` with any `pop_to`: `SceneStack::pop(kind)`
	// pops the topmost scene of that kind *and everything above it* (`scene_stack.cpp:63-95`), so
	// `Pop{.pop_to = FirstApp}` would tear
	// down a live `Working` animation or `Popup` sitting over the menu along with it. `Remove`
	// (`SceneStack::remove`) excises just the named scene and leaves everything else exactly where
	// it was, which is what "close the editor" should mean regardless of which overlay happens to be
	// up. And it has to name a kind at all rather than defaulting to `Pop{.pop_to = Null}` ("just the
	// top"): a `Notices`-driven overlay (`Console` in particular, which -- unlike `Popup`/`Working`
	// -- is never removed again once shown) can legitimately be pushed above this scene between the
	// render that produced this `Result` and the router applying it (`menu_submenu_horizontal`'s
	// `Console` notice): with `Null`, pressing Back while that notice was
	// still showing closed the console strip and left the menu itself on the stack.
	if (event.kind == UiEvent::Kind::Button && event.button == ButtonId::Back && event.on && st_.depth == 0) {
		// `SoundEditor::goUpOneLevel` runs `endScreen()` (-> `currentMenuItem->endSession()`)
		// **before** its depth-0 arm's `exitCompletely()` (`sound_editor.cpp:711-716`) -- the same
		// cancel `descend_to`/`ascend` make, just reached by the one path neither of them covers: a
		// depth-0 horizontal page closing the whole menu rather than navigating within it.
		// `reroot_to_scene_opener_page()` puts a claimed file selector's browser back here at
		// depth 0, so this is a shipped path, not a hypothetical one.
		if (on_horizontal_page()) {
			deluge::notify::notice_kind(deluge::notify::Notice::Kind::CancelNotification);
		}
		Result r;
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = SceneKind::FirstApp});
		return r;
	}

	return Result::unhandled();
}

// ---------------------------------------------------------------------------------------------
// Composition, saved state and timers

deluge::ui::grid::GreyoutMask MenuScene::greyout_mask() const {
	// While the overlay shows every column is greyed, whatever the root (`getGreyoutColsAndRows`,
	// `sound_editor.cpp:463-467`).
	if (overlay_up()) {
		return {0xFFFFFFFFu, 0};
	}
	return menu::greyout_mask(ctx_, st_);
}

void MenuScene::save_state(SceneState& state) const {
	walk_.save(ctx_, st_, state);
}

void MenuScene::restore_state(const SceneState& state) {
	walk_.restore(core(), visit_.session, state);
	voice_usage_.leave();
}

std::optional<uint32_t> MenuScene::next_timer() const {
	// The voice usage page's next sample, else the pad blinks: the shorter of the counter's two steps that the plan
	// uses. Nothing blinking arms nothing.
	if (const std::optional<uint32_t> sample = voice_usage_.next_timer(st_)) {
		return sample;
	}
	return pad_next_timer(ctx_, st_);
}

deluge::ui::shell::Scene::RepaintSurfaces MenuScene::timer_repaints() const {
	return {.oled = VoiceUsageClock::place_of(focused()) != VoiceUsageClock::Place::None, .grid = true};
}

void MenuScene::tick(uint32_t dt_ms) {
	if (voice_usage_.tick(ctx_, st_, dt_ms)) {
		read_value_again();
	}
}

// ---------------------------------------------------------------------------------------------
// Navigation for callers outside the scene.

bool MenuScene::go_up_one_level() {
	if (st_.depth == 0) {
		return false; // `sound_editor.cpp:715-717`, on the loop's first pass
	}
	walk_.ascend(core(), visit_.session);
	// `:715-717` on a later pass: `ascend()` stepped down to depth 0 and `:720-722` refused that level too, so it
	// asked for the close. The caller closes the editor itself, so the request is taken back here.
	return !walk_.take_close_request();
}

void MenuScene::enter_submenu(const Node& node) {
	walk_.descend_to(core(), visit_.session, node); // `sound_editor.cpp:364-368`
}

bool is_automation_item(const Node& item) {
	if (item.custom != nullptr) {
		return item.custom->is_automation_item();
	}
	return item.bind.param.valid();
}

std::optional<deluge::project::ParamAddress> automation_address(const Node& item, const MenuContext& ctx,
                                                                const MenuState& state) {
	if (item.custom != nullptr) {
		return item.custom->automation_address(ctx, state);
	}
	if (!item.bind.param.valid()) {
		return std::nullopt;
	}
	const deluge::scenes::bind::Target param{.kind = deluge::scenes::bind::Target::Kind::PARAM,
	                                         .param = param_id_of(item.bind.param),
	                                         .param_table = param_table_of(item.bind.param)};
	return deluge::scenes::bind::param_address(param, ctx.addresses);
}

} // namespace deluge::scenes::menu
