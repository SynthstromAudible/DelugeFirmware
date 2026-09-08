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

#include "gui/ui/load/load_song_ui.h"
#include "gui/ui/shell_host_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/arranger_view.h"
#include "gui/views/automation_view.h"
#include "gui/views/performance_view.h"
#include "gui/views/session_view.h"
#include "gui/views/view.h"
#include "hid/buttons.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "scenes/menu/menu_scene.h"
#include "scenes/overlay/param_popup_scene.h"
#include "scenes/registry.h"
#include "scenes/services/global_controls.h"
#include "scenes/services/midi_learn.h"
#include "scenes/services/mod_knobs.h"

namespace deluge::gui::ui {
namespace {

/// @brief The three services, in the one order their constructors allow.
///
/// `learn` first because `GlobalControls` takes a `LearnMode&` and a real application passes the
/// same `MidiLearn` it also puts in `Services::midi_learn` (`services/global_controls.h:289-292`);
/// member order is what makes that reference valid rather than a promise. Same shape as
/// `sim/scenes_spec/scene_scripts_spec.cpp`'s `RealServices`, which is the level-2 demonstration of
/// this wiring.
struct GuestServices {
	deluge::scenes::services::MidiLearn learn;
	deluge::scenes::services::ModKnobs knobs;
	deluge::scenes::services::GlobalControls controls;

	explicit GuestServices(deluge::ui::shell::Shell& shell)
	    : learn(shell.stack()), knobs(deluge::scenes::overlay::param_popup_scene(), shell.stack(), shell.lowering()),
	      controls(shell.stack(), shell.lowering(), learn) {}
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

/// @brief Mirror `View::activeModControllableModelStack` into the port's `ModTarget`.
///
/// Legacy's gold-knob state is a live `ModelStackWithThreeMainThings` on `View` (`gui/views/view.h:120`),
/// set by whichever root view is open -- `InstrumentClipMinder::focusRegained` calls
/// `view.setActiveModControllableTimelineCounter(getCurrentInstrumentClip())`
/// (`model/clip/instrument_clip_minder.cpp:291-295`), and the other views their own equivalents.
/// `ModKnobs` keeps the same six pointers as POD (`scenes/services/mod_target.h`) and nothing sets
/// them for it, so an installed-but-unseeded `ModKnobs` reports `mode() == -1` and
/// `target_.mod_controllable == nullptr` -- which makes its `leds()` declare **all eight mod LEDs
/// off** (`services/mod_knobs.cpp:725-735`) and its `declare_indicators` declare **level 0 on both
/// gold knobs** (`:684-690`, the port of `indicator_leds::clearKnobIndicatorLevels`). Both are
/// written every tick the guest runs, so an unseeded service is not inert: it blanks the mod LEDs
/// and the two indicator rings that legacy is showing. Seeding is what makes the guest's LEDs equal
/// `SoundEditor`'s (plan F1, global constraint 1), not a nicety.
///
/// The mirror uses legacy's own two entry-point shapes rather than a field copy, because those are
/// what `View` itself distinguishes: `setActiveModControllableTimelineCounter` (`view.cpp:1884-1916`)
/// resolves the controllable and param manager *from* a counter, and
/// `setActiveModControllableWithoutTimelineCounter` (`:1900-1922`) is handed them directly and
/// leaves the counter null. `ModelStackWithTimelineCounter::timelineCounterIsSet()`
/// (`model/model_stack.h:145`) is exactly the discriminator legacy's own `ModTarget` doc cites.
///
/// @warning **A snapshot, not a subscription.** Legacy re-reads the member on every turn; this
///          re-reads it whenever `wireShellGuestServices()` is called, i.e. at boot and on every
///          guest open. Nothing changes `view`'s target while a menu is open (the root view
///          underneath is not taking input), so the snapshot is current for the whole of a guest
///          session -- but that is an argument about the first tenant, not a general one.
void seedModTarget(deluge::scenes::services::ModKnobs& knobs) {
	ModelStackWithThreeMainThings& stack = view.activeModControllableModelStack;
	if (stack.timelineCounterIsSet()) {
		// `send_midi_feedback` false: legacy sends it when the target *changes*
		// (`view.cpp:1913`), and this call is re-asserting a target legacy already set. Passing
		// true would emit a `sendCCWithoutModelStackForMidiFollowFeedback` burst on every guest
		// open that legacy has no counterpart for.
		knobs.set_target_with_timeline(stack.getTimelineCounter(), /*send_midi_feedback=*/false);
	}
	else {
		knobs.set_target_without_timeline(stack.modControllable, stack.paramManager);
	}
	// `View::modPos` / `modLength` / `modNoteRowId` (`gui/views/view.h:130-135`), the three globals
	// `View::setModRegion` writes (`view.cpp:1940-1967`) and every automation read/write uses.
	knobs.set_mod_region(view.modPos, view.modLength, view.modNoteRowId);
}

} // namespace

void wireShellGuestServices() {
	GuestServices& services = guestServices();

	// `currentSong`, which `src/scenes` may not read for itself (`check_layer_rules.py`'s
	// `scenes-currentSong` row) and which all three services resolve against
	// (`services/mod_knobs.h:240-249`, `global_controls.h:305-313`, `scenes/services/midi_learn.h:279`).
	services.learn.set_song(currentSong);
	services.knobs.set_song(currentSong);
	services.controls.set_song(currentSong);
	// After `set_song`, never before: `ModKnobs::set_target_with_timeline` is a no-op with a null
	// song and says so at the code (`services/mod_knobs.cpp:121-126`).
	seedModTarget(services.knobs);

	// The two `ViewState` blocks -- the questions about *which legacy view is on screen* that a
	// service in `src/scenes` has no way to ask. Each field is its own doc comment's legacy
	// expression, evaluated here.
	deluge::scenes::services::ModKnobs::ViewState knobView;
	knobView.in_automation_editor = getRootUI() == &automationView && automationView.inAutomationEditor();
	knobView.automation_on_arranger = automationView.onArrangerView;
	knobView.root_is_performance = getRootUI() == &performanceView;
	knobView.root_is_clip_minder = rootUIIsClipMinderScreen();
	// `mod_button_ui_mode_in_range` is left at its default **true**, and that is a gap rather than
	// an answer: legacy's `isUIModeWithinRange(modButtonUIModes)` (`gui/views/view.cpp:1529`) reads
	// a table that is `static` inside `view.cpp` (`:1487-1495`) and so cannot be evaluated here.
	// True is legacy's answer whenever `currentUIMode` is `UI_MODE_NONE`, which is what it is while
	// a menu is open; what it cannot reproduce is a mode outside the list. Inert for this plan --
	// mod buttons never reach the shell at all under the guest, because `Buttons::buttonAction`
	// intercepts them before the UI hook (`hid/buttons.cpp:99-107`) -- and stated rather than
	// silently defaulted.
	services.knobs.set_view_state(knobView);

	deluge::scenes::services::GlobalControls::ViewState controlView;
	// `currentUIIsClipMinderScreen()` (`gui/ui/ui.cpp:152-155`) rather than the bare
	// `getCurrentUI()->toClipMinder()` the field's doc quotes from `gui/views/view.cpp:521`: it is
	// the same question with legacy's own null guard on it, and `getCurrentUI()` really can be null
	// -- it answers `lastUIBeforeNullifying` while nothing is open (`ui.cpp:136-141`), which is the
	// state during a song swap and the state a spec that opens no UI is in. Found by this call
	// segfaulting `sim/scenes_spec/boot_wiring_spec.cpp`.
	controlView.current_ui_is_clip_minder = currentUIIsClipMinderScreen();
	controlView.root_is_performance = getRootUI() == &performanceView;
	controlView.loading_song = loadSongUI.isLoadingSong();
	controlView.current_ui_is_song_or_arranger =
	    getCurrentUI() == static_cast<UI*>(&sessionView) || getCurrentUI() == static_cast<UI*>(&arrangerView);
	// `ui_mode` is left at `UiMode::None` for the same reason and with a stronger one behind it:
	// there is no total mapping from legacy's 47-value `currentUIMode` bitfield onto the port's
	// enum, and `GlobalControls`'s own doc says `MidiLearn` is deliberately not settable through
	// this struct because `effective_ui_mode()` takes that from `LearnMode::active()` instead
	// (`services/global_controls.h:266-269`). Every arm that reads it is inside the
	// `View::buttonAction` group, which `GlobalControls::handle` gates on
	// `stack_.input_top().is_root()` (`services/global_controls.cpp:580`) -- and the guest's only
	// tenant, `MenuScene`, is not a root scene. So no arm that reads `ui_mode` is reachable under
	// this plan. Uncovered, and named as uncovered.
	services.controls.set_view_state(controlView);

	// F1 task 2's setter, and the call migration §10.2 item 7 was opened for. Safe over a live set:
	// it re-seats `services_`, the `Router`'s copy and the deduplicated tick list together, so a
	// second call neither double-ticks nor strands a slot (`shell/shell.h`'s `set_services`).
	shellHostUI.shell().set_services(
	    {.mod_knobs = &services.knobs, .global_controls = &services.controls, .midi_learn = &services.learn});
}

namespace {

/// @brief Is the ported menu the scene that would take input right now?
///
/// The port of "`getCurrentUI() == &soundEditor`", which is what makes `getCurrentMenuItem()` total
/// in the two legacy bodies these two functions come from. `input_top()` and not `top()`, for the
/// reason `Router::run_chain` uses it: a popup, a console line or the working animation sits over
/// the menu without taking its input (`Scene::input_transparent`), and a MIDI-learn message during
/// a popup is still the menu's.
///
/// Identity against `menu_scene()` rather than `kind() == SceneKind::FirstApp`: the registry maps
/// three application kinds (`scenes/registry.h`) and only one of them is the menu, so comparing the
/// object is the question actually being asked.
bool menuIsFrontmost() {
	return &shellHostUI.shell().stack().input_top() == static_cast<deluge::ui::shell::Scene*>(&scenes::menu_scene());
}

} // namespace

bool shellGuestMenuLearnNoteOn(::MIDICable& cable, int32_t channel, int32_t note) {
	if (!menuIsFrontmost()) {
		return false;
	}
	return scenes::menu_scene().learn_note_on(cable, channel, note);
}

bool shellGuestMenuLearnProgramChange(::MIDICable& cable, int32_t channel, int32_t program) {
	// `sound_editor.cpp:1606`'s gate, both conjuncts.
	if (currentUIMode != UI_MODE_MIDI_LEARN || Buttons::isShiftButtonPressed()) {
		return false;
	}
	if (!menuIsFrontmost()) {
		return false;
	}
	// `:1590-1591`: the item's answer is DISCARDED and the hook reports true regardless, which is
	// what stops the dispatcher's `else` handing the same message to `view`. Legacy's asymmetry
	// with `noteOnReceivedForMidiLearn` one function below it, kept rather than smoothed.
	(void)scenes::menu_scene().learn_program_change(cable, channel, program);
	return true;
}

} // namespace deluge::gui::ui
