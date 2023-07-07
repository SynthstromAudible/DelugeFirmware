/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "processing/sound/sound.h"
#include "menu_item_with_cc_learning.h"
#include "gui/ui/sound_editor.h"
#include "hid/display.h"
#include "gui/views/view.h"
#include "model/song/song.h"

void MenuItemWithCCLearning::unlearnAction() {

	ParamDescriptor paramDescriptor = getLearningThing();

	// If it was a reasonable-ish request...
	if (!paramDescriptor.isNull()) {
		bool success = soundEditor.currentModControllable->unlearnKnobs(paramDescriptor, currentSong);

		if (success) {
			display.displayPopup("UNLEARNED");
			view.setKnobIndicatorLevels();
			soundEditor.markInstrumentAsEdited();
		}
	}
}

void MenuItemWithCCLearning::learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {
	ParamDescriptor paramDescriptor = getLearningThing();

	bool success = soundEditor.currentModControllable->learnKnob(fromDevice, paramDescriptor, whichKnob, modKnobMode,
	                                                             midiChannel, currentSong);

	if (success) {
		display.displayPopup("LEARNED");
		view.setKnobIndicatorLevels();
		soundEditor.markInstrumentAsEdited();
	}
}
