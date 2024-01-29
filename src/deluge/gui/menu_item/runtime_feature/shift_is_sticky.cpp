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

#include "shift_is_sticky.h"
#include "hid/buttons.h"
#include "model/settings/runtime_feature_settings.h"

#include <cstdint>

namespace deluge::gui::menu_item::runtime_feature {

void ShiftIsSticky::writeCurrentValue() {
	Setting::writeCurrentValue();

	if (runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky) == RuntimeFeatureStateToggle::Off) {
		Buttons::clearShiftSticky();
	}
	else {
		// Enable shift LED lighting when sticky keys gets enabled, so people can actually tell that their shift key is
		// down. People can still turn it off later if they want, but I think this is a good default.
		//
		// We can safely poke another setting here because exiting this menu is going to save the runtime feature
		// settings anyway.
		runtimeFeatureSettings.set(RuntimeFeatureSettingType::LightShiftLed, RuntimeFeatureStateToggle::On);
	}
}

} // namespace deluge::gui::menu_item::runtime_feature
