/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
 *
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
#include "gui/menu_item/midi/macro_preset.h"
#include "gui/ui/load/load_macro_preset_ui.h"
#include "gui/ui/save/save_macro_preset_ui.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "io/midi/midi_macro.h"

namespace deluge::gui::menu_item::midi {

void MacroPreset::openBrowser() {
	MIDIMacro::presetMacroIndex = macro;
	if (isSave) {
		openUI(&saveMacroPresetUI);
	}
	else {
		openUI(&loadMacroPresetUI);
	}
}

void MacroPreset::beginSession(MenuItem* navigatedBackwardFrom) {
	// Pop back up to the parent macro menu (not this item) once the browser closes.
	soundEditor.shouldGoUpOneLevelOnBegin = true;
	openBrowser();
}

MenuItem* MacroPreset::selectButtonPress() {
	openBrowser();
	return NO_NAVIGATION;
}

} // namespace deluge::gui::menu_item::midi
