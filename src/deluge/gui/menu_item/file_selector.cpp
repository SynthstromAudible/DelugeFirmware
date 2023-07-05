/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "gui/ui/browser/sample_browser.h"
#include "processing/sound/sound.h"
#include "file_selector.h"
#include "definitions.h"
#include "gui/ui_timer_manager.h"
#include "gui/ui/sound_editor.h"
#include "util/functions.h"
#include "hid/display/numeric_driver.h"
#include "gui/views/view.h"
#include "gui/ui/keyboard_screen.h"
#include "model/song/song.h"
#include "model/clip/clip.h"

namespace menu_item {

FileSelector fileSelectorMenu{"File browser"};

void FileSelector::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.shouldGoUpOneLevelOnBegin = true;
	if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) {
		keyboardScreen.exitAuditionMode();
	}
	bool success = openUI(&sampleBrowser);
	if (!success) {
		//if (getCurrentUI() == &soundEditor) soundEditor.goUpOneLevel();
		uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
	}
}
bool FileSelector::isRelevant(Sound* sound, int whichThing) {
	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		return true;
	}
	Source* source = &sound->sources[whichThing];

	if (source->oscType == OSC_TYPE_WAVETABLE) {
		return (sound->getSynthMode() != SYNTH_MODE_FM);
	}

	return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE && source->oscType == OSC_TYPE_SAMPLE);
}
int FileSelector::checkPermissionToBeginSession(Sound* sound, int whichThing, ::MultiRange** currentRange) {

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) {
		return MENU_PERMISSION_YES;
	}

	bool can =
	    (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE
	     || (sound->getSynthMode() == SYNTH_MODE_RINGMOD && sound->sources[whichThing].oscType == OSC_TYPE_WAVETABLE));

	if (!can) {
		return MENU_PERMISSION_NO;
	}

	return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, false, currentRange);
}
} // namespace menu_item
