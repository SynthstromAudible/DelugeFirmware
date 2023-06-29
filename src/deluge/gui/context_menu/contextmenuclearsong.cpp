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

#include "processing/engines/audio_engine.h"
#include "storage/audio/audio_file_manager.h"
#include "gui/context_menu/contextmenuclearsong.h"
#include "modulation/params/param_manager.h"
#include "hid/display/numeric_driver.h"
#include "memory/general_memory_allocator.h"
#include "gui/views/view.h"
#include "playback/mode/session.h"
#include "playback/mode/arrangement.h"
#include "model/action/action_logger.h"
#include <new>
#include "model/song/song.h"
#include "hid/led/indicator_leds.h"
#include "extern.h"
#include "playback/playback_handler.h"
#include "hid/display/oled.h"

ContextMenuClearSong contextMenuClearSong{};

extern void setUIForLoadedSong(Song* song);
extern void deleteOldSongBeforeLoadingNew();

ContextMenuClearSong::ContextMenuClearSong() {
#if HAVE_OLED
	title = "Clear song?";
#endif
}

char const** ContextMenuClearSong::getOptions() {
#if HAVE_OLED
	static char const* options[] = {"Ok"};
#else
	static char const* options[] = {"New"};
#endif
	return options;
}

void ContextMenuClearSong::focusRegained() {
	ContextMenu::focusRegained();

	// TODO: Switch a bunch of LEDs off (?)

	IndicatorLEDs::setLedState(saveLedX, saveLedY, false);
	IndicatorLEDs::setLedState(synthLedX, synthLedY, false);
	IndicatorLEDs::setLedState(kitLedX, kitLedY, false);

	IndicatorLEDs::setLedState(crossScreenEditLedX, crossScreenEditLedY, false);
	IndicatorLEDs::setLedState(clipViewLedX, clipViewLedY, false);
	IndicatorLEDs::setLedState(sessionViewLedX, sessionViewLedY, false);
	IndicatorLEDs::setLedState(scaleModeLedX, scaleModeLedY, false);

	IndicatorLEDs::blinkLed(loadLedX, loadLedY);
	IndicatorLEDs::blinkLed(backLedX, backLedY);
}

bool ContextMenuClearSong::acceptCurrentOption() {
	if (playbackHandler.playbackState
	    && ((playbackHandler.playbackState & PLAYBACK_CLOCK_INTERNAL_ACTIVE) || currentPlaybackMode == &arrangement)) {

		playbackHandler.endPlayback();
	}

	actionLogger.deleteAllLogs();

	nullifyUIs();
	if (!(playbackHandler.playbackState & PLAYBACK_CLOCK_EITHER_ACTIVE)) {
		deleteOldSongBeforeLoadingNew();
	}
	else {
		AudioEngine::songSwapAboutToHappen();
	}

	void* songMemory = generalMemoryAllocator.alloc(sizeof(Song), NULL, false, true); // TODO: error checking
	preLoadedSong = new (songMemory) Song();
	preLoadedSong->paramManager.setupUnpatched(); // TODO: error checking
	GlobalEffectable::initParams(&preLoadedSong->paramManager);
	preLoadedSong->setupDefault();

	Song* toDelete = currentSong;

	preLoadedSong->ensureAtLeastOneSessionClip(); // Will load a synth preset from SD card

	playbackHandler.doSongSwap((playbackHandler.playbackState & PLAYBACK_CLOCK_EITHER_ACTIVE));
	if (toDelete) {
		void* toDealloc = dynamic_cast<void*>(toDelete);
		toDelete->~Song();
		generalMemoryAllocator.dealloc(toDealloc);
	}

	audioFileManager.deleteAnyTempRecordedSamplesFromMemory();

	// If for some reason the default synth preset included a sample which needs loading, and somehow there wasn't enough RAM to load it before, do it now.
	currentSong->loadAllSamples();

	setUIForLoadedSong(currentSong);
	currentUIMode = UI_MODE_NONE;

#if HAVE_OLED
	OLED::removeWorkingAnimation();
#endif

	return true;
}
