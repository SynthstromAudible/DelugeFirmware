#include "fake_legacy_ui.h"

#include "definitions_cxx.hpp"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "memory/heaps.h"
#include "model/settings/runtime_feature_settings.h"
#include "scenes/registry.h"

namespace fakelegacyui {

std::vector<UI*> closed;
int oled_redraws = 0;
int mod_encoder_actions = 0;
int mod_button_actions = 0;
int mod_encoder_button_actions = 0;
int graphics_routines = 0;
double system_time_s = 0.0;
bool sd_routine_active = false;
int knob_indicator_clears = 0;
bool knob_clear_saw_close = false;
int service_wirings = 0;
int menu_learn_note_ons = 0;
int menu_learn_program_changes = 0;
MidiLearnCall last_menu_learn{};
bool menu_learn_claims = false;
int action_dispatches = 0;
std::function<void()> on_action_dispatch{};
int choice_dispatches = 0;
std::vector<TimerArm> timer_arms;
std::vector<TimerName> timer_unsets;

int32_t ui_specific_armed_ms() {
	if (!uiTimerManager.isTimerSet(TimerName::UI_SPECIFIC)) {
		return -1;
	}
	for (size_t i = timer_arms.size(); i-- > 0;) {
		if (timer_arms[i].which == TimerName::UI_SPECIFIC) {
			return timer_arms[i].ms;
		}
	}
	return -1;
}

UI* current_ui = nullptr;

void reset() {
	closed.clear();
	oled_redraws = 0;
	mod_encoder_actions = 0;
	mod_button_actions = 0;
	mod_encoder_button_actions = 0;
	graphics_routines = 0;
	system_time_s = 0.0;
	sd_routine_active = false;
	knob_indicator_clears = 0;
	knob_clear_saw_close = false;
	service_wirings = 0;
	menu_learn_note_ons = 0;
	menu_learn_program_changes = 0;
	last_menu_learn = MidiLearnCall{};
	menu_learn_claims = false;
	action_dispatches = 0;
	on_action_dispatch = nullptr;
	choice_dispatches = 0;
	timer_arms.clear();
	timer_unsets.clear();
	for (int32_t i = 0; i < util::to_underlying(TimerName::NUM_TIMERS); i++) {
		uiTimerManager.getTimer(static_cast<TimerName>(i)).active = false;
	}
	// `ShellHostUI::opened()` reads `ShiftIsSticky` out of the global below, and a spec that left it
	// set would change how a *later* example's shift tap behaves -- with the clock at 0, every tap
	// is shorter than `hold_time_ms` and would latch. `Rig` calls this in its constructor, so an
	// example that wants the feature on sets it after constructing its rig and before `open()`.
	runtimeFeatureSettings.set(RuntimeFeatureSettingType::ShiftIsSticky, RuntimeFeatureStateToggle::Off);
}

} // namespace fakelegacyui

// ---------------------------------------------------------------------------
// gui/ui_timer_manager.cpp's share: the one global and the five out-of-line
// members. The real file drags in `soundEditor`, `view`, every legacy view and
// `AudioEngine::audioSampleTimer`; what a guest spec needs is only that an arm
// is recorded with its duration and that `isTimerSet` answers honestly.
//
// `routine()` is deliberately a no-op rather than a reimplementation of
// `gui/ui_timer_manager.cpp:52-251`: dispatching would need `getCurrentUI()`,
// which needs the whole `uiNavigationHierarchy` this suite does not build. A
// spec that wants the tick chain calls `timerCallback()` itself, at the moment
// the recorded arm says the manager would have.
// ---------------------------------------------------------------------------

UITimerManager uiTimerManager{};

UITimerManager::UITimerManager() {
	timeNextEvent = 2147483647;
}

void UITimerManager::routine() {
}

void UITimerManager::setTimer(TimerName which, int32_t ms) {
	fakelegacyui::timer_arms.push_back(fakelegacyui::TimerArm{which, ms});
	getTimer(which).active = true;
}

void UITimerManager::setTimerSamples(TimerName which, int32_t samples) {
	// The real conversion, run backwards, so a spec reads one unit: `setTimer` is
	// `setTimerSamples(which, ms * 44)` (`gui/ui_timer_manager.cpp:253-255`).
	fakelegacyui::timer_arms.push_back(fakelegacyui::TimerArm{which, samples / 44});
	getTimer(which).active = true;
}

void UITimerManager::unsetTimer(TimerName which) {
	fakelegacyui::timer_unsets.push_back(which);
	getTimer(which).active = false;
}

bool UITimerManager::isTimerSet(TimerName which) {
	return getTimer(which).active;
}

void UITimerManager::workOutNextEventTime() {
}

// ---------------------------------------------------------------------------
// OSLikeStuff/scheduler_api.h's share: the two answers the guest's input hooks
// ask the scheduler for. `extern "C"` because that header declares them inside
// its own `extern "C"` block.
// ---------------------------------------------------------------------------

extern "C" double getSystemTime() {
	return fakelegacyui::system_time_s;
}

extern "C" bool isSDRoutineActive(void) {
	return fakelegacyui::sd_routine_active;
}

// ---------------------------------------------------------------------------
// gui/ui/ui.cpp's share: the seven non-inline members of `UI`, plus `closeUI`
// and `renderUIsForOled`, which two of them call.
// ---------------------------------------------------------------------------

// `UI::UI` (`gui/ui/ui.cpp:31-33`) sets exactly one thing, and it is the field legacy's OLED
// transparency actually lives in -- the one `canSeeViewUnderneath()` is NOT (see
// `ShellHostUI::canSeeViewUnderneath`). Reproduced rather than approximated so a spec can read it.
UI::UI() {
	oledShowsUIUnderneath = false;
}

// The three mod hooks forward to `view` in legacy (`gui/ui/ui.cpp:35-45`). `view` is the legacy
// `View` global; here they only record, which is enough for the one thing a guest spec asks of
// them -- that they still exist and that F1 task 5 did not override them.
void UI::modEncoderAction(int32_t whichModEncoder, int32_t offset) {
	(void)whichModEncoder;
	(void)offset;
	fakelegacyui::mod_encoder_actions++;
}

void UI::modButtonAction(uint8_t whichButton, bool on) {
	(void)whichButton;
	(void)on;
	fakelegacyui::mod_button_actions++;
}

void UI::modEncoderButtonAction(uint8_t whichModEncoder, bool on) {
	(void)whichModEncoder;
	(void)on;
	fakelegacyui::mod_encoder_button_actions++;
}

// `UI::graphicsRoutine` (`gui/ui/ui.cpp:47-51`) runs the ROOT view's per-frame animation when
// `canSeeViewUnderneath()` says the root is still visible. There is no root UI in this suite, so
// the fake records the call and evaluates the predicate the real one gates on -- which is the half
// a guest spec cares about.
void UI::graphicsRoutine() {
	fakelegacyui::graphics_routines++;
	(void)canSeeViewUnderneath();
}

void UI::displayOrLanguageChanged() {
	renderUIsForOled();
}

void UI::close() {
	closeUI(this);
}

void closeUI(UI* uiToClose) {
	fakelegacyui::closed.push_back(uiToClose);
}

// `getCurrentUI` (`gui/ui/ui.cpp:136-140`) reads `uiNavigationHierarchy`, which this suite does
// not build. `ShellHostUI::rearmTimer` asks it anyway -- it refuses to arm `TimerName::UI_SPECIFIC`
// for a UI that is no longer current -- so the fake answers whatever the rig declared. See
// `fakelegacyui::current_ui` for why that declaration is the honest model rather than a shim.
UI* getCurrentUI() {
	return fakelegacyui::current_ui;
}

void renderUIsForOled() {
	fakelegacyui::oled_redraws++;
}

// ---------------------------------------------------------------------------
// src/scenes/registry.cpp's share: an empty table, so the one global exists.
// ---------------------------------------------------------------------------
namespace deluge::scenes {

deluge::ui::shell::SceneRegistry scene_registry() {
	return deluge::ui::shell::SceneRegistry{std::span<deluge::ui::shell::Scene* const>{}};
}

} // namespace deluge::scenes

// ---------------------------------------------------------------------------
// model/settings/runtime_feature_settings.cpp's share: the one global, and the
// one out-of-line member. `ShellHostUI::opened()` reads `ShiftIsSticky` out of
// it to seed the shell's own copy of the setting, and `get()` is already inline
// in the header (`model/settings/runtime_feature_settings.h:100`), so all this
// suite needs is storage plus a constructor -- the real one reads a file.
// A spec sets the value through `set()`, which is inline too (`:108`).
// ---------------------------------------------------------------------------
RuntimeFeatureSettings runtimeFeatureSettings;

RuntimeFeatureSettings::RuntimeFeatureSettings() = default;

// Its two `deluge::vector` members pull in `external_allocator`, whose deallocate calls
// `deluge::memory::dealloc` (`memory/external_allocator.h:35`) -- named by the vectors' destructors
// whether or not they ever hold anything. The real pair is the GMA. Forwarding to the host
// allocator keeps them honest rather than aborting: an empty vector frees nothing, and a spec that
// somehow made one grow would still be correct instead of leaking.
namespace deluge::memory {
void* alloc_external(std::size_t size, std::size_t align) {
	return ::operator new(size, std::align_val_t{align});
}
void dealloc(void* ptr) {
	::operator delete(ptr);
}
} // namespace deluge::memory

// ---------------------------------------------------------------------------
// storage/flash_storage.cpp's share: the one setting `ShellHostUI::opened()`
// reads out of it. `FlashStorage::holdTime` is the tap threshold in AUDIO
// SAMPLES, which the guest converts to the milliseconds `LoweringConfig` wants;
// the real translation unit is the whole settings reader and writer, which this
// suite does not build. `fakelegacyui::reset()` puts it back to legacy's own
// default -- `defaultHoldTime (2) * kSampleRate / 20`
// (`storage/flash_storage.cpp:354-355`), i.e. 4410 samples and 100 ms.
// ---------------------------------------------------------------------------
namespace FlashStorage {
int32_t holdTime = (2 * kSampleRate) / 20;
} // namespace FlashStorage

// ---------------------------------------------------------------------------
// hid/led/indicator_leds.cpp's share of the guest's close path. The real file is
// the whole LED driver plus its blink timers; `clearKnobIndicatorLevels` is the
// one symbol `ShellHostUI::exitUI` names, and what a spec is about is that it
// was called, once, before `close()`.
// ---------------------------------------------------------------------------
namespace indicator_leds {
void clearKnobIndicatorLevels() {
	fakelegacyui::knob_indicator_clears++;
	// The **order** matters and is what this records: `closeUI` hands focus to the UI beneath on the
	// same call (`gui/ui/ui.cpp:199`) and that is what re-fills the rings, so a clear made after the
	// close blanks rings nothing then refreshes. True here means the close had already happened.
	fakelegacyui::knob_clear_saw_close = !fakelegacyui::closed.empty();
}
} // namespace indicator_leds

// ---------------------------------------------------------------------------
// gui/ui/shell_guest_services.cpp's share: the one function, recorded rather
// than performed. The real one constructs `services::ModKnobs`,
// `services::GlobalControls` and `services::MidiLearn` -- concrete classes over
// `model/` from the `deluge_scenes` closure this suite deliberately never builds
// (`tests/spec/CMakeLists.txt`'s note, and `gui/ui/shell_guest_services.h`'s own
// "why this is its own translation unit"). What a guest spec can say about it is
// that `opened()` calls it, which is what the counter is for; that the services
// it installs are the right ones is `sim/scenes_spec/boot_wiring_spec.cpp`'s,
// where the real closure exists.
// ---------------------------------------------------------------------------
namespace deluge::gui::ui {
void wireShellGuestServices() {
	fakelegacyui::service_wirings++;
}

// The other two symbols out of `gui/ui/shell_guest_services.h`, faked for the same reason and with
// one addition: these two have a return value the guest ACTS on, so the fake records the arguments
// and answers whatever `fakelegacyui::menu_learn_claims` is set to. The real ones ask
// `menu_scene().learn_note_on` / `learn_program_change`, which are `MenuScene`'s and are covered by
// `sim/scenes_spec`; what a guest spec can say is that the hook forwards the right three arguments
// and honours the answer.
bool shellGuestMenuLearnNoteOn(::MIDICable& cable, int32_t channel, int32_t note) {
	fakelegacyui::menu_learn_note_ons++;
	fakelegacyui::last_menu_learn = fakelegacyui::MidiLearnCall{&cable, channel, note, 0};
	return fakelegacyui::menu_learn_claims;
}

bool shellGuestMenuLearnProgramChange(::MIDICable& cable, int32_t channel, int32_t program) {
	fakelegacyui::menu_learn_program_changes++;
	fakelegacyui::last_menu_learn = fakelegacyui::MidiLearnCall{&cable, channel, program, 0};
	return fakelegacyui::menu_learn_claims;
}

// `gui/ui/shell_action_dispatch.cpp`'s share, on exactly the terms of the three above: the real one
// reads `deluge::scenes::menu_scene()` and its seven arms name `performanceView`, `audioClipView`,
// `stemExport`, two `gui/context_menu/` singletons, `soundEditor` and
// `hid::display::Screensaver` -- the `deluge_scenes` and legacy-view closures this suite never
// builds. The counter and the callback are what let
// `shell_action_dispatch_spec.cpp` ask the only questions that live on this side of that seam:
// whether `ShellHostUI::timerCallback` polls at all, how often, on which paths, and after what.
void dispatchShellActions() {
	fakelegacyui::action_dispatches++;
	if (fakelegacyui::on_action_dispatch) {
		fakelegacyui::on_action_dispatch();
	}
}

// `gui/ui/shell_choice_dispatch.cpp`'s share, on exactly the same terms: the real one reads
// `deluge::scenes::browser::browser_scene()` and its Sample arm performs a load that names
// `soundEditor`, `AudioEngine`, `slicer`, `PadLEDs` and `instrumentClipView`. No callback hook here
// because nothing in this suite asks an ordering question about it yet -- add one beside
// `on_action_dispatch` when something does.
void dispatchShellChoices() {
	fakelegacyui::choice_dispatches++;
}
} // namespace deluge::gui::ui
