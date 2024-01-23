

#include "gui/context_menu/launch_style.h"
#include "extern.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/root_ui.h"
#include "hid/led/indicator_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "model/clip/clip.h"
#include <cstddef>

namespace deluge::gui::context_menu {

enum class LaunchStyle::Value {
	DEFAULT,
	FILL,
};
constexpr size_t kNumValues = 2;

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
	};
	return {optionsls, kNumValues};
}

bool LaunchStyle::setupAndCheckAvailability() {
	Value valueOption = Value::DEFAULT;

	switch (clip->launchStyle) {
	case ::LaunchStyle::DEFAULT:
		valueOption = Value::DEFAULT;
		break;
	case ::LaunchStyle::FILL:
		valueOption = Value::FILL;
		break;
	default:
		valueOption = Value::DEFAULT;
	}
	currentUIMode = UI_MODE_NONE;

	currentOption = static_cast<int32_t>(valueOption);

	if (display->haveOLED()) {
		scrollPos = currentOption;
	}

	return true;
}

void LaunchStyle::selectEncoderAction(int8_t offset) {
	/* if (currentUIMode) return; */

	ContextMenu::selectEncoderAction(offset);

	auto valueOption = static_cast<Value>(currentOption);

	switch (valueOption) {
	case Value::DEFAULT:
		clip->launchStyle = ::LaunchStyle::DEFAULT;
		break;
	case Value::FILL:
		clip->launchStyle = ::LaunchStyle::FILL;
		break;

	default:
		clip->launchStyle = ::LaunchStyle::DEFAULT;
	}
}

} // namespace deluge::gui::context_menu
