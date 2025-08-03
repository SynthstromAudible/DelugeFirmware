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
#include "model/clip/audio_clip.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::audio_clip {

MenuPermission SampleMarkerEditor::checkPermissionToBeginSession(ModControllableAudio* modControllable,
                                                                 int32_t whichThing, ::MultiRange** currentRange) {

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

bool SampleMarkerEditor::isRelevant(ModControllableAudio* modControllable, int32_t whichThing) {
	return getCurrentAudioClip()->sampleHolder.audioFile != nullptr;
}

void SampleMarkerEditor::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	using namespace hid::display;
	oled_canvas::Canvas& image = OLED::main;

	const int32_t lineX = startX + width - 6;
	for (int32_t y = startY + 2; y <= startY + height - 4; y += 2) {
		image.drawPixel(lineX, y);
	}

	const Icon& icon = OLED::loopPointIcon;
	image.drawIcon(icon, startX + 5, startY + 3, true);
}

void SampleMarkerEditor::getColumnLabel(StringBuf& label) {
	label.append(l10n::get(l10n::String::STRING_FOR_END_POINT));
	label.truncate(3);
}

} // namespace deluge::gui::menu_item::audio_clip
