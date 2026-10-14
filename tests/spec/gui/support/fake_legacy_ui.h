#pragma once
#include "gui/ui/shell_note_mode.h" // NoteModeKind, ActionResult, hid::Button
#include "gui/ui_timer_manager.h"

#include <cstdint>
#include <functional>
#include <optional>
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

/// @name Legacy's own shift state, made settable
///
/// `ShellHostUI::claimsGridPad`'s `ShortcutsOnly` arm reads `Buttons::isShiftButtonPressed()`
/// directly, exactly as `SoundEditor::potentialShortcutPadAction` does
/// (`gui/ui/sound_editor.cpp:1169`) -- not `ctx.shift`, which is the shell's own latch and a
/// different fact. `hid/buttons.cpp`, where the real function reads a matrix-scan flag, is not in
/// this suite's link closure (only `hid/button.cpp`, singular, is), so it is defined here over one
/// settable bool, the same pattern as the scheduler's two answers above.
/// @{
/// What `Buttons::isShiftButtonPressed()` returns.
extern bool shift_button_pressed;
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

/// @brief How many times `deluge::gui::ui::dispatchShellActions()` was called.
///
/// Faked for the third time for the same reason as `wireShellGuestServices` and the two
/// `shellGuestMenuLearn*` hooks above: the real one reads `deluge::scenes::menu_scene()` and so
/// belongs to the `deluge_scenes` closure this suite never builds (`gui/ui/shell_action_dispatch.h`
/// says so in its own words). What that leaves assertable here is the half that lives in
/// `ShellHostUI::timerCallback` and cannot be seen from anywhere else -- **that the poll happens,
/// once per tick, on every path through the tick, and after `Shell::tick` has run**. That the poll
/// then empties `MenuScene`'s real outbox is `sim/scenes_spec/menu_actions_spec.cpp`'s, in the
/// binary where the real scene exists.
extern int action_dispatches;

/// @brief Called by the fake `dispatchShellActions()`, after it bumps `action_dispatches`.
///
/// The ordering questions this suite is about are all of the form "what had already happened when
/// the poll ran", and the answers live in the example's own rig -- how many events its scene had
/// been handed, whether `after_render` had run, whether the stack was still up. None of those is
/// visible from this file, so instead of guessing which of them to record, the fake calls back and
/// lets the example record whatever it is asking about. Empty by default; cleared by `reset()`.
extern std::function<void()> on_action_dispatch;

/// @brief How many times the fake `dispatchShellChoices()` has been called since `reset()`.
///
/// F3 task 1's sibling of `action_dispatches`, on identical terms: the real
/// `gui/ui/shell_choice_dispatch.cpp` names `deluge::scenes::browser::browser_scene()` and so
/// belongs to the `deluge_scenes` closure this suite never builds. What the drain then does with a
/// real outbox is `sim/scenes_spec/browser_choice_spec.cpp`'s; this counter exists so the guest
/// still links here, and so a later task can ask the one question that IS on this side -- whether
/// the second poll happens on the same paths the first does.
extern int choice_dispatches;

/// @brief How many times `deluge::gui::ui::forwardGridPad()` was called.
///
/// F4's sibling of `action_dispatches` and `choice_dispatches`, on the same terms: the real
/// `gui/ui/shell_pad_forward.cpp` names `keyboardScreen`, `instrumentClipView`, `performanceView`,
/// `automationView` and `deluge::scenes::menu_scene()`, none of which this suite builds. What that
/// leaves assertable here is the question that lives on `ShellHostUI`'s side of the seam --
/// whether a pad the scene did not claim reaches the forward at all, and with which arguments --
/// not which arm the real decision takes; that is `sim/scenes_spec/pad_forward_spec.cpp`'s, in the
/// binary where the real legacy views exist.
extern int pad_forwards;
/// @brief The arguments of the last `forwardGridPad()` call.
struct PadCall {
	int32_t x, y, velocity;
	bool operator==(const PadCall&) const = default;
};
extern PadCall last_pad_forward;
/// @}

/// @name `gui/ui/shell_note_mode.h`'s four guest-facing functions
///
/// The same terms as `forwardGridPad` above, with the difference the guest makes of the answer:
/// `ShellHostUI`'s three input hooks **act** on what these return -- a value is returned in place of
/// feeding the shell, `std::nullopt` feeds it -- so each fake answers a settable value rather than a
/// fixed one. The real functions read `deluge::scenes::menu_scene()` and call `instrumentClipView`,
/// neither of which this suite builds; which arm the real classification takes is
/// `sim/scenes_spec/note_mode_spec.cpp`'s. What is assertable here is the guest's side: that each hook
/// consults its function, with which arguments, and what it does with either answer.
/// @{
/// @brief The arguments of one `forwardNoteButton()` call.
struct NoteButtonCall {
	deluge::hid::Button b = 0;
	bool on = false;
	bool in_card_routine = false;
	bool operator==(const NoteButtonCall&) const = default;
};
/// How many times `forwardNoteButton()` was called.
extern int note_button_forwards;
/// The arguments of the last `forwardNoteButton()` call.
extern NoteButtonCall last_note_button;
/// What `forwardNoteButton()` answers. `std::nullopt` -- "not the note mode's" -- by default.
extern std::optional<ActionResult> note_button_answer;

/// How many times `forwardNoteHorizontalEncoder()` was called.
extern int note_horizontal_forwards;
/// The `offset` of the last `forwardNoteHorizontalEncoder()` call.
extern int32_t last_note_horizontal_offset;
/// What `forwardNoteHorizontalEncoder()` answers. `std::nullopt` by default.
extern std::optional<ActionResult> note_horizontal_answer;

/// @brief The arguments of one `forwardNoteVerticalEncoder()` call.
struct NoteVerticalCall {
	int32_t offset = 0;
	bool in_card_routine = false;
	bool operator==(const NoteVerticalCall&) const = default;
};
/// How many times `forwardNoteVerticalEncoder()` was called.
extern int note_vertical_forwards;
/// The arguments of the last `forwardNoteVerticalEncoder()` call.
extern NoteVerticalCall last_note_vertical;
/// What `forwardNoteVerticalEncoder()` answers. `std::nullopt` by default.
extern std::optional<ActionResult> note_vertical_answer;

/// How many times `exitNoteModeBeforeClose()` was called.
extern int note_mode_exits;
/// Whether `closeUI` had already run when `exitNoteModeBeforeClose()` was called. Must be false:
/// legacy's note arms run before `exitCompletely`'s `close()` (`gui/ui/sound_editor.cpp:749-761`).
extern bool note_exit_saw_close;
/// Whether `clearKnobIndicatorLevels()` had already run when `exitNoteModeBeforeClose()` was called.
/// Must be true: the exit goes below the existing indicator clear in `ShellHostUI::closeGuest`.
extern bool note_exit_saw_knob_clear;
/// @}

/// @brief Which `UI` this suite's `getCurrentUI()` answers -- i.e. **who owns
///        `TimerName::UI_SPECIFIC` right now**.
///
/// There is no `uiNavigationHierarchy` in this suite, which the timer-manager note above already
/// records as the reason `routine()` cannot dispatch. `ShellHostUI::rearmTimer` nevertheless has to
/// ask, because it refuses to arm `UI_SPECIFIC` for a UI that is no longer current
/// (`gui/ui/shell_host_ui.cpp:630`) -- the guard that stops an arm whose `openUI(...)` has just put
/// something else on top from handing this guest's tick to that something else.
///
/// So each `Rig` sets this to its own `guest`, and that is not a convenience: it is the precondition
/// under which the real `UITimerManager` would have called `timerCallback()` at all
/// (`gui/ui_timer_manager.cpp:142-148`). A spec driving `timerCallback()` by hand is standing in for
/// that dispatch, and saying so out loud is what makes the re-arm assertions mean what they claim.
///
/// @note **`reset()` deliberately does NOT clear this**, unlike every other field here. It describes
///       the fixture's topology rather than something an example recorded, and several examples call
///       `reset()` again mid-way to zero the counters -- clearing it there would silently disarm the
///       guest for the rest of the example. Each `Rig`'s constructor sets it and its destructor
///       clears it, so it never outlives the object it points at.
extern UI* current_ui;

/// @brief Clear every recorder, and put the clock and the SD lock back to 0 / false.
///
/// Everything except `current_ui` -- see its note.
void reset();

} // namespace fakelegacyui
