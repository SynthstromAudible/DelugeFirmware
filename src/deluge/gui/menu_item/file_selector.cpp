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
#include "horizontal_menu.h"
#include "model/clip/audio_clip.h"
#include "model/clip/clip.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "storage/audio/audio_file.h"
#include "storage/multi_range/multi_range.h"

namespace deluge::gui::menu_item {

void FileSelector::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.shouldGoUpOneLevelOnBegin = true;
	soundEditor.setCurrentSource(sourceId_);

	if (getRootUI() == &keyboardScreen && currentUIMode == UI_MODE_AUDITIONING) {
		keyboardScreen.exitAuditionMode();
	}

	if (!openUI(&sampleBrowser)) {
		uiTimerManager.unsetTimer(TimerName::SHORTCUT_BLINK);
	}
}

MenuItem* FileSelector::selectButtonPress() {
	beginSession(nullptr);
	return NO_NAVIGATION;
}

bool FileSelector::isRelevant(ModControllableAudio* modControllable, int32_t whichThing) {
	if (getCurrentClip()->type == ClipType::AUDIO) {
		return true;
	}

	const auto sound = static_cast<Sound*>(modControllable);
	const Source& source = sound->sources[sourceId_];

	if (source.oscType == OscType::WAVETABLE) {
		return sound->getSynthMode() != SynthMode::FM;
	}

	return sound->getSynthMode() == SynthMode::SUBTRACTIVE && source.oscType == OscType::SAMPLE;
}
MenuPermission FileSelector::checkPermissionToBeginSession(ModControllableAudio* modControllable, int32_t whichThing,
                                                           ::MultiRange** currentRange) {

	if (getCurrentClip()->type == ClipType::AUDIO) {
		return MenuPermission::YES;
	}

	Sound* sound = static_cast<Sound*>(modControllable);

	bool can =
	    (sound->getSynthMode() == SynthMode::SUBTRACTIVE
	     || (sound->getSynthMode() == SynthMode::RINGMOD && sound->sources[sourceId_].oscType == OscType::WAVETABLE));

	if (!can) {
		return MenuPermission::NO;
	}

	return soundEditor.checkPermissionToBeginSessionForRangeSpecificParam(sound, sourceId_, currentRange);
}

void FileSelector::renderInHorizontalMenu(const SlotPosition& slot) {
	using namespace hid::display;
	OLED::main.drawIconCentered(OLED::folderIconBig, slot.start_x, slot.width, slot.start_y - 1);
}

void FileSelector::getColumnLabel(StringBuf& label) {
	if (const auto audioClip = getCurrentAudioClip(); audioClip != nullptr) {
		if (const auto audioFile = audioClip->sampleHolder.audioFile; audioFile != nullptr) {
			return label.append(getLastFolderFromPath(audioFile->filePath));
		}
		return MenuItem::getColumnLabel(label);
	}

	auto& source = soundEditor.currentSound->sources[sourceId_];
	if (!source.hasAtLeastOneAudioFileLoaded()) {
		return MenuItem::getColumnLabel(label);
	}

	if (source.ranges.getNumElements() > 1) {
		return label.append("Mult");
	}

	auto path = source.ranges.getElement(0)->getAudioFileHolder()->filePath;
	label.append(getLastFolderFromPath(path));
}

std::string FileSelector::getLastFolderFromPath(String& path) {
	const char* str = path.get();
	const char* lastSlash = strrchr(str, '/');
	if (!lastSlash || lastSlash == str)
		return "";

	const char* beforeFile = lastSlash;
	const char* prevSlash = beforeFile;
	while (prevSlash > str && *(prevSlash - 1) != '/') {
		--prevSlash;
	}

	int32_t folderLength = beforeFile - prevSlash;
	return std::string(prevSlash, folderLength);
}

} // namespace deluge::gui::menu_item
