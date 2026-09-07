#include "fake_legacy_ui.h"

#include "gui/ui/ui.h"
#include "scenes/registry.h"

namespace fakelegacyui {

std::vector<UI*> closed;
int oled_redraws = 0;
int mod_encoder_actions = 0;
int mod_button_actions = 0;
int mod_encoder_button_actions = 0;
int graphics_routines = 0;

void reset() {
	closed.clear();
	oled_redraws = 0;
	mod_encoder_actions = 0;
	mod_button_actions = 0;
	mod_encoder_button_actions = 0;
	graphics_routines = 0;
}

} // namespace fakelegacyui

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
