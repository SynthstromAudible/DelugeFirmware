

#include "gui/context_menu/clip_settings/new_clip_type.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "gui/views/session_view.h"
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
	currentUIMode = UI_MODE_NONE;

	this->currentOption = scrollPos = 0; // start at the top of the menu

	return true;
}

void NewClipType::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
}

bool NewClipType::acceptCurrentOption() {
	int32_t selection = this->currentOption;
	OutputType outputType = OutputType::NONE;
	if (selection == 0) {
		outputType = OutputType::AUDIO;
	}
	else if (selection == 1) {
		outputType = OutputType::SYNTH;
	}
	else if (selection == 2) {
		outputType = OutputType::KIT;
	}
	else if (selection == 3) {
		outputType = OutputType::MIDI_OUT;
	}
	else if (selection == 4) {
		outputType = OutputType::CV;
	}
	sessionView.newClipToCreate = sessionView.createNewClip(outputType, yDisplay);
	return false; // so you exit out of the context menu
}

ActionResult NewClipType::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;

	if (inCardRoutine) {
		return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
	}

	if (b == SYNTH) {
		if (on) {
			sessionView.newClipToCreate = sessionView.createNewClip(OutputType::SYNTH, yDisplay);
		}
	}

	else if (b == KIT) {
		if (on) {
			sessionView.newClipToCreate = sessionView.createNewClip(OutputType::KIT, yDisplay);
		}
	}

	else if (b == MIDI) {
		if (on) {
			sessionView.newClipToCreate = sessionView.createNewClip(OutputType::MIDI_OUT, yDisplay);
		}
	}

	else if (b == CV) {
		if (on) {
			sessionView.newClipToCreate = sessionView.createNewClip(OutputType::CV, yDisplay);
		}
	}

	else {
		return ContextMenu::buttonAction(b, on, inCardRoutine);
	}

	display->setNextTransitionDirection(-1);
	close();

	return ActionResult::DEALT_WITH;
}

} // namespace deluge::gui::context_menu::clip_settings
