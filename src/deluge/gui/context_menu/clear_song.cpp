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

#include "gui/context_menu/clear_song.h"
#include "gui/l10n/l10n.h"
#include "gui/views/view.h"
#include "hid/display/display.h"
#include "hid/led/indicator_leds.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "model/song/song.h"
#include "modulation/params/param_manager.h"
#include "playback/mode/arrangement.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"

extern void setUIForLoadedSong(Song* song);
extern void deleteOldSongBeforeLoadingNew();
namespace deluge::gui::context_menu {
ClearSong clearSong{};

char const* ClearSong::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_CLEAR_SONG_QMARK);
}

std::span<char const*> ClearSong::getOptions() {
	using enum l10n::String;
	if (display->haveOLED()) {
		static char const* options[] = {l10n::get(STRING_FOR_OK)};
		return {options, 1};
	}
	else {
		static char const* options[] = {l10n::get(STRING_FOR_NEW)};
		return {options, 1};
	}
}

void ClearSong::focusRegained() {
	ContextMenu::focusRegained();

	// TODO: Switch a bunch of LEDs off (?)

	indicator_leds::setLedState(IndicatorLED::SAVE, false);
	indicator_leds::setLedState(IndicatorLED::SYNTH, false);
	indicator_leds::setLedState(IndicatorLED::KIT, false);

	indicator_leds::setLedState(IndicatorLED::CROSS_SCREEN_EDIT, false);
	indicator_leds::setLedState(IndicatorLED::CLIP_VIEW, false);
	indicator_leds::setLedState(IndicatorLED::SESSION_VIEW, false);
	indicator_leds::setLedState(IndicatorLED::SCALE_MODE, false);

	indicator_leds::blinkLed(IndicatorLED::LOAD);
	indicator_leds::blinkLed(IndicatorLED::BACK);
}

bool ClearSong::acceptCurrentOption() {
	if (playbackHandler.playbackState
	    && (playbackHandler.isInternalClockActive() || currentPlaybackMode == &arrangement)) {

		playbackHandler.endPlayback();
	}

	actionLogger.deleteAllLogs();

	nullifyUIs();
	if (!playbackHandler.isEitherClockActive()) {
		deleteOldSongBeforeLoadingNew();
	}
	else {
		AudioEngine::songSwapAboutToHappen();
	}

	void* songMemory = GeneralMemoryAllocator::get().allocMaxSpeed(sizeof(Song)); // TODO: error checking
	preLoadedSong = new (songMemory) Song();
	preLoadedSong->paramManager.setupUnpatched(); // TODO: error checking
	GlobalEffectable::initParams(&preLoadedSong->paramManager);
	preLoadedSong->setupDefault();

	Song* toDelete = currentSong;

	preLoadedSong->ensureAtLeastOneSessionClip(); // Will load a synth preset from SD card

	playbackHandler.doSongSwap(playbackHandler.isEitherClockActive());
	if (toDelete) {
		void* toDealloc = dynamic_cast<void*>(toDelete);
		toDelete->~Song();
		delugeDealloc(toDealloc);
	}

	audioFileManager.deleteAnyTempRecordedSamplesFromMemory();

	// If for some reason the default synth preset included a sample which needs loading, and somehow there wasn't
	// enough RAM to load it before, do it now.
	currentSong->loadAllSamples();

	setUIForLoadedSong(currentSong);
	currentUIMode = UI_MODE_NONE;

	display->removeWorkingAnimation();

	return true;
}
} // namespace deluge::gui::context_menu
