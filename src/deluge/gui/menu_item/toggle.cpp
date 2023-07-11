#include "toggle.h"

namespace deluge::gui::menu_item {
Sized<char const**> Toggle::getOptions() {
	static char const* options[] = {"Off", "On"};
	return {options, 2};
}
} // namespace deluge::gui::menu_item