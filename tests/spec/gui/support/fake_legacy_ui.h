#pragma once
#include "gui/ui_timer_manager.h"

#include <cstdint>
#include <vector>

class MIDICable;

class UI;

/// @brief Test doubles for the parts of `gui/ui/ui.cpp` a legacy `UI` subclass needs to exist at
///        all.
///
/// The host spec suite deliberately never builds legacy's `deluge_app` closure (the note at the top
/// of `tests/spec/CMakeLists.txt`), and `gui/ui/ui.cpp` is squarely inside it: its five out-of-line
/// `UI` virtuals forward to `view`, `getRootUI()` and `renderUIsForOled()`, which between them drag
/// in the model, the playback engine and every legacy view. But a subclass's *vtable* names all
/// five, so nothing deriving from `UI` links without them.
///
/// So this file defines exactly those five plus `UI::UI` and `UI::close`, the same arrangement as
/// `gui/support/fake_legacy_hid.cpp` (the HID buffers) and `scenes/support/fake_shortcuts.cpp`
/// (the menu shortcut tables). Each one records that it was called and does nothing else, which is
/// what makes them observable: a spec can assert that `ShellHostUI::exitUI` closed the guest, which
/// is a fact about `closeUI(this)` and cannot be seen from inside the shell.
///
/// It also defines `deluge::scenes::scene_registry()`, for the same reason and not the same shape:
/// `shell_host_ui.cpp` defines the one `shellHostUI` global over the application's real scene table,
/// and that table odr-uses every ported scene -- the whole `deluge_scenes` closure this suite does
/// not build. The fake returns an empty registry, so the global exists and is inert; every example
/// builds its own `ShellHostUI` over its own table.
namespace fakelegacyui {

/// @name Recorders
/// @{
/// Every `closeUI(ui)` call, in order.
extern std::vector<UI*> closed;
/// How many times `renderUIsForOled()` was asked for a redraw.
extern int oled_redraws;
/// Every `UI::modEncoderAction` / `modButtonAction` / `modEncoderButtonAction` / `graphicsRoutine`
/// call, as a running count -- the four base implementations a guest must not silently inherit
/// without meaning to.
extern int mod_encoder_actions;
extern int mod_button_actions;
extern int mod_encoder_button_actions;
extern int graphics_routines;
/// @}

/// @brief One recorded `UITimerManager::setTimer` call.
struct TimerArm {
	TimerName which;
	int32_t ms;
};

/// @name The `uiTimerManager` recorder
///
/// F1 task 7 makes `ShellHostUI` a *client* of legacy's timer manager: `timerCallback` is the
/// shell's tick and `TimerName::UI_SPECIFIC` is what schedules it. The real manager
/// (`gui/ui_timer_manager.cpp`) converts milliseconds to audio samples against
/// `AudioEngine::audioSampleTimer` and dispatches through `getCurrentUI()`, neither of which this
/// suite has -- so the fake keeps only the two facts a guest spec is about: which timer was armed,
/// and for how long. `active` is modelled too, so `isTimerSet` answers the way legacy's does.
/// @{
/// Every `setTimer` call, in order. `setTimerSamples` is recorded here as well, in milliseconds.
extern std::vector<TimerArm> timer_arms;
/// Every `unsetTimer` call, in order.
extern std::vector<TimerName> timer_unsets;
/// @brief The `ms` of the last `TimerName::UI_SPECIFIC` arm, or -1 if it has never been armed or was
///        unset since.
int32_t ui_specific_armed_ms();
/// @}

/// @name The scheduler's two answers, made settable
///
/// `ShellHostUI`'s input hooks read exactly two things from outside the shell: the clock
/// (`getSystemTime()`) and whether the SD routine holds the lock (`isSDRoutineActive()`). Both are
/// declared in `OSLikeStuff/scheduler_api.h` and defined in the task scheduler, which this suite
/// does not build -- so they are defined here over these two variables, which is also what makes a
/// deferral example drivable at all.
/// @{
/// What `getSystemTime()` returns, in seconds. `nowMs()` is this times 1000.
extern double system_time_s;
/// What `isSDRoutineActive()` returns.
extern bool sd_routine_active;
/// @}

/// @name What the guest calls out to, on the two paths this suite cannot build
/// @{
/// How many times `indicator_leds::clearKnobIndicatorLevels()` was called -- the guest's close-path
/// hand-back of the two gold-knob rings.
extern int knob_indicator_clears;
/// Whether `closeUI` had already run when `clearKnobIndicatorLevels()` was called. Must be false:
/// the clear is only useful *before* the close, because `closeUI` runs the `focusRegained` that
/// re-fills the rings.
extern bool knob_clear_saw_close;
/// How many times `deluge::gui::ui::wireShellGuestServices()` was called. The real one lives in the
/// `deluge_scenes` closure this suite never builds; see its own header for why the seam is a free
/// function rather than three members.
extern int service_wirings;

/// @brief The arguments of the last `shellGuestMenuLearn*` call the guest made.
struct MidiLearnCall {
	const ::MIDICable* cable = nullptr;
	int32_t channel = 0;
	/// The note number, or the program number.
	int32_t data = 0;
	int32_t unused = 0;
	bool operator==(const MidiLearnCall&) const = default;
};
/// How many times `deluge::gui::ui::shellGuestMenuLearnNoteOn()` was called. Like
/// `wireShellGuestServices`, the real one lives in the `deluge_scenes` closure this suite never
/// builds.
extern int menu_learn_note_ons;
/// How many times `deluge::gui::ui::shellGuestMenuLearnProgramChange()` was called.
extern int menu_learn_program_changes;
/// The arguments of whichever of the two ran last.
extern MidiLearnCall last_menu_learn;
/// What both of them return -- the ported menu item's own answer, which the guest acts on.
extern bool menu_learn_claims;
/// @}

/// @brief Clear every recorder, and put the clock and the SD lock back to 0 / false.
void reset();

} // namespace fakelegacyui
