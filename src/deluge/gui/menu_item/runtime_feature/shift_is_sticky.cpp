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
#include "gui/ui/shell_host_ui.h"
#include "hid/buttons.h"
#include "model/settings/runtime_feature_settings.h"

namespace deluge::gui::menu_item::runtime_feature {

void ShiftIsSticky::writeCurrentValue() {
	Setting::writeCurrentValue();

	const bool on =
	    runtimeFeatureSettings.get(RuntimeFeatureSettingType::ShiftIsSticky) == RuntimeFeatureStateToggle::On;

	// The shell guest keeps its own shift state machine and its own copy of this setting, because
	// nothing lets it read legacy's (`ShellHostUI`'s `@par Shift`). `ShellHostUI::opened()` seeds it
	// on the way in; this is the live half, for the setting being changed while a guest is already
	// open. `InputLowering::set_shift_is_sticky` also calls `clear_shift_sticky()` when switched off
	// (`shell/input_lowering.cpp:334-341`), which is the shell's own copy of the
	// `Buttons::clearShiftSticky()` below -- so the two machines are released together rather than
	// one of them keeping a latch nothing will clear.
	shellHostUI.shell().lowering().set_shift_is_sticky(on);

	if (!on) {
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
