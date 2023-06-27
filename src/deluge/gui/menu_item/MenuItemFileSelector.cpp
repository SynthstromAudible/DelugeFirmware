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

#include <samplebrowser.h>
#include <sound.h>
#include "MenuItemFileSelector.h"
#include "definitions.h"
#include "uitimermanager.h"
#include "soundeditor.h"
#include "functions.h"
#include "numericdriver.h"
#include "View.h"
#include "KeyboardScreen.h"
#include "song.h"
#include "Clip.h"

MenuItemFileSelector fileSelectorMenu;

void MenuItemFileSelector::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.shouldGoUpOneLevelOnBegin = true;
	if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) keyboardScreen.exitAuditionMode();
	bool success = openUI(&sampleBrowser);
	if (!success) {
		//if (getCurrentUI() == &soundEditor) soundEditor.goUpOneLevel();
		uiTimerManager.unsetTimer(TIMER_SHORTCUT_BLINK);
	}
}
bool MenuItemFileSelector::isRelevant(Sound* sound, int whichThing) {
	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) return true;
	Source* source = &sound->sources[whichThing];

	if (source->oscType == OSC_TYPE_WAVETABLE) return (sound->getSynthMode() != SYNTH_MODE_FM);
	else return (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE && source->oscType == OSC_TYPE_SAMPLE);
}
int MenuItemFileSelector::checkPermissionToBeginSession(Sound* sound, int whichThing, MultiRange** currentRange) {

	if (currentSong->currentClip->type == CLIP_TYPE_AUDIO) return MENU_PERMISSION_YES;

	bool can =
	    (sound->getSynthMode() == SYNTH_MODE_SUBTRACTIVE
	     || (sound->getSynthMode() == SYNTH_MODE_RINGMOD && sound->sources[whichThing].oscType == OSC_TYPE_WAVETABLE));

	if (!can) {
		return MENU_PERMISSION_NO;
	}

	return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, false, currentRange);
}
