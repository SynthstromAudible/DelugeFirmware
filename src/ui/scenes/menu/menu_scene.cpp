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
/// its inputs in the same call (`ui/widgets/core/frame_context.h`, rule 2: *reading is claiming*), so
/// `render_oled` is where input is observed -- but it may not touch the model. `after_render()`
/// is therefore where a `Response` becomes navigation or a `bindings::write`, and `handle()` is
/// left with only the two things a widget can never claim: a pad press (positional, never masked
/// -- rule 4) and a button nothing owned.

#include "ui/scenes/menu/menu_scene.h"

#include "ui/scenes/menu/nodes/nodes.h" // `roots()`' table

#include "ui/scenes/bind/dispatch.h"
#include "ui/scenes/bind/knob_focus.h"
#include "ui/scenes/bind/ports/write_sink.h"
#include "ui/scenes/browser/browser_scene.h" // on_resume: the browser's load blink
#include "ui/scenes/menu/node_id.h"
#include "ui/scenes/menu/nodes/dx_tables.h"
#include "ui/scenes/recorder/recorder_scene.h" // on_resume: a refused recorder push

#include "base/notify/notice_queue.h"
#include "ui/shell/input_lowering.h" // claim_covers -- MenuScene::owns

#include "base/notify/dirty_set.h"
#include "gui/l10n/strings.h"
#include "plugin/types/param_mapping.h"
#include "ui/scenes/menu/bindings.h"
#include "ui/scenes/menu/chains.h"
#include "ui/scenes/menu/menu_core.h"

#include <span>
#include <utility>

namespace deluge::ui::scenes::menu {

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

namespace {

/// `kShortPressTime`, the half second a patch cable page's cursor stands at an edge before a horizontal turn passes on.
constexpr uint32_t kCableEdgeDelayMs = 500;

/// 10 to the power @p exponent, as `decimal_value` steps its cursor by.
int64_t pow10_of(int32_t exponent) {
	int64_t v = 1;
	for (int32_t i = 0; i < exponent; i++) {
		v *= 10;
	}
	return v;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Lifecycle

void MenuScene::open(const Node& root, const MenuContext& ctx) {
	install_context(ctx);
	refused_recorder_landing_ = nullptr;
	pushed_kind_ = SceneKind::Null;
	pushed_scene_refused_ = false;
	import_range_ = -1;
	const DxState dx = st_.dx;
	st_ = MenuState{};
	st_.dx = dx;
	pending_transition_.reset();
	transition_.reset();
	cable_edge_until_ms_ = 0;
	visit_ = {}; // `blinkLed(IndicatorLED::BACK)` as the editor opens (`SoundEditor::setLedStates`), among the rest
	holding_affect_entire_ = false;
	pads_.forget_landing();
	retarget(root);
}

SceneKind MenuScene::kind() const {
	return kMenuSceneKind;
}

std::span<const Node* const> MenuScene::roots() {
	// One slot per `MenuContext::Root`, and every one of them filled. This array is `on_enter`'s "arg 0 means the root
	// the context chose" lookup, and adding or re-pointing a root is a change to this array alone. One node per
	// `MenuContext::Root`, in enumerator order. `Root::MidiOrCv` holds the MIDI-titled spelling of that root -- the
	// title `origin/main:src/deluge/gui/ui/menus.cpp`'s constructor gives it; a caller on a CV output takes
	// `nodes::midi_or_cv_root(ctx)` instead (`nodes/roots.h`'s note). `NoteEditor`/`NoteRowEditor` are the last two: a
	// menu opened with `ctx.root` set to either (`openMenu`'s `NoteMode`) re-roots here through `on_enter`'s
	// `arg == 0` path (`MenuScene::open`'s warning), exactly as every other root does.
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
	refused_recorder_landing_ = nullptr;
	host_gates_.reset();
	walk_.forget_rows();
	st_.dx = DxState{};
	pending_retarget_ = 0;
	land_dx_cartridge_ = false;
	view_.forget_acceleration();
	voice_usage_ = VoiceUsageClock{};
}

bool MenuScene::on_enter(const SceneId& id) {
	// A definition browser that opened over an earlier visit is no blink of this one.
	(void)browser::browser_scene().take_load_blink();
	// `SceneId::arg` is deliberately NOT part of the `SceneState` cache key (see `SceneId`),
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
		// `SoundEditor::setup(clip, item, sourceIndex)` runs `item->checkPermissionToBeginSession(...)` on the item it
		// was asked for (`SoundEditor::setup`'s item arm), and for every range-specific item that lands in
		// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), whose **first statement** is
		// `source->getOrCreateFirstRange()` -- a *create*, not a read, which fails only with `INSUFFICIENT_RAM`.
		//
		// `enter_child` (below) already runs it for a descent, which covers everything
		// reached by walking the menu. A deep link does not descend: it re-roots. So the `file0SelectorMenu` entry of
		// `gui/ui/menu_entry.cpp`'s table -- `soundEditor.setup(getCurrentInstrumentClip(),
		// &menu_item::file0SelectorMenu, 0)`, `InstrumentClipView::buttonAction`'s auditioning LOAD/KIT arm -- and the
		// shortcut-pad jump, which comes back through here as an `Action::Kind::Replace`
		// (`handle`'s `Replace` arm), both arrived with no range selected on a source with no
		// ranges yet. **Measured** at level 3 on a fresh synth clip: legacy opened the
		// sample browser and the menu did not, because `browser::sample_can` refuses a
		// null `AudioFileHolder` (`sample_can` in `ui/scenes/browser/browser_scene.cpp`) and the push was
		// declined, leaving the menu parked on the file selector's own title page.
		//
		// **The range gate in full, but NOT `permission_for` around it.** Legacy's gate here is the
		// *item's own* `checkPermissionToBeginSession` override, and the five scene-opener items
		// override it in a way that never consults `isRelevant`:
		// `FileSelector::checkPermissionToBeginSession` (`origin/main:src/deluge/gui/menu_item/file_selector.cpp`)
		// answers YES for any SUBTRACTIVE sound and defers the rest to the range check, while
		// `FileSelector::isRelevant` additionally demands `oscType == SAMPLE`.
		// `permission_for` is the port of the *base* `MenuItem::checkPermissionToBeginSession`
		// (`origin/main:src/deluge/gui/menu_item/menu_item.cpp`, relevance and nothing else) plus the range rule, so
		// calling it here would refuse that entry on a default SAW synth that legacy opens. That gap --
		// `Node` has no port of an item-specific `checkPermissionToBeginSession` that bypasses
		// relevance -- is listed in `src/ui/scenes/README.md`'s "Features `main` has that the scenes do not" rather
		// than half-closed here.
		//
		// What is NOT skipped is the range half, both of its arms. Paying only the allocation and
		// dropping `MUST_SELECT_RANGE` would be a partial port of a two-armed gate, which is worse
		// than paying neither: the half that is there makes it look complete. It is latent on a
		// fresh source (0 ranges becomes 1, so the item just opens) and fires the moment a user
		// presses LOAD on a multisampled oscillator.
		if (target != nullptr) {
			// About the item's own source (`range_rule_context`): a recorder for oscillator 2 under oscillator 1's
			// context makes oscillator 2's range, and its picker test reads oscillator 2.
			const Permission range = ensure_item_first_range(*target, ctx_);
			if (range == Permission::No) {
				deluge::base::notify::notice_string(
				    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_ERROR_INSUFFICIENT_RAM));
				return false;
			}
			// The two arms after that allocation in `main`'s
			// `SoundEditor::checkPermissionToBeginSessionForRangeSpecificParam`
			// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`). The allocation has already applied the first -- a kit
			// row or a single range is unambiguous, so it points the range index at range 0. What is left is
			// `MUST_SELECT_RANGE`: a range-specific item on a source with more than one range and none remembered.
			// Spelled as "the allocation did not resolve one" rather than by calling `permission_for`, whose first
			// statement is the relevance fold above.
			if (range == Permission::MustSelectRange) {
				// `SoundEditor::setup`: `multiRangeMenu.menuItemHeadingTo =
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
	// A shortcut's page: `beginScreen` begins it, which keeps a range only for a range-dependent column
	// (`HorizontalMenu::beginSession`).
	if (page != nullptr) {
		visit_ = {};
		retarget(*page);
		walk_.focus_column(core(), *target);
		begin_submenu_range(ctx_, st_, nullptr);
		return true;
	}
	// `beginScreen`: a new screen, with `blinkLed(LEARN)` as `SoundEditor::updatePadLightsFor` runs. Reset only on a
	// screen that opens: a refused id leaves the menu the visit it was, as the shell rolls the move back.
	visit_ = {};
	retarget(*target);
	return true;
}

uint16_t MenuScene::arm_shortcut_landing(const Node& target) {
	return pads_.arm_landing(ctx_, target);
}

uint16_t MenuScene::arm_page_landing(const Node& page, const Node& column) {
	return pads_.arm_landing_on(page, column);
}

void MenuScene::on_resume() {
	// Input went elsewhere, and so did LEARN's release: the menu is not learning when it comes back.
	visit_.learn.cancel();
	// A folder import's range is this resume's to apply (on the step up below) or to drop.
	const int16_t imported = std::exchange(import_range_, int16_t{-1});
	visit_.blink_restart = true; // `focusRegained`'s `setLedStates` -> `blinkLed(IndicatorLED::BACK)`
	// `SoundEditor::focusRegained` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`): "If just came back from a
	// deeper nested UI", step up one level. The flag is set where the push was requested (see
	// `after_render`), and it is what stops the item that opened the pushed scene from opening it
	// again the instant the user gets back.
	// A landing armed by a jump whose `Replace` never reached `on_enter` is stale.
	// The scene over the menu jumped (`shortcut_jump`): it is removed first, so this resume comes before the
	// `Replace` its landing is for.
	if (!std::exchange(landing_survives_resume_, false)) {
		pads_.forget_landing();
	}
	visit_.use_up_select = true;
	visit_.leds_held_back = false; // `focusRegained`'s `setLedStates`
	visit_.load_blink = browser::browser_scene().take_load_blink() || visit_.load_blink;
	if (std::exchange(land_dx_cartridge_, false) && focused() == &nodes::dx_browse) {
		// `DxSyxBrowser`'s `enterSubmenu(&dxCartridge)`, with `shouldGoUpOneLevelOnBegin` cleared: no going up.
		st_.walk.go_up_on_resume = false;
		walk_.descend_to(core(), visit_.session, nodes::dx_cartridge);
	}
	// A recorder opened on the SAMPLE RECORDER page returns to its source's page, the file selector focused, in place
	// of the step up (`MenuWalkState::resume_landing`); the re-root's `retarget` spends the step up with the walk.
	if (const Node* landing = std::exchange(st_.walk.resume_landing, nullptr); landing != nullptr) {
		(void)reroot_to_page_listing(*landing);
	}
	// The resume a push brings, refused or not (`SceneStack::push` resumes the scene beneath a refused enter): which
	// scene it was, and whether it refused.
	if (const SceneKind pushed = std::exchange(pushed_kind_, SceneKind::Null); pushed != SceneKind::Null) {
		const bool refused = (pushed == recorder::kRecorderSceneKind && recorder::recorder_scene().last_enter_refused())
		                     || (pushed == browser::kBrowserSceneKind && browser::browser_scene().last_enter_refused());
		pushed_scene_refused_ = pushed_scene_refused_ || refused;
	}
	if (st_.walk.go_up_on_resume) {
		st_.walk.go_up_on_resume = false;
		const Node* const landing = std::exchange(refused_recorder_landing_, nullptr);
		if (landing != nullptr && recorder::recorder_scene().last_enter_refused()) {
			// `enterDrumCreator`'s fallback after a refused `openUI(&audioRecorder)`: the editor on the file selector,
			// whose session opens the sample browser. Retargeting onto the leaf runs its hook, which asks for the push.
			retarget(*landing);
		}
		else {
			// A folder import left its range current, as `SoundEditor::setCurrentMultiRange` does. The level the step
			// lands on keeps it or drops it as main's does: the picker opens on it, and a submenu that begins drops it
			// (`begin_submenu_range`).
			if (imported >= 0) {
				select_range(ctx_, imported);
			}
			walk_.ascend(core(), visit_.session);
		}
	}
	// Otherwise the path is kept exactly as it was. Reset only the per-tick observations, which
	// belong to a render pass that never happened.
	st_.tick = {};
}

std::optional<SceneId> MenuScene::shortcut_jump(uint8_t x, uint8_t y) {
	// The kit's own effects while the host's latch is set, as for a press on the menu itself.
	const std::optional<MenuContext> kit_global = kit_global_shortcut_context();
	const MenuContext& press = kit_global.has_value() ? *kit_global : ctx_;
	// A `comingSoonMenu` cell pops its message and goes nowhere (`potentialShortcutPadAction`).
	if (shortcut_coming_soon(press.root, x, y)) {
		deluge::base::notify::notice_string(static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_UNIMPLEMENTED));
		return std::nullopt;
	}
	// Over the automation view any item is taken: `potentialShortcutPadAction` refuses a non-param one only while
	// the editor itself is the current UI, and here the scene over the menu is.
	const Node* target = shortcut_node(press.root, shortcuts_version(press), false, x, y);
	if (target == nullptr) {
		return std::nullopt;
	}
	if (!shortcut_applies(*target, press)) {
		target = nodes::dx_shortcut_fallback(*target, press, x);
		if (target == nullptr) {
			deluge::base::notify::notice_string(
			    static_cast<uint16_t>(deluge::l10n::String::STRING_FOR_PARAMETER_NOT_APPLICABLE));
			return std::nullopt;
		}
	}
	if (kit_global.has_value()) {
		install_context(*kit_global);
	}
	const uint16_t id = arm_shortcut_landing(*target);
	if (id == 0) {
		return std::nullopt;
	}
	landing_survives_resume_ = true;
	// `enterOrUpdateSoundEditor` from the UI over the editor: its write to `setupKitGlobalFXMenu` is the host's.
	if (reentry_hook_ != nullptr) {
		reentry_hook_();
	}
	return SceneId{SceneKind::FirstApp, id};
}

std::optional<MenuContext> MenuScene::kit_global_shortcut_context() const {
	if (!ctx_.is_kit) {
		return std::nullopt;
	}
	switch (ctx_.root) {
	case MenuContext::Root::Drum:
	case MenuContext::Root::MidiDrum:
	case MenuContext::Root::GateDrum:
		// A row's root opened on the kit's own effects already edits them; its shortcuts are the kit's own.
		if (ctx_.is_kit_global_fx) {
			MenuContext kit_global = ctx_;
			kit_global.root = MenuContext::Root::KitGlobalFx;
			return kit_global;
		}
		if (kit_global_shortcut_source_ == nullptr) {
			return std::nullopt;
		}
		return kit_global_shortcut_source_(ctx_);
	default:
		return std::nullopt;
	}
}

MenuContext::Root MenuScene::shortcut_root() const {
	return kit_global_shortcut_context().has_value() ? MenuContext::Root::KitGlobalFx : ctx_.root;
}

void MenuScene::install_context(const MenuContext& ctx) {
	ctx_ = ctx;
	if (context_adopted_hook_ != nullptr) {
		context_adopted_hook_();
	}
}

void MenuScene::dx_column_press(int32_t op) {
	const bool on_page = focused() == &nodes::dx_param;
	st_.dx.param = dx::column_param(st_.dx.param, op, on_page);
	if (on_page) {
		st_.dx.pad_blink = true;
		st_.dx.flash_restart = true;
	}
	else {
		st_.dx.blink_on_focus = true;
		pending_retarget_ = node_id(nodes::dx_param);
	}
	deluge::base::notify::raise(deluge::base::notify::Dirty::Oled); // the host's press, between the guest's ticks
}

bool MenuScene::takes_affect_entire() const {
	if (!ctx_.is_kit || ctx_.is_kit_global_fx || ctx_.root == MenuContext::Root::NoteEditor
	    || ctx_.root == MenuContext::Root::NoteRowEditor) {
		return false;
	}
	return menu::uses_affect_entire(ctx_, st_);
}

void MenuScene::set_affect_entire_hold(bool on) {
	holding_affect_entire_ = on;
	affect_entire_blink_restart_ = on;
	ctx_.affect_entire_held = on;
	ctx_.addresses.affect_entire = bindings::writes_whole_kit(ctx_);
	deluge::base::notify::raise(deluge::base::notify::Dirty::Oled);
}

void MenuScene::reroot(const Node& root, const MenuContext& ctx, const Node& column) {
	open(root, ctx);
	pending_retarget_ = arm_page_landing(root, column);
	deluge::base::notify::raise(deluge::base::notify::Dirty::Oled); // the host's press, between the guest's ticks
}

void MenuScene::reroot_at(const Node& root, const MenuContext& ctx, uint16_t arg, const Node* shortcut) {
	open(root, ctx);
	pending_retarget_ = shortcut != nullptr ? arm_shortcut_landing(*shortcut) : arg;
	deluge::base::notify::raise(deluge::base::notify::Dirty::Oled); // the host's press, between the guest's ticks
}

std::optional<int32_t> MenuScene::dx_column_flash(uint32_t now_ms) const {
	if (!dx_column_flashing()) {
		return std::nullopt;
	}
	const uint32_t phase = (now_ms - *st_.dx.flash_anchor_ms) % dx::kColumnFlashPeriodMs;
	return phase < dx::kColumnFlashOnMs ? std::optional{dx::flash_row(st_.dx.param)} : std::nullopt;
}

bool MenuScene::dx_column_flashing() const {
	// `DxParam::hasSideColumn` (`origin/main:src/deluge/gui/menu_item/dx/param.cpp`): the parameter page over the
	// keyboard screen.
	return focused() == &nodes::dx_param && ctx_.root_is_keyboard && st_.dx.flash_anchor_ms.has_value();
}

void MenuScene::delete_range_press() {
	if (focused() != &nodes::multi_range) {
		return;
	}
	nodes::range_delete_press(ctx_, st_);
	deluge::base::notify::raise(deluge::base::notify::Dirty::Oled); // the host's press, between the guest's ticks
}

bool MenuScene::select_range_for_note(int32_t note) {
	if (focused() != &nodes::multi_range) {
		return false;
	}
	if (!nodes::range_note_press(ctx_, st_, note)) {
		return false;
	}
	deluge::base::notify::raise(deluge::base::notify::Dirty::Oled); // a note the root played, between the guest's ticks
	return true;
}

bool MenuScene::reroot_to_scene_opener_page() {
	// Read before `retarget` runs, which drops it along with the rest of the abandoned walk. Legacy reads its pair only
	// when both halves are set (`SampleClaim::stepFinish`, `gui/ui/browser/sample_claim.cpp`).
	const Node* item = st_.walk.scene_opener;
	return item != nullptr && reroot_to_page_listing(*item);
}

bool MenuScene::reroot_to_page_listing(const Node& item) {
	if (st_.path[0] == nullptr) {
		// No menu at all (`on_model_changed`'s song swap empties the path).
		return false;
	}
	// With the feature off legacy's chain is `nullopt`, so `maybeGetParentMenu` writes no pair and the menu stays where
	// the scene was opened from.
	if (!ctx_.horizontal_menus) {
		return false;
	}
	const std::span<const Node* const> r = roots();
	const size_t i = static_cast<size_t>(ctx_.root);
	if (i >= r.size() || r[i] == nullptr) {
		return false;
	}
	// `maybeGetParentMenu`'s search: the first menu of the root's chain that lists the item, which for an oscillator's
	// file selector is the whole `source_group`, paged across its five members. A root whose chain lists the item
	// nowhere falls back to the page that does (the audio clip's sample page), except under the kit-global latch: its
	// kit chain never lists a drum's file selector, and legacy then sets no pair, so the claim goes up one level.
	// Legacy answers `nullptr` when no menu lists the item, and its caller then does nothing -- which is this `false`.
	const Node* page = nullptr;
	for (const Node* candidate : chain_for(shortcut_root())) {
		if (candidate != nullptr && page_lists(*candidate, item)) {
			page = candidate;
			break;
		}
	}
	if (page == nullptr && !kit_global_shortcut_context().has_value()) {
		page = horizontal_page_listing(*r[i], item);
	}
	if (page == nullptr) {
		return false;
	}
	// `menuItemNavigationRecord[0] = parentMenuHeadingTo; navigationDepth = 0`: `retarget` is both at once, and drops
	// exactly the per-visit state legacy's two statements throw away.
	retarget(*page);
	// `parentMenuHeadingTo->focusChild(menuItemHeadingTo)`, after `retarget`, which resets `columns[0]`. An item the
	// page no longer shows leaves the page on its first column, as legacy's discarded `focusChild` does.
	walk_.focus_column(core(), item);
	// Callers run between the guest's ticks: ask for the frame that shows the page.
	deluge::base::notify::raise(deluge::base::notify::Dirty::Oled);
	return true;
}

void MenuScene::on_model_changed(const deluge::base::notify::DirtySet& dirty) {
	// A song swap invalidates every pointer in `ctx_`. The application re-resolves the context and
	// calls `open()` again; until it does, refuse to render anything by dropping the path, which
	// is strictly safer than drawing a menu over a freed `Sound`. The emptied context goes out through the context hook
	// too (`install_context`), so the host republishes the target, now empty.
	if (dirty.has(deluge::base::notify::Dirty::SongSwapped)) {
		const DxState dx = st_.dx;
		st_ = MenuState{};
		st_.dx = dx;
		install_context(MenuContext{});
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

void MenuScene::unlearn() {
	visit_.learn.unlearn(ctx_, st_);
	read_learned_value_again();
}

void MenuScene::read_learned_value_again() {
	const Node* at = focused();
	if (at != nullptr && at->kind != Kind::Submenu && at->kind != Kind::Custom
	    && (at->bind.get != nullptr || at->bind.target.valid()) && (at->learn != nullptr || at->unlearn != nullptr)) {
		read_value_again();
	}
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

void MenuScene::set_root_facts(MenuContext::RootGreyout greyout, bool root_is_keyboard, bool root_is_automation) {
	ctx_.root_greyout = greyout;
	ctx_.root_is_keyboard = root_is_keyboard;
	ctx_.root_is_automation = root_is_automation;
}

void MenuScene::note_transition(const UiEvent& event, const Result& r, const Node* value_before) {
	// One record per tick: the first event's value is the one it moved from (`note_cause`).
	// SELECT and BACK are the widgets' own edges, consumed by the render (`after_render`), not routed here.
	std::optional<TransitionCause> cause;
	if (event.kind == UiEvent::Kind::Button) {
		switch (event.button) {
		case ButtonId::Synth:
		case ButtonId::Kit:
		case ButtonId::Midi:
		case ButtonId::Cv:
		case ButtonId::CrossScreen:
		case ButtonId::Scale:
			// `SoundEditor::buttonAction`'s horizontal-menu arm compares the item before and after the press.
			if (event.on) {
				cause = TransitionCause::ColumnButton;
			}
			break;
		default:
			break;
		}
	}
	else if (event.kind == UiEvent::Kind::Pad) {
		// A pad that jumps (a shortcut, or a patching source opening its page) answers with a Replace.
		for (const Action& action : r.actions) {
			if (action.kind == Action::Kind::Replace) {
				cause = TransitionCause::Shortcut;
				break;
			}
		}
	}
	if (cause) {
		note_cause(*cause, value_before);
	}
}

void MenuScene::note_cause(TransitionCause cause, const Node* value_before) {
	if (!pending_transition_) {
		pending_transition_ = cause;
		transition_from_ = value_before;
	}
}

MenuScene::HorizontalTurn MenuScene::route_horizontal_turn(int32_t offset, bool xenc_held, uint32_t now_ms) {
	const Node* node = focused();
	if (node == nullptr || offset == 0) {
		return HorizontalTurn::Menu;
	}
	if (node->custom != nullptr && node->custom->is_patch_cable()) {
		// `Decimal::horizontalEncoderAction`'s move, which `decimal_value` makes for the page: right while there is a
		// place below, left while the next place up is in range.
		const int8_t pos = st_.decimal_pos;
		const bool moves = !xenc_held
		                   && ((offset > 0 && pos > 0)
		                       || (offset < 0 && static_cast<int64_t>(pow10_of(pos)) * 10 <= plugin::kCableMenuMax));
		if (moves) {
			cable_edge_until_ms_ = 0;
			return HorizontalTurn::Menu;
		}
		if (cable_edge_until_ms_ == 0) {
			cable_edge_until_ms_ = now_ms + kCableEdgeDelayMs;
		}
		else if (static_cast<int32_t>(now_ms - cable_edge_until_ms_) > 0) {
			return HorizontalTurn::Root;
		}
		return xenc_held ? HorizontalTurn::Neither : HorizontalTurn::Menu;
	}
	// `Param::horizontalEncoderAction` hands a param's number screen's turn to the root. A `Decimal` keeps it whatever
	// it learns: `Transpose` is a `Decimal` first, and its learn-only param leaves the cursor to
	// `Decimal::horizontalEncoderAction`.
	if (node->kind != Kind::Submenu && node->kind != Kind::Decimal && node->custom == nullptr
	    && node->bind.param.valid()) {
		return HorizontalTurn::Root;
	}
	return HorizontalTurn::Menu;
}

std::optional<MenuTransition> MenuScene::take_transition() {
	return std::exchange(transition_, std::nullopt);
}

std::optional<AutomationParam> MenuScene::focused_automation_param() const {
	const Node* value = focused_value();
	return value != nullptr ? automation_param(*value, ctx_, st_) : std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// Rendering. Draws AND consumes input (the toolkit's rule); never touches the model.

void MenuScene::render_oled(SurfaceRole role, deluge::ui::widgets::oled::OledFrame& f) {
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
	// `beginScreen`'s `blinkLed(LEARN)` for an item that wants it (`SoundEditor::updatePadLightsFor` in
	// `origin/main:src/deluge/gui/ui/sound_editor.cpp`) restarts the shared phase, BACK's too, at this tick's `leds()`;
	// the open's and LEARN's release are filed the same way.
	if (node != visit_.blink_screen) {
		visit_.blink_screen = node;
		visit_.blink_restart = visit_.blink_restart || wants_learn_blink();
	}

	st_.tick.back_acts = select_and_back_act();
	// The turn the page owns arrives in this frame. Read only where `owned_input` grants it, so the read claims
	// nothing the page does not.
	if (owned_input().turn && f.turn() != 0) {
		st_.tick.select_turned = true;
	}
	view_.render(core(), f, *node);
}

void MenuScene::render_grid(deluge::ui::widgets::grid::GridFrame& f) {
	render_ms_ = f.now_ms();
	// The shift level, latched or not, kept in step here as well as from `handle`'s events. A covered frame reads no
	// input, and a menu under another scene shows no overlay.
	if (!f.covered) {
		visit_.shift_held = f.shift();
		if (overlay_up()) {
			const std::optional<MenuContext> kit_global = kit_global_shortcut_context();
			render_overlay(f, kit_global.has_value() ? *kit_global : ctx_, st_, focused_value());
		}
	}
	// The blinks flash over the overlay: the control surface plays them over whatever the pads hold.
	pads_.render_grid(core(), f, kit_global_shortcut_context().has_value());
}

void MenuScene::leds(deluge::ui::widgets::core::LedFrame& f) {
	// Button LEDs only: `LedFrame` has no pad concept whatever, so the pad blink above is a pad
	// flash rather than an LED declaration. The set is `SoundEditor::setLedStates`
	// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), minus the parts that belong to services (`playbackHandler`,
	// `view.setKnobIndicatorLevels`).
	// Nothing to declare while the path is empty: `on_model_changed(SongSwapped)` drops it and the
	// scene draws nothing until the application re-`open()`s, so announcing the menu's LED set
	// would light a menu that is not on screen.
	if (focused() == nullptr || visit_.leds_held_back) {
		return;
	}
	constexpr deluge::ui::widgets::core::LedState kOff{deluge::ui::widgets::core::LedState::Kind::Off, 0, 0};
	// `Save` is never a horizontal-page LED in legacy either -- unaffected by anything below.
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Save), kOff);

	page_button_leds(ctx_, st_, f);

	// A focused bespoke item's own LEDs over the page buttons' (the cable-strength page's MIDI/CV polarity pair).
	if (const Node* screen = focused(); screen->kind == Kind::Custom && screen->custom != nullptr) {
		screen->custom->leds(f, ctx_, st_);
	}

	// `blinkLed`'s default blink, on the phase every normal blink shares (`indicator_leds.cpp`).
	if (visit_.blink_restart) {
		visit_.blink_restart = false;
		f.restart(deluge::ui::widgets::core::BlinkRate::Normal);
	}
	const deluge::ui::widgets::core::LedState shared_blink =
	    deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Normal);
	(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Back), shared_blink);
	if (visit_.load_blink) {
		(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::Load), shared_blink);
	}
	visit_.learn.leds(st_, f, shared_blink);

	// `blinkLed(IndicatorLED::AFFECT_ENTIRE, 255, 1)` while the hold is on an item that uses it; off it, legacy's
	// `view.setModLedStates()` shows the clip's own state, which an undeclared LED hands back.
	if (holding_affect_entire_ && takes_affect_entire()) {
		if (std::exchange(affect_entire_blink_restart_, false)) {
			f.restart(deluge::ui::widgets::core::BlinkRate::Fast);
		}
		(void)f.set(static_cast<deluge::ui::widgets::core::LedId>(ButtonId::AffectEntire),
		            deluge::ui::widgets::core::LedState::shared(deluge::ui::widgets::core::BlinkRate::Fast));
	}
}

// ---------------------------------------------------------------------------------------------
// Navigation and commits: the one place the render pass's observations touch the model.

MenuActionRequest MenuScene::take_action() {
	const MenuActionRequest taken = taken_action_;
	taken_action_ = MenuActionRequest{};
	return taken;
}

Result MenuScene::after_render() {
	const bool entering = !visit_.entered;
	visit_.entered = true;
	// The render consumed this tick's BACK (above depth 0) and select edges -- the select one on its release, as
	// legacy's SELECT arm acts (`select_acts_on_release`) -- and the navigation below acts on them: note the value they
	// leave now.
	if (st_.tick.back_claimed) {
		note_cause(TransitionCause::Back, focused_value());
	}
	else if (st_.tick.render_resp.entered || st_.tick.column_press) {
		note_cause(TransitionCause::Select, focused_value());
	}
	Result r = after_render_navigation();
	if (entering) {
		for (const Action& a : r.actions) {
			visit_.leds_held_back = visit_.leds_held_back || a.kind == Action::Kind::Push;
		}
	}
	// The record an event noted (`handle`) is filed once its navigation has run, which is here.
	if (pending_transition_) {
		transition_ = MenuTransition{.from = transition_from_, .to = focused_value(), .cause = *pending_transition_};
		pending_transition_.reset();
	}
	return r;
}

void MenuScene::flip_in_place(const Node& node) {
	// The list row reads its value live, so a refused flip already draws the stored one.
	(void)bindings::write(node, ctx_, bindings::read(node, ctx_) == 0 ? 1 : 0);
}

void MenuScene::mark_edited_after_turn() {
	// `currentSound`, which the slot stands for, and `markInstrumentAsEdited`'s `!inSettingsMenu()`: the settings menu
	// can be opened over a synth with a slot resolved.
	if (ctx_.ports.controls_sink == nullptr || ctx_.root == MenuContext::Root::Settings || !ctx_.addresses.slot
	    || !select_turn_edits_instrument(ctx_, st_)) {
		return;
	}
	const deluge::project::SlotAddress slot = *ctx_.addresses.slot;
	const bool drum = slot.kind == deluge::project::SlotAddress::Kind::DRUM;
	(void)ctx_.ports.controls_sink->instrument_edited(deluge::ui::scenes::bind::KnobFocus{
	    .owner = deluge::ui::scenes::bind::SoundFocus{
	        .slot = slot, .clip = ctx_.addresses.clip, .kit = drum ? ctx_.addresses.kit_output : std::nullopt}});
}

Result MenuScene::after_render_navigation() {
	Result r;
#ifdef DELUGE_HOST
	drain_trace_len_ = 0;
#endif
	// A `Value`-derived bespoke item's cursor moved under a held Select is a value edited under it.
	const Node* const shown = focused();
	const bool value_turned = shown != nullptr && shown->kind == Kind::Custom && shown->custom != nullptr
	                          && shown->custom->is_value() && st_.tick.render_resp.changed;
	if (std::exchange(visit_.use_up_select, false) || st_.tick.use_up_select
	    || ((st_.tick.value_changed || value_turned) && visit_.select_held)) {
		(void)r.actions.push_back(Action{.kind = Action::Kind::UseUpSelect});
	}
	if (walk_.take_close_request()) {
		// `ascend()` went up from depth 0 since the last render (`on_resume`'s go-up): `exitCompletely()`.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Remove, .pop_to = SceneKind::FirstApp});
		return r;
	}
	if (const uint16_t to = std::exchange(pending_retarget_, 0); to != 0) {
		// `dx_column_press` off the parameter page: `setup()` re-roots, as a shortcut's `Replace` does.
		r.handled = true;
		(void)r.actions.push_back(Action{.kind = Action::Kind::Replace, .id = SceneId{SceneKind::FirstApp, to}});
		return r;
	}
	if (std::exchange(st_.walk.exit_completely, false)) {
		// An item's `soundEditor.exitCompletely()`.
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
	// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), which `MultiRangeItem` cannot do itself: it is handed a
	// `const MenuContext&` and this is the scene's own mutable one. Applied on every tick the picker
	// republishes its cursor, which is exactly when legacy calls it (`MultiRange::beginSession`, `selectEncoderAction`,
	// `deletePress` and `noteOnToChangeRange`).
	if (st_.walk.range_choice >= 0) {
		trace_drain(DrainBand::RangeChoice);
		select_range(ctx_, st_.walk.range_choice);
		st_.walk.range_choice = -1;
	}

	// The CV/gate selectors' own commit -- `soundEditor.currentSourceIndex = this->getValue()`
	// (`cv::Selection::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/cv/selection.h`,
	// `gate::Selection::selectButtonPress` in `origin/main:src/deluge/gui/menu_item/gate/selection.h`) -- and the
	// device list's assignment of `soundEditor`'s current MIDI cable from `getCable(...)` (`Devices::beginSession` in
	// `origin/main:src/deluge/gui/menu_item/midi/devices.cpp`, `Devices::selectEncoderAction`). Same shape as
	// `range_choice`, and deliberately **above** the `entered` arm below: legacy assigns `currentSourceIndex` on the
	// way through `selectButtonPress`, so `gate_selection_chain` and the mode page's per-output row must both see the
	// output the user just picked, not the previous one.
	if (st_.walk.source_choice >= 0) {
		trace_drain(DrainBand::SourceChoice);
		ctx_.source_index = st_.walk.source_choice;
		st_.walk.source_choice = -1;
	}
	if (st_.walk.midi_device_choice) {
		trace_drain(DrainBand::MidiDeviceChoice);
		ctx_.midi_device = st_.walk.midi_device_choice;
		st_.walk.midi_device_choice = {};
	}

	if (st_.tick.value_changed && st_.tick.value_node != nullptr) {
		trace_drain(DrainBand::ValueCommit);
		visit_.session.commit(core(), *st_.tick.value_node);
		// The write raises its own `notify::Dirty` facts (`bindings.cpp`), which repaint the
		// surfaces those name; `RepaintAll` covers what a commit changed *off* this surface (a
		// mod-knob indicator, the automation grid). Never `Action{Repaint, SurfaceRole::None}`,
		// which is a documented no-op (`Action::Kind::Repaint`'s doc in `ui/shell/scene.h`).
		repaint = true;
	}

	// After the write, as legacy marks after `writeCurrentValue`: `beenEdited` copies the clip's arp as it now is.
	if (st_.tick.select_turned) {
		mark_edited_after_turn();
	}

	// The horizontal menu's notification popup, `HorizontalMenu::displayNotification`
	// (`origin/main:src/deluge/gui/menu_item/horizontal_menu.cpp`): it shows the item's *name* alongside its
	// value. Legacy raises it on a value change (the tail of `HorizontalMenu::selectEncoderAction`) and on a
	// button focus move or an already-focused button press that does not open anything
	// (`handleInstrumentButtonPress`'s tail, `handleItemAction` -- both ported in
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

	// `Automation::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/automation/automation.cpp`): shift +
	// select on an automation item deletes its param's automation, raises the popup where there was a param, and stays
	// put -- on a leaf, and on a horizontal page's value column, which `Submenu::selectButtonPress` forwards the press
	// to.
	bool deleting_automation = false;
	if (st_.tick.select_shift && (st_.tick.render_resp.entered || st_.tick.column_press)) {
		const Node* item =
		    st_.tick.render_resp.entered ? (node->kind == Kind::Submenu ? nullptr : node) : selected_child(*node);
		if (item != nullptr && is_automation_item(*item)) {
			deleting_automation = true;
			const std::optional<deluge::project::ParamAddress> address = automation_address(*item, ctx_, st_);
			if (address && ctx_.ports.writes != nullptr && ctx_.ports.writes->delete_automation(*address)) {
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
				if (flips_in_place(*child)) {
					// `shouldEnterSubmenu()` is false: `SoundEditor` calls the item's `selectButtonPress`, which flips
					// it from the list and answers NO_NAVIGATION; no permission check runs on this arm.
					flip_in_place(*child);
					repaint = true;
				}
				else {
					walk_.enter_child(core(), visit_.session, *child);
				}
			}
		}
		else if (const Node* chained = node->select_chain != nullptr ? node->select_chain(ctx_, st_) : nullptr) {
			// `MenuItem::selectButtonPress` returning a menu: `SoundEditor` navigates *into* it rather than going up
			// (the `SELECT_ENC` arm of `SoundEditor::buttonAction` in `origin/main:src/deluge/gui/ui/sound_editor.cpp`
			// -> `enterSubmenu`, which is `navigationDepth++` and a record write, exactly what `enter_child` does). The
			// node's own hook answers, because the answer is value-dependent for two of the seven ported overrides --
			// see `node.h`'s `SelectChainFn`.
			//
			// `enter_child` applies legacy's `maybeGetParentMenu`/`focusChild` reparenting: an item a chained page
			// lists is entered on that page, focused on it.
			walk_.enter_child(core(), visit_.session, *chained);
		}
		else {
			// A leaf's select press goes up, it does not re-enter: `MenuItem::selectButtonPress` returns nullptr and
			// `SoundEditor` reads that as "up". A Toggle is a leaf only when it is the current item (a shortcut opened
			// it), where `Toggle::selectButtonPress` goes up without flipping; from a list its row flips instead
			// (`flips_in_place`, above).
			walk_.ascend(core(), visit_.session);
		}
	}
	else if (st_.tick.column_press) {
		// `Submenu::selectButtonPress` forwarding to the focused item, on a horizontal page's value
		// column (`origin/main:src/deluge/gui/menu_item/submenu.cpp`): the column's own `selectButtonPress()`
		// override runs FIRST, as the horizontal forward's own call -- not a separate arm bolted on
		// after. No notification either way: neither `Toggle`'s/`Selection`'s `selectButtonPress`
		// nor the chain/ascend arms below call `displayNotification` (see `MenuState::
		// column_press`'s doc).
		if (const Node* column = selected_child(*node); column != nullptr && column->kind != Kind::Submenu) {
			if (column->kind == Kind::Toggle || column->selection_is_toggle) {
				// `Toggle::selectButtonPress` and `Selection::selectButtonPress` when `isToggle()`: flip and answer
				// NO_NAVIGATION, so nothing chains or goes up. `Node::selection_is_toggle`'s doc names the three
				// columns that reach here.
				flip_in_place(*column);
				repaint = true;
			}
			else if (column->allow_session_from_horizontal) {
				// `Submenu::selectButtonPress` returns the item itself when it begins its own session from the
				// horizontal page (`origin/main:src/deluge/gui/menu_item/submenu.cpp`), and the select arm enters it
				// the general way.
				walk_.enter_child(core(), visit_.session, *column);
			}
			else if (const Node* chained =
			             column->select_chain != nullptr ? column->select_chain(ctx_, st_) : nullptr) {
				// `PatchedParam::selectButtonPress` (`origin/main:src/deluge/gui/menu_item/patched_param.cpp`) records
				// the param it patches. The source page reads it from the level above it, which here is the page, not
				// the column.
				if (is_patched(column->bind.param)) {
					st_.walk.patching_param = patched_byte(column->bind.param);
				}
				walk_.enter_child(core(), visit_.session, *chained);
			}
			else if (stays_on_page(*column)) {
				// `Selection::selectButtonPress` from the page: `toggleValue()` changes nothing on a selection that is
				// not a toggle, and the answer is NO_NAVIGATION, so the page stays.
			}
			else {
				// `MenuItem::selectButtonPress`'s null: up a level.
				walk_.ascend(core(), visit_.session);
			}
		}
	}
	else if (st_.tick.back_claimed) {
		// Only ever set at depth > 0 (see `render_oled`); at depth 0 the back was never claimed
		// and `handle()` pops the scene instead.
		walk_.ascend(core(), visit_.session);
	}

	// The verb half of the `pending_scene` seam (`ui/scenes/menu/action.h`): a `CustomItem` that asked
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
	//     `shouldEnterSubmenu()` is false, so `SoundEditor` calls `selectButtonPress()` and stays on the parent list
	//     (the `else` of that test in `SoundEditor::buttonAction`, `origin/main:src/deluge/gui/ui/sound_editor.cpp`).
	//     Ascending on the tick the request is filed is what makes the port's descend-then-leave look like legacy's
	//     never-descend. `PerformEditorItem` is excluded because legacy *does* stay on that page
	//     (`EditingMode::selectButtonPress` in
	//     `origin/main:src/deluge/gui/menu_item/performance_session_view/editing_mode.h`, `NO_NAVIGATION`); it files
	//     its request from `render`, not `on_focus`, which is exactly the discriminator -- see below.
	//
	// Placed **above** the repaint check so the ascend is covered by it.
	if (st_.walk.pending_action.engaged()) {
		trace_drain(DrainBand::PendingAction);
		taken_action_ = st_.walk.pending_action;
		// `on_focus` fires from `enter_leaf`, i.e. before this tick's render; a request filed from
		// `render` (`PerformEditorItem`) arrives with the item already drawn. The four that ascend
		// are the four that file from `on_focus`, which is the same set as "main never enters
		// this item" -- so the test is on the action's own identity rather than on a flag, and an
		// action added without deciding this question fails the `switch`'s coverage warning.
		switch (st_.walk.pending_action.action) {
		case MenuAction::StartStemExport:
		case MenuAction::SetClipLengthToSampleLength:
		case MenuAction::OpenSongMidiLearn:
		case MenuAction::OpenSongMacros:
			walk_.ascend(core(), visit_.session);
			break;
		case MenuAction::OpenPerformanceView:
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
		pushed_kind_ = st_.walk.pending_scene.kind;
		st_.walk.pending_scene = SceneId{};
		st_.walk.go_up_on_resume = true;
		// `sampleBrowser.menuItemHeadingTo = item` (`SoundEditor::maybeGetParentMenu`
		// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`)), and `osc::AudioRecorder::menuItemHeadingTo`
		// (`origin/main:src/deluge/gui/menu_item/osc/audio_recorder.h`) for the recorder half. Legacy writes it on the
		// way in, from `maybeGetParentMenu`, which is exactly here: the statement beside its own `openUI`. Items with
		// no return affordance record one too and nothing reads it; the recorder's return is
		// `MenuWalkState::resume_landing`, which its item writes.
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

deluge::ui::widgets::core::InputClaims MenuScene::owned_input() const {
	deluge::ui::widgets::core::InputClaims claims = menu::owned_input(ctx_, st_);
	// A BACK the UI mode refuses goes to `handle()`, which swallows it, not to the render, which would go up.
	if (!select_and_back_act()) {
		claims.back = false;
	}
	return claims;
}

bool MenuScene::owns(const UiEvent& event) const {
	return deluge::ui::shell::claim_covers(owned_input(), event);
}

Result MenuScene::handle(const UiEvent& event, const UiContext& ctx) {
	// The SHIFT overlay follows the shift level each event brings; a change takes or gives back the pads and the
	// greyout, and while it shows a source pad's press or release can change the source column.
	const bool overlay_was_up = overlay_up();
	visit_.shift_held = ctx.shift;
	const Node* const value_before = focused_value();
	Result r = handle_event(event, ctx);
	note_transition(event, r, value_before);
	if (overlay_up() != overlay_was_up
	    || (overlay_up() && event.kind == UiEvent::Kind::Pad && is_patching_source_pad(event.x, event.y))) {
		(void)r.actions.push_back(Action{.kind = Action::Kind::RepaintAll});
	}
	return r;
}

Result MenuScene::handle_event(const UiEvent& event, const UiContext& ctx) {
	// Legacy's `UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR`, which the host enters and leaves
	// (`set_affect_entire_hold`); `bindings::write` reads it. Re-asserted here because `on_enter` re-installs the
	// context.
	ctx_.affect_entire_held = holding_affect_entire_;
	ctx_.addresses.affect_entire = bindings::writes_whole_kit(ctx_);
	// `SoundEditor::buttonAction` acts on SELECT and BACK only in the five UI modes it lists, which learn mode is not:
	// in any other, a press of either does nothing.
	if ((event.kind == UiEvent::Kind::Button || event.kind == UiEvent::Kind::EncoderButton)
	    && (event.button == ButtonId::Back || event.button == ButtonId::SelectEnc) && !select_and_back_act()) {
		return Result{.handled = true};
	}
	// A select or BACK press, and a select turn, restart the hold's blink
	// (`origin/main:src/deluge/gui/ui/sound_editor.cpp`'s three `blinkLed(IndicatorLED::AFFECT_ENTIRE, 255, 1)` sites).
	if (holding_affect_entire_
	    && (((event.kind == UiEvent::Kind::Button || event.kind == UiEvent::Kind::EncoderButton) && event.on
	         && (event.button == ButtonId::SelectEnc || event.button == ButtonId::Back))
	        || (event.kind == UiEvent::Kind::Encoder
	            && event.encoder == deluge::ui::widgets::core::EncoderRole::Select))) {
		affect_entire_blink_restart_ = true;
	}
	// A turn the page does not own (a bespoke item that takes it as an event) is routed here instead of the frame.
	if (event.kind == UiEvent::Kind::Encoder && event.encoder == deluge::ui::widgets::core::EncoderRole::Select
	    && event.delta != 0) {
		st_.tick.select_turned = true;
	}

	// A MIDI message is an offer for MIDI learn, and nothing else here reads it.
	switch (event.kind) {
	case UiEvent::Kind::MidiNoteOn:
	case UiEvent::Kind::MidiCc:
	case UiEvent::Kind::MidiProgramChange:
	case UiEvent::Kind::MidiPitchBend: {
		const bool used = visit_.learn.offer(ctx_, st_, event, ctx);
		if (used) {
			read_learned_value_again();
		}
		return Result{.handled = used};
	}
	default:
		break;
	}

	// `SoundEditor::modEncoderAction`: in learn mode a gold turn learns that knob. Under the automation view legacy
	// hands the turn to the view instead, which the knobs' service stands in for.
	if (!ctx_.root_is_automation && visit_.learn.gold_turn(ctx_, st_, event)) {
		return Result{.handled = true};
	}

	// `SoundEditor::buttonAction`'s LEARN arm (`origin/main:src/deluge/gui/ui/sound_editor.cpp`), observed only: the
	// event goes on as before.
	if (visit_.learn.observe(ctx_, st_, event, ctx,
	                         host_gates_ ? std::optional<bool>{host_gates_->learn} : std::nullopt)) {
		visit_.blink_restart = true; // the release's `blinkLed(LEARN)` (`SoundEditor::buttonAction` in
		                             // `origin/main:src/deluge/gui/ui/sound_editor.cpp`, the learn-button release)
	}
	if (event.kind == UiEvent::Kind::Button && event.button == ButtonId::Learn && event.on) {
		read_learned_value_again(); // SHIFT + LEARN's unlearn
	}

	// A custom item owns its input while it is focused: it sees every event first and, if it consumes it, the scene
	// does nothing else this call -- the same precedence legacy gives a `MenuItem`'s own `buttonAction` override over
	// the rest of `SoundEditor::buttonAction` (`SoundEditor::buttonAction` in
	// `origin/main:src/deluge/gui/ui/sound_editor.cpp`: `currentMenuItem->buttonAction(...)` runs before anything else
	// in that branch looks at the result). Checked before the shortcut-pad jump and the root `Back` below, not after: a
	// bespoke item's own input (the patch-cable pages' pad presses, a file browser's scroll) is exactly the kind of
	// thing this layer cannot interpret generically, so it must not be shadowed by a case that happens to match the
	// same event shape.
	if (const Node* focused_node = focused();
	    focused_node != nullptr && focused_node->kind == Kind::Custom && focused_node->custom != nullptr) {
		if (Result r = focused_node->custom->handle(event, ctx, ctx_, st_); r.handled) {
			return r;
		}
	}

	// The six page buttons on a horizontal page, ahead of the shortcut-pad jump, as legacy's `buttonAction` override
	// is reached before `SoundEditor::buttonAction`'s own switch.
	// SHIFT + CROSS_SCREEN/SCALE at depth 0 steps the kit's own chain while the host's latch is set
	// (`horizontalMenusChainForKit` under `editingKitAffectEntire()`), and a step lands the menu on the kit's effects.
	const bool chain_step = event.kind == UiEvent::Kind::Button && ctx.shift && st_.depth == 0
	                        && (event.button == ButtonId::CrossScreen || event.button == ButtonId::Scale);
	if (std::optional<MenuContext> kit_global = chain_step ? kit_global_shortcut_context() : std::nullopt) {
		const Node* const before = st_.path[0];
		if (std::optional<Result> r = handle_page_button(MenuCore{*kit_global, st_}, walk_, visit_.session, event, ctx,
		                                                 MenuContext::Root::KitGlobalFx)) {
			if (st_.path[0] != before) {
				install_context(*kit_global);
			}
			return *r;
		}
	}
	else if (std::optional<Result> r = handle_page_button(core(), walk_, visit_.session, event, ctx, ctx_.root)) {
		return *r;
	}

	// A shortcut press after the host's latch went true reads the kit's own effects, and a jump moves the menu there.
	const bool shortcut_press =
	    event.kind == UiEvent::Kind::Pad && event.on && (ctx.shift || event.shortcut_without_shift);
	if (std::optional<MenuContext> kit_global = shortcut_press ? kit_global_shortcut_context() : std::nullopt) {
		if (std::optional<Result> r = pads_.handle_pad(MenuCore{*kit_global, st_}, walk_, visit_.session, event, ctx)) {
			if (std::ranges::any_of(r->actions, [](const Action& a) { return a.kind == Action::Kind::Replace; })) {
				install_context(*kit_global);
			}
			return *r;
		}
	}
	else if (std::optional<Result> r = pads_.handle_pad(core(), walk_, visit_.session, event, ctx)) {
		return *r;
	}

	// Back at the root. `owns` leaves it to this arm at depth 0, which closes the editor --
	// `origin/main:src/deluge/gui/ui/sound_editor.cpp`'s BACK case -> `SoundEditor::goUpOneLevel` ->
	// `SoundEditor::exitCompletely`.
	//
	// `Remove{.pop_to = SceneKind::FirstApp}`, NOT `Pop` with any `pop_to`: `SceneStack::pop(kind)`
	// pops the topmost scene of that kind *and everything above it* (`SceneStack::pop`), so
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
		// **before** its depth-0 arm's `exitCompletely()` (`SoundEditor::goUpOneLevel`) -- the same
		// cancel `descend_to`/`ascend` make, just reached by the one path neither of them covers: a
		// depth-0 horizontal page closing the whole menu rather than navigating within it.
		// `reroot_to_scene_opener_page()` puts a claimed file selector's browser back here at
		// depth 0, so this is a shipped path, not a hypothetical one.
		if (on_horizontal_page()) {
			deluge::base::notify::notice_kind(deluge::base::notify::Notice::Kind::CancelNotification);
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

deluge::ui::widgets::grid::GreyoutMask MenuScene::greyout_mask() const {
	// While the overlay shows every column is greyed, whatever the root (`getGreyoutColsAndRows`,
	// `origin/main:src/deluge/gui/ui/sound_editor.cpp`).
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
	std::optional<uint32_t> next = pad_next_timer(ctx_, st_, kit_global_shortcut_context().has_value());
	if (dx_column_flashing()) {
		// The keyboard DX column's next flip, which the host reads after the tick (`dx_column_flash`).
		const uint32_t phase = (render_ms_ - *st_.dx.flash_anchor_ms) % dx::kColumnFlashPeriodMs;
		const uint32_t flip =
		    phase < dx::kColumnFlashOnMs ? dx::kColumnFlashOnMs - phase : dx::kColumnFlashPeriodMs - phase;
		next = next.has_value() ? std::min(*next, flip) : flip;
	}
	return next;
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
		return false; // `SoundEditor::goUpOneLevel`'s depth-0 arm, on the loop's first pass
	}
	walk_.ascend(core(), visit_.session);
	// `SoundEditor::goUpOneLevel`'s depth-0 arm on a later pass: `ascend()` stepped down to depth 0 and that arm
	// refused that level too, so it asked for the close. The caller closes the editor itself, so the request is taken
	// back here.
	return !walk_.take_close_request();
}

void MenuScene::enter_submenu(const Node& node) {
	walk_.descend_to(core(), visit_.session,
	                 node); // `SoundEditor::enterSubmenu` (`origin/main:src/deluge/gui/ui/sound_editor.cpp`)
}

bool is_automation_item(const Node& item) {
	if (item.custom != nullptr) {
		return item.custom->is_automation_item();
	}
	return item.bind.param.valid();
}

std::optional<AutomationParam> automation_param(const Node& item, const MenuContext& ctx, const MenuState& state) {
	if (item.custom != nullptr) {
		if (!item.custom->is_automation_item()) {
			return std::nullopt;
		}
		return AutomationParam{.kind = deluge::plugin::ParamKind::PATCH_CABLE,
		                       .id = static_cast<int32_t>(item.custom->learn_descriptor(ctx, state).word)};
	}
	if (!item.bind.param.valid()) {
		return std::nullopt;
	}
	return legacy_pair_of(item.bind.param);
}

AutomationParam legacy_pair_of(const ParamRef& p) {
	if (!p.id) {
		return {};
	}
	const deluge::instruments::classic::RuntimeParam runtime =
	    p.table == deluge::ui::scenes::bind::ParamTable::CHAIN
	        ? deluge::instruments::classic::chain_runtime_param_of(*p.id)
	        : deluge::instruments::classic::runtime_param_of(*p.id);
	return {.kind = runtime.kind, .id = runtime.index};
}

std::optional<deluge::project::ParamAddress> automation_address(const Node& item, const MenuContext& ctx,
                                                                const MenuState& state) {
	if (item.custom != nullptr) {
		return item.custom->automation_address(ctx, state);
	}
	if (!item.bind.param.valid()) {
		return std::nullopt;
	}
	const deluge::ui::scenes::bind::Target param{.kind = deluge::ui::scenes::bind::Target::Kind::PARAM,
	                                             .param = param_id_of(item.bind.param),
	                                             .param_table = param_table_of(item.bind.param)};
	return deluge::ui::scenes::bind::param_address(param, ctx.addresses);
}

} // namespace deluge::ui::scenes::menu
