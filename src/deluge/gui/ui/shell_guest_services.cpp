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
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "scenes/browser/browser_scene.h"
#include "scenes/context_menu/context_menu_scene.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/overlay/param_popup_scene.h"
#include "scenes/registry.h"
#include "scenes/services/global_controls.h"
#include "scenes/services/midi_learn.h"
#include "scenes/services/mod_knobs.h"
#include "scenes/slicing/slicer_scene.h"

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
	// -- it answers `lastUIBeforeNullifying` while nothing is open (`ui.cpp:165-170`), which is the
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

bool shellSoundMenuOpen() {
	const auto& scene = deluge::scenes::menu_scene();
	return getCurrentUI() == &shellHostUI && &shellHostUI.shell().stack().input_top() == &scene
	       && scene.menu_context().root != deluge::scenes::menu::MenuContext::Root::Settings;
}

bool shellSamplePreviewSounds() {
	if (getCurrentUI() != &shellHostUI) {
		return false;
	}
	const deluge::ui::shell::Scene* top = &shellHostUI.shell().stack().input_top();
	const auto& browser = deluge::scenes::browser::browser_scene();
	const auto& menu = deluge::scenes::context_menu::context_menu_scene();
	using deluge::scenes::context_menu::DescriptorId;
	return (top == &browser && browser.browser_id() == deluge::scenes::browser::BrowserId::Sample)
	       || (top == &menu
	           && (menu.descriptor_id() == DescriptorId::SampleBrowserKit
	               || menu.descriptor_id() == DescriptorId::SampleBrowserSynth))
	       || top == &deluge::scenes::slicing::slicer_scene();
}

void shellMenuReadValueAgain() {
	deluge::scenes::menu_scene().read_value_again();
}

bool guestKnobsSeeAutomationEditor() {
	return guestServices().knobs.view_state().in_automation_editor;
}

void wireShellGuestServices() {
	GuestServices& services = guestServices();
	ShellHostUI::setCloseHook(&handOffGuestOverlays);

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
