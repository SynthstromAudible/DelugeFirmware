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

#include "file_selector.h"
#include "definitions_cxx.hpp"
#include "gui/ui/browser/sample_browser.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "gui/views/view.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "util/functions.h"

namespace deluge::gui::menu_item {

FileSelector fileSelectorMenu{l10n::String::STRING_FOR_FILE_BROWSER};

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
bool FileSelector::isRelevant(Sound* sound, int32_t whichThing) {
	if (getCurrentClip()->type == ClipType::AUDIO) {
		return true;
	}
	Source* source = &sound->sources[whichThing];

	if (source->oscType == OscType::WAVETABLE) {
		return (sound->getSynthMode() != SynthMode::FM);
	}

	return (sound->getSynthMode() == SynthMode::SUBTRACTIVE && source->oscType == OscType::SAMPLE);
}
MenuPermission FileSelector::checkPermissionToBeginSession(Sound* sound, int32_t whichThing,
                                                           ::MultiRange** currentRange) {

	if (getCurrentClip()->type == ClipType::AUDIO) {
		return MenuPermission::YES;
	}

	bool can =
	    (sound->getSynthMode() == SynthMode::SUBTRACTIVE
	     || (sound->getSynthMode() == SynthMode::RINGMOD && sound->sources[whichThing].oscType == OscType::WAVETABLE));

	if (!can) {
		return MenuPermission::NO;
	}

	return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, whichThing, false, currentRange);
}
} // namespace deluge::gui::menu_item
