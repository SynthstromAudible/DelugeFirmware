

#include "gui/context_menu/launch_style.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "hid/display/display.h"
#include "model/clip/clip.h"
#include <cstddef>

namespace deluge::gui::context_menu {

constexpr size_t kNumValues = 3;

LaunchStyle launchStyle{};

char const* LaunchStyle::getTitle() {
	static char const* title = "Launch Style";
	return title;
}

Sized<char const**> LaunchStyle::getOptions() {
	using enum l10n::String;
	static const char* optionsls[] = {
	    l10n::get(STRING_FOR_DEFAULT_LAUNCH),
	    l10n::get(STRING_FOR_FILL_LAUNCH),
	    l10n::get(STRING_FOR_ONCE_LAUNCH),
	};
	return {optionsls, kNumValues};
}

bool LaunchStyle::setupAndCheckAvailability() {
	currentUIMode = UI_MODE_NONE;
	this->currentOption = static_cast<int32_t>(clip->launchStyle);

	if (display->haveOLED()) {
		scrollPos = this->currentOption;
	}

	return true;
}

void LaunchStyle::selectEncoderAction(int8_t offset) {
	ContextMenu::selectEncoderAction(offset);
	clip->launchStyle = static_cast<::LaunchStyle>(this->currentOption);
}

} // namespace deluge::gui::context_menu
