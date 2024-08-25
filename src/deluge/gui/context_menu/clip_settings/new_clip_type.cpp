

#include "gui/context_menu/clip_settings/new_clip_type.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/ui/ui.h"
#include "gui/views/session_view.h"
#include "hid/button.h"
#include "hid/display/display.h"
#include <cstddef>

namespace deluge::gui::context_menu::clip_settings {

constexpr size_t kNumValues = 5;

NewClipType newClipType{};

char const* NewClipType::getTitle() {
	static char const* title = "New Clip Type";
	return title;
}

Sized<char const**> NewClipType::getOptions() {
	using enum l10n::String;
	static const char* optionsls[] = {
	    "Audio", // audio
	    "Synth", // synth
	    "Kit",   // kit
	    "MIDI",  // midi
	    "CV",    // cv
	};
	return {optionsls, kNumValues};
}

bool NewClipType::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_CREATING_CLIP;

	updateSelectedOption();

	indicator_leds::blinkLed(IndicatorLED::BACK);

	if (display->haveOLED()) {
		scrollPos = currentOption;
	}

	return true;
}

void NewClipType::updateSelectedOption() {
	switch (toCreate) {
	case OutputType::AUDIO:
		currentOption = 0;
		break;
	case OutputType::SYNTH:
		currentOption = 1;
		break;
	case OutputType::KIT:
		currentOption = 2;
		break;
	case OutputType::MIDI_OUT:
		currentOption = 3;
		break;
	case OutputType::CV:
		currentOption = 4;
		break;
	default:
		break;
	}
	blinkLedForOption(currentOption);
}

void NewClipType::selectEncoderAction(int8_t offset) {
	int32_t optionSelectedBeforeEncoderAction = currentOption;
	ContextMenu::selectEncoderAction(offset);
	if (currentOption != optionSelectedBeforeEncoderAction) {
		disableLedForOption(optionSelectedBeforeEncoderAction);
		updateOutputToCreate();
		blinkLedForOption(currentOption);
	}
}

void NewClipType::updateOutputToCreate() {
	if (currentOption == 0) {
		toCreate = OutputType::AUDIO;
	}
	else if (currentOption == 1) {
		toCreate = OutputType::SYNTH;
	}
	else if (currentOption == 2) {
		toCreate = OutputType::KIT;
	}
	else if (currentOption == 3) {
		toCreate = OutputType::MIDI_OUT;
	}
	else if (currentOption == 4) {
		toCreate = OutputType::CV;
	}
}

bool NewClipType::acceptCurrentOption() {
	deluge::hid::Button b;
	using namespace deluge::hid::button;

	if (currentOption == 0) {
		b = CROSS_SCREEN_EDIT;
	}
	else if (currentOption == 1) {
		b = SYNTH;
	}
	else if (currentOption == 2) {
		b = KIT;
	}
	else if (currentOption == 3) {
		b = MIDI;
	}
	else if (currentOption == 4) {
		b = CV;
	}

	sessionView.clipCreationButtonPressed(b, 1, sdRoutineLock); // let the grid handle this

	return true;
}
ActionResult NewClipType::padAction(int32_t x, int32_t y, int32_t on) {
	ActionResult result = sessionView.padAction(x, y, on); // let the grid handle this

	display->setNextTransitionDirection(-1);
	close();

	return result;
}

ActionResult NewClipType::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (b == SELECT_ENC) {
		acceptCurrentOption();
	}
	else {
		sessionView.clipCreationButtonPressed(b, on, inCardRoutine); // let the grid handle this
	}

	display->setNextTransitionDirection(-1);
	close();

	return ActionResult::DEALT_WITH;
}

LED NewClipType::getLedFromOption(int32_t option) {
	if (option == 0) {
		return IndicatorLED::CROSS_SCREEN_EDIT;
	}
	else if (option == 1) {
		return IndicatorLED::SYNTH;
	}
	else if (option == 2) {
		return IndicatorLED::KIT;
	}
	else if (option == 3) {
		return IndicatorLED::MIDI;
	}
	else {
		return IndicatorLED::CV;
	}
}

void NewClipType::disableLedForOption(int32_t option) {
	indicator_leds::setLedState(getLedFromOption(option), false, false);
}

void NewClipType::blinkLedForOption(int32_t option) {
	indicator_leds::blinkLed(getLedFromOption(option));
}

} // namespace deluge::gui::context_menu::clip_settings
