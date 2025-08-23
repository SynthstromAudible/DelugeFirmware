/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "loop_point.h"

#include "gui/menu_item/menu_item.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sample_marker_editor.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui_timer_manager.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file_holder.h"
#include "storage/multi_range/multi_range.h"
#include "utils.h"

#include <hid/display/oled.h>

namespace deluge::gui::menu_item::sample {

bool LoopPoint::isRelevant(ModControllableAudio* modControllable, int32_t) {
	return isSampleModeSample(modControllable, sourceId_);
}

MenuPermission LoopPoint::checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t,
                                                        MultiRange** currentRange) {

	if (!isRelevant(modControllable, sourceId_)) {
		return MenuPermission::NO;
	}

	const auto sound = static_cast<Sound*>(modControllable);

	MenuPermission permission =
	    soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, sourceId_, currentRange);

	// Before going ahead, make sure a Sample is loaded
	if (permission == MenuPermission::YES) {
		if (!(*currentRange)->getAudioFileHolder()->audioFile) {
			permission = MenuPermission::NO;
		}
	}

	return permission;
}

void LoopPoint::beginSession(MenuItem* navigatedBackwardFrom) {
	if (getRootUI() == &keyboardScreen) {
		if (currentUIMode == UI_MODE_AUDITIONING) {
			keyboardScreen.exitAuditionMode();
		}
	}

	soundEditor.shouldGoUpOneLevelOnBegin = true;
	soundEditor.setCurrentSource(sourceId_);
	sampleMarkerEditor.markerType = markerType;

	if (const bool success = openUI(&sampleMarkerEditor); !success) {
		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	}
}
void LoopPoint::renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) {
	using namespace hid::display;
	oled_canvas::Canvas& image = OLED::main;

	const bool isStartMarker = markerType == MarkerType::START;
	const int32_t lineX = isStartMarker ? startX + 8 : startX + width - 12;

	for (int32_t y = startY + 2; y <= startY + height - 4; y += 2) {
		image.drawPixel(lineX, y);
	}

	const Icon& icon = OLED::loopPointIcon;
	const int32_t iconX = isStartMarker ? lineX + 4 : startX - 2;
	const int32_t iconY = startY + 3;
	image.drawIcon(icon, iconX, iconY, !isStartMarker);
}

void LoopPoint::getColumnLabel(StringBuf& label) {
	if (markerType == MarkerType::START) {
		return label.append(l10n::get(l10n::String::STRING_FOR_START_POINT_SHORT));
	}

	label.append(getName());
	label.truncate(3);
}

} // namespace deluge::gui::menu_item::sample
