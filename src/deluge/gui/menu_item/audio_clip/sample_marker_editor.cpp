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

#include "sample_marker_editor.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "model/clip/audio_clip.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::audio_clip {

MenuPermission SampleMarkerEditor::checkPermissionToBeginSession(ModControllableAudio* modControllable,
                                                                 int32_t whichThing, MultiRange** currentRange) {

	if (!isRelevant(modControllable, whichThing)) {
		return MenuPermission::NO;
	}

	// Before going ahead, make sure a Sample is loaded
	if (getCurrentAudioClip()->sampleHolder.audioFile == nullptr) {
		return MenuPermission::NO;
	}

	return MenuPermission::YES;
}

void SampleMarkerEditor::beginSession(MenuItem* navigatedBackwardFrom) {

	soundEditor.shouldGoUpOneLevelOnBegin = true;
	sampleMarkerEditor.markerType = whichMarker;
	bool success = openUI(&sampleMarkerEditor); // Shouldn't be able to fail anymore
	if (!success) {
		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	}
}

} // namespace deluge::gui::menu_item::audio_clip
