/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#pragma once
#include "gui/ui/audio_recorder.h"
#include "gui/menu_item/menu_item.h"
#include "hid/display/numeric_driver.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"

namespace menu_item::osc {
class AudioRecorder final : public MenuItem {
public:
	AudioRecorder(char const* newName = 0) : MenuItem(newName) {}
	void beginSession(MenuItem* navigatedBackwardFrom) {
		soundEditor.shouldGoUpOneLevelOnBegin = true;
		bool success = openUI(&audioRecorder);
		if (!success) {
			if (getCurrentUI() == &soundEditor) {
				soundEditor.goUpOneLevel();
			}
			uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
		}
		else {
			audioRecorder.process();
		}
	}
	bool isRelevant(Sound* sound, int whichThing) {
		Source* source = &sound->sources[whichThing];
		return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE);
	}

	int checkPermissionToBeginSession(Sound* sound, int whichThing, ::MultiRange** currentRange) {

		bool can = isRelevant(sound, whichThing);
		if (!can) {
			numericDriver.displayPopup(HAVE_OLED ? "Can't record audio into an FM synth" : "CANT");
			return false;
		}

		return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, false, currentRange);
	}
};
} // namespace menu_item::osc
