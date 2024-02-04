/*
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

#include "emulated_display.h"
#include "model/settings/runtime_feature_settings.h"

namespace deluge::gui::menu_item::runtime_feature {

void EmulatedDisplay::writeCurrentValue() {
	Setting::writeCurrentValue();

	bool is_swapped = (display->haveOLED() != hid::display::have_oled_screen);

	// unless we set it to "Toggleable", set to the expected display type at boot
	if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EmulatedDisplay)
	    == RuntimeFeatureStateEmulatedDisplay::Hardware) {
		if (is_swapped) {
			hid::display::swapDisplayType();
		}
	}
	else if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::EmulatedDisplay)
	         == RuntimeFeatureStateEmulatedDisplay::OnBoot) {
		if (!is_swapped) {
			hid::display::swapDisplayType();
		}
	}
}

} // namespace deluge::gui::menu_item::runtime_feature
