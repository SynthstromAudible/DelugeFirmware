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

#include "gui/ui/shell_guest_services.h"

#include "gui/ui/menu_view_of.h"

#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/save/save_instrument_preset_ui.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/shell_note_mode.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/instrument_clip_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include "model/clip/instrument_clip.h"
#include "model/model_stack.h"
#include "model/project_bridge/slot_bridge.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/marker/marker_scene.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/menu/nodes/midi_device.h"
#include "scenes/menu/nodes/multi_range.h"
#include "scenes/overlay/param_popup_scene.h"
#include "scenes/recorder/recorder_scene.h"
#include "scenes/registry.h"
#include "scenes/rename/rename_scene.h"
#include "scenes/services/global_controls.h"
#include "scenes/services/midi_learn.h"
#include "scenes/services/mod_knobs.h"
#include "scenes/slicing/slicer_scene.h"
#include "util/shell_menu_entry.h"
#include "util/sound_menu_open.h"

#include <atomic>

namespace deluge::gui::ui {
namespace {

/// @brief The three services, in the one order their constructors allow.
///
/// `learn` first because `GlobalControls` takes a `LearnMode&` and a real application passes the
/// same `MidiLearn` it also puts in `Services::midi_learn` (`GlobalControls`'s constructor);
/// member order is what makes that reference valid rather than a promise. Same shape as
/// `sim/scenes_spec/scene_scripts_spec.cpp`'s `RealServices`, which is the level-2 demonstration of
/// this wiring.
struct GuestServices {
	deluge::scenes::services::MidiLearn learn;
	deluge::scenes::services::ModKnobs knobs;
	deluge::scenes::services::GlobalControls controls;

	explicit GuestServices(deluge::ui::shell::Shell& shell)
	    : learn(shell.stack()), knobs(deluge::scenes::overlay::param_popup_scene(), shell.stack(), shell.lowering()),
	      controls(shell.stack(), shell.lowering(), learn) {
		// One focus for the gold knobs: the legacy views set it, and the guest's knobs read and move it.
		knobs.use_focus(view.knobFocus(), [] { view.knobFocusChanged(); });
	}
};

/// @brief The one set, built on first use over `shellHostUI`'s own `Shell`.
///
/// A function-local static rather than a namespace-scope object, for the reason
/// `scenes::menu_scene()` gives: it must not participate in static-initialisation order across
/// translation units, and `shellHostUI` -- whose `SceneStack` and `InputLowering` these three hold
/// references to -- is a global in another one. The first call happens at boot, long after every
/// global is constructed.
GuestServices& guestServices() {
	static GuestServices services{shellHostUI.shell()};
	return services;
}

/// @brief Give the knobs and the global controls the two `ViewState` blocks, read from the legacy views now.
void setViewStates(GuestServices& services) {
	// The two `ViewState` blocks -- the questions about *which legacy view is on screen* that a
	// service in `src/scenes` has no way to ask. Each field is its own doc comment's legacy
	// expression, evaluated here.
	deluge::scenes::services::ModKnobs::ViewState knobView;
	knobView.in_automation_editor = getRootUI() == &automationView && automationView.inAutomationEditor();
	knobView.automation_on_arranger = automationView.onArrangerView;
	knobView.root_is_performance = getRootUI() == &performanceView;
	knobView.root_is_clip_minder = rootUIIsClipMinderScreen();
	knobView.mod_button_ui_mode_in_range = view.modButtonUIModeInRange();
	services.knobs.set_view_state(knobView);

	deluge::scenes::services::GlobalControls::ViewState controlView;
	// `currentUIIsClipMinderScreen()` (`gui/ui/ui.cpp`) rather than the bare
	// `getCurrentUI()->toClipMinder()` the field's doc quotes from `gui/views/view.cpp:522`: it is
	// the same question with legacy's own null guard on it, and `getCurrentUI()` really can be null
	// -- it answers `lastUIBeforeNullifying` while nothing is open (`ui.cpp:166-171`), which is the
	// state during a song swap and the state a spec that opens no UI is in.
	controlView.current_ui_is_clip_minder = currentUIIsClipMinderScreen();
	controlView.root_is_performance = getRootUI() == &performanceView;
	controlView.root_greyout = deluge::gui::menuRootGreyout();
	controlView.loading_song = loadSongUI.isLoadingSong();
	controlView.current_ui_is_song_or_arranger =
	    getCurrentUI() == static_cast<UI*>(&sessionView) || getCurrentUI() == static_cast<UI*>(&arrangerView);
	// `ui_mode` is left at `UiMode::None` for the same reason and with a stronger one behind it:
	// there is no total mapping from legacy's 47-value `currentUIMode` bitfield onto the port's
	// enum, and `GlobalControls`'s own doc says `MidiLearn` is deliberately not settable through
	// this struct because `effective_ui_mode()` takes that from `LearnMode::active()` instead
	// (`GlobalControls::ViewState`'s doc). Every arm that reads it is inside the
	// `View::buttonAction` group, which `GlobalControls::handle` gates on
	// `stack_.input_top().is_root()` (`GlobalControls::handle`) -- and the guest's only
	// tenant, `MenuScene`, is not a root scene. So no arm that reads `ui_mode` is reachable under
	// the guest. Uncovered, and named as uncovered.
	services.controls.set_view_state(controlView);
}

} // namespace

void handOffGuestOverlays(uint32_t now_ms) {
	// The menu's kit context goes with it (`adoptShellMenuContext` wrote it at open), as legacy's `exitCompletely`
	// clears it; a legacy menu opened afterwards sets it only when it means it.
	soundEditor.setupKitGlobalFXMenu = false;
	clearShellKitGlobalFx();
	// The automation view's follow of the closing menu's last move goes with the menu (`closeGuest` has applied the
	// flush's settings already). On the tick's close path the drain after `Shell::tick` has emptied it.
	(void)deluge::scenes::menu_scene().take_transition();
	if (const std::optional<deluge::scenes::overlay::ShownPopup> shown =
	        deluge::scenes::overlay::param_popup_scene().hand_off(now_ms)) {
		display->displayNotification(shown->name, shown->value);
		uiTimerManager.setTimer(TimerName::DISPLAY, static_cast<int32_t>(shown->remaining_ms));
		view.adoptModEncoderValuePopup(shown->kind, shown->param_id, shown->display_value, shown->source1,
		                               shown->source2);
	}
}

void refreshShellRootFacts() {
	setViewStates(guestServices());
	deluge::scenes::menu_scene().set_root_facts(deluge::gui::menuRootGreyout(), getRootUI() == &keyboardScreen,
	                                            getRootUI() == &automationView);
}

bool shellMenuFrontmost() {
	return getCurrentUI() == &shellHostUI && &shellHostUI.shell().stack().input_top() == &deluge::scenes::menu_scene();
}

bool shellSoundMenuOpen() {
	return shellMenuFrontmost()
	       && deluge::scenes::menu_scene().menu_context().root != deluge::scenes::menu::MenuContext::Root::Settings;
}

namespace {
/// The guest's answer to "does the sample preview sound", published by the guest's tick for the audio tier, which
/// renders the preview drum and may not walk the guest's scene stack while the interaction tier changes it.
std::atomic<bool> guestPreviewSounds{false};
} // namespace

bool shellRangePickerOpen() {
	return shellSoundMenuOpen() && deluge::scenes::menu_scene().focused() == &deluge::scenes::menu::nodes::multi_range;
}

bool drumSelectionFrozenBy(uint8_t raw_kind, uint16_t which) {
	const auto kind = static_cast<deluge::ui::shell::SceneKind>(raw_kind);
	if (kind == deluge::scenes::browser::kBrowserSceneKind) {
		return which == static_cast<uint16_t>(deluge::scenes::browser::BrowserId::Sample);
	}
	if (kind == deluge::scenes::rename::kRenameSceneKind) {
		return which == static_cast<uint16_t>(deluge::scenes::rename::TargetId::Drum);
	}
	return kind == deluge::scenes::marker::kMarkerSceneKind;
}

bool shellFreezesDrumSelection() {
	if (getCurrentUI() != &shellHostUI) {
		return false;
	}
	const deluge::ui::shell::Scene& top = shellHostUI.shell().stack().input_top();
	uint16_t which = 0;
	if (&top == &deluge::scenes::browser::browser_scene()) {
		which = static_cast<uint16_t>(deluge::scenes::browser::browser_scene().browser_id());
	}
	else if (&top == &deluge::scenes::rename::rename_scene()) {
		which = static_cast<uint16_t>(deluge::scenes::rename::rename_scene().target_id());
	}
	return drumSelectionFrozenBy(static_cast<uint8_t>(top.kind()), which);
}

bool shellRangeDependentOpen() {
	if (getCurrentUI() != &shellHostUI) {
		return false;
	}
	const deluge::ui::shell::Scene* top = &shellHostUI.shell().stack().input_top();
	const auto& browser = deluge::scenes::browser::browser_scene();
	if ((top == &browser && browser.browser_id() == deluge::scenes::browser::BrowserId::Sample)
	    || top == &deluge::scenes::recorder::recorder_scene()) {
		return true;
	}
	if (!shellSoundMenuOpen()) {
		return false;
	}
	const deluge::scenes::menu::Node* node = deluge::scenes::menu_scene().focused();
	return node != nullptr && (node->range_dependent || node == &deluge::scenes::menu::nodes::multi_range);
}

bool shellNoteWithinCurrentRange(int32_t untransposed) {
	// `SoundEditor::isUntransposedNoteWithinRange` (`sound_editor.cpp`), on the guest's source and range.
	const deluge::scenes::menu::MenuContext& ctx = deluge::scenes::menu_scene().menu_context();
	if (currentSong == nullptr || !ctx.addresses.slot || ctx.source_index < 0 || ctx.source_index >= kNumSources
	    || ctx.range_index < 0) {
		return false;
	}
	::Sound* sound = deluge::bridge::soundAt(*currentSong, *ctx.addresses.slot);
	if (sound == nullptr) {
		return false;
	}
	Source& source = sound->sources[ctx.source_index];
	if (std::ssize(source.ranges) <= 1 || ctx.range_index >= std::ssize(source.ranges)) {
		return false;
	}
	return source.getRange(untransposed + sound->getTranspose()) == source.ranges.getElement(ctx.range_index);
}

void shellChangeRangeForNote(int32_t note) {
	(void)deluge::scenes::menu_scene().select_range_for_note(note);
}

namespace {
/// The ported menu's source and range at the last guest tick, which `refreshShellRangeDisplay` compares against.
int32_t lastRangeShown = -1;
int32_t lastRangeSource = -1;
/// Whether the root drew the range highlight at the last guest tick: it goes when the guest leaves the range-dependent
/// item, as a menu move re-renders legacy's root.
bool lastHighlighting = false;
/// How many ranges the source had: an insert or delete re-renders the root even where the range index stays.
int32_t lastRangeCount = -1;
/// The edges of the range shown: an edge edit moves them and re-renders the keyboard, as legacy's
/// `MultiRange::selectEncoderAction` does on every arm; the clip view is left as it was.
int32_t lastRangeBottom = -1;
int32_t lastRangeTop = -1;
/// The menu's screen at the last guest tick: `SoundEditor::beginScreen` re-renders the root's range display on every
/// screen change (`possibleChangeToCurrentRangeDisplay`), which redraws a kit's selected-drum pad for affect-entire.
const deluge::scenes::menu::Node* lastScreen = nullptr;
} // namespace

void refreshMenuModeGates() {
	const bool selectAndBack =
	    currentUIMode == UI_MODE_NONE || currentUIMode == UI_MODE_AUDITIONING || currentUIMode == UI_MODE_NOTES_PRESSED
	    || currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR || currentUIMode == UI_MODE_STUTTERING;
	deluge::scenes::menu_scene().set_host_mode_gates(
	    {.learn = currentUIMode == UI_MODE_NONE, .select_and_back = selectAndBack});
}

bool shellMenuLearnMode() {
	return deluge::scenes::menu_scene().learn_mode();
}

void refreshShellRangeDisplay() {
	const deluge::scenes::menu::MenuContext& ctx = deluge::scenes::menu_scene().menu_context();
	const bool open = shellSoundMenuOpen();
	const int32_t range = open ? ctx.range_index : -1;
	const int32_t source = open ? ctx.source_index : -1;
	const bool highlighting = shellRangeDependentOpen();
	const int32_t count = open ? deluge::scenes::menu::range_count(ctx) : -1;
	const int32_t bottom = range > 0 ? deluge::scenes::menu::range_top_note(ctx, range - 1) : -1;
	const int32_t top = range >= 0 ? deluge::scenes::menu::range_top_note(ctx, range) : -1;
	const deluge::scenes::menu::Node* screen = open ? deluge::scenes::menu_scene().focused() : nullptr;
	const bool edges_only = range == lastRangeShown && source == lastRangeSource && highlighting == lastHighlighting
	                        && count == lastRangeCount && screen == lastScreen;
	if (edges_only && bottom == lastRangeBottom && top == lastRangeTop) {
		return;
	}
	lastRangeShown = range;
	lastRangeSource = source;
	lastHighlighting = highlighting;
	lastRangeCount = count;
	lastRangeBottom = bottom;
	lastRangeTop = top;
	lastScreen = screen;
	// An edge moved: `MultiRange::selectEncoderAction`'s edge arm re-renders the keyboard and nothing else, so the
	// clip view's sidebar keeps the highlight it had.
	RootUI* root = getRootUI();
	if (edges_only) {
		if (root == &keyboardScreen) {
			uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
		}
		return;
	}
	// `SoundEditor::possibleChangeToCurrentRangeDisplay` (`sound_editor.cpp:1223-1232`).
	if (root == &keyboardScreen) {
		uiNeedsRendering(&keyboardScreen, 0xFFFFFFFF, 0);
	}
	else if (root != nullptr && root->getUIContextType() == UIType::INSTRUMENT_CLIP) {
		uiNeedsRendering(root, 0, 0xFFFFFFFF);
	}
}

std::optional<int32_t> shellDxColumnFlashRow() {
	return deluge::scenes::menu_scene().dx_column_flash(shellHostUI.nowMs());
}

void refreshShellDxColumn() {
	static std::optional<int32_t> lastFlashRow;
	const std::optional<int32_t> row = dxColumnFlashRow();
	if (row == lastFlashRow) {
		return;
	}
	lastFlashRow = row;
	// `blinkSideColumn`'s `uiNeedsRendering(&keyboardScreen, 0, 0xFFFFFFFF)`: the sidebar alone.
	if (getRootUI() == &keyboardScreen) {
		uiNeedsRendering(&keyboardScreen, 0, 0xFFFFFFFF);
	}
}

void refreshShellSamplePreviewSounds() {
	const deluge::ui::shell::Scene* top = &shellHostUI.shell().stack().input_top();
	const auto& browser = deluge::scenes::browser::browser_scene();
	const auto& menu = deluge::scenes::context_menu::context_menu_scene();
	using deluge::scenes::context_menu::DescriptorId;
	const bool sounds = (top == &browser && browser.browser_id() == deluge::scenes::browser::BrowserId::Sample)
	                    || (top == &menu
	                        && (menu.descriptor_id() == DescriptorId::SampleBrowserKit
	                            || menu.descriptor_id() == DescriptorId::SampleBrowserSynth))
	                    || top == &deluge::scenes::slicing::slicer_scene();
	guestPreviewSounds.store(sounds, std::memory_order_relaxed);
}

bool shellSamplePreviewSounds() {
	return getCurrentUI() == &shellHostUI && guestPreviewSounds.load(std::memory_order_relaxed);
}

std::optional<ActionResult> forwardMenuSaveButton(deluge::hid::Button b, bool on) {
	if (b != deluge::hid::button::SAVE || !shellSoundMenuOpen()) {
		return std::nullopt;
	}
	// `SoundEditor::buttonAction` (`sound_editor.cpp:611-616`, `:719-730`); `shellSoundMenuOpen` is its
	// `!inSettingsMenu()`.
	const Clip* clip = rootUIIsClipMinderScreen() ? getCurrentClip() : nullptr;
	const bool instrument_clip_view = clip != nullptr && clip->type == ClipType::INSTRUMENT;
	if (on && currentUIMode == UI_MODE_NONE && instrument_clip_view) {
		if (Buttons::isShiftButtonPressed()) {
			deluge::scenes::menu_scene().delete_range_press();
		}
		else {
			openUI(&saveInstrumentPresetUI);
		}
	}
	return ActionResult::DEALT_WITH;
}

std::optional<ActionResult> forwardMenuAffectEntire(deluge::hid::Button b, bool on) {
	if (b != deluge::hid::button::AFFECT_ENTIRE || !shellMenuFrontmost()) {
		return std::nullopt;
	}
	const Clip* clip = rootUIIsClipMinderScreen() ? getCurrentClip() : nullptr;
	if (clip == nullptr || clip->type != ClipType::INSTRUMENT || activeNoteMode() != NoteModeKind::None) {
		return std::nullopt;
	}
	deluge::scenes::menu::MenuScene& menu = deluge::scenes::menu_scene();
	if (menu.takes_affect_entire()) {
		if (on) {
			if (currentUIMode == UI_MODE_NONE) {
				currentUIMode = UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR;
				menu.set_affect_entire_hold(true);
			}
		}
		else if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR) {
			view.setModLedStates();
			currentUIMode = UI_MODE_NONE;
			menu.set_affect_entire_hold(false);
		}
		return ActionResult::DEALT_WITH;
	}
	const ActionResult result = instrumentClipView.InstrumentClipMinder::buttonAction(b, on);
	menu.set_clip_affect_entire(static_cast<const InstrumentClip*>(clip)->affectEntire);
	shellHostUI.refreshRootFacts();
	return result;
}

void shellMenuReadValueAgain() {
	deluge::scenes::menu_scene().read_value_again();
}

void shellMpeZonesPotentiallyUpdated() {
	if (shellMenuFrontmost()
	    && deluge::scenes::menu_scene().focused() == &deluge::scenes::menu::nodes::mpe_zone_num_member_channels) {
		deluge::scenes::menu_scene().read_value_again();
	}
}

bool guestKnobsSeeAutomationEditor() {
	return guestServices().knobs.view_state().in_automation_editor;
}

void wireShellGuestServices() {
	GuestServices& services = guestServices();
	ShellHostUI::setCloseHook(&handOffGuestOverlays);
	wireShellKitGlobalShortcuts();

	// `currentSong`, which `src/scenes` may not read for itself (`check_layer_rules.py`'s
	// `scenes-currentSong` row) and which all three services resolve against
	// (`ModKnobs::set_song`, `GlobalControls::set_song`, `MidiLearn::set_song`).
	services.learn.set_song(currentSong);
	services.knobs.set_song(currentSong);
	services.controls.set_song(currentSong);
	// A guest opens with the knobs untouched, so its first turn is not taken as a repeat of the last.
	services.knobs.pretend_knobs_untouched();
	// The clip the views have open, which the global buttons and MIDI learn ask about.
	deluge::scenes::services::view_state().set_current_view(currentSong != nullptr
	                                                            ? deluge::gui::menuViewOf(currentSong->getCurrentClip())
	                                                            : deluge::scenes::services::MenuView{});

	setViewStates(services);

	// Installs the services into the guest's `Shell`. Safe over a live set:
	// it re-seats `services_`, the `Router`'s copy and the deduplicated tick list together, so a
	// second call neither double-ticks nor strands a slot (`shell/shell.h`'s `set_services`).
	shellHostUI.shell().set_services(
	    {.mod_knobs = &services.knobs, .global_controls = &services.controls, .midi_learn = &services.learn});
}

} // namespace deluge::gui::ui
