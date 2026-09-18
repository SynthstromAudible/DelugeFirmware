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

namespace {
/// "Create New Song": ends playback, tears down the old song, builds a blank one and puts the UI back on it.
///
/// Reads no card. It used to: `ensureAtLeastOneSessionClip()` walked the card for a preset and loaded it, and the
/// `loadAllSamples()` after it loaded whatever that preset brought. Phase 4b moved the first onto the interaction
/// tier -- the clip is built on the built-in default synth and `InstrumentLoad` acquires the preset afterwards --
/// which left the second with a song holding one unsampled synth and nothing to do. So this runs where its caller
/// stands, and the whole operation is over when it returns.
///
/// The UI-mode gate around it stays, and is still load-bearing: `nullifyUIs()` below drops `numUIsOpen` to 0, during
/// which `getCurrentUI()` falls back to `lastUIBeforeNullifying` -- the (now-stale) clearSong singleton (ui.cpp's
/// documented "ugly work-around to stop everything breaking"). buttons.cpp routes presses to
/// `getCurrentUI()->buttonAction()` unconditionally, so without the mode a BACK press reaching
/// `ContextMenu::buttonAction()` -> `close()` -> `closeUI(&clearSong)` would index `uiNavigationHierarchy[-2]`
/// (OOB/UB). Running synchronously leaves no window for such a press to arrive in, and the mode keeps the invariant
/// stated next to the code that nullifies the UIs.
void runAccept() {
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

	void* songMemory = deluge::memory::alloc_fast(sizeof(Song)); // TODO: error checking
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

	setUIForLoadedSong(currentSong);
	currentUIMode = UI_MODE_NONE;

	display->removeWorkingAnimation();
}
} // namespace

char const* ClearSong::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_CLEAR_SONG_QMARK);
}

std::span<char const*> ClearSong::getOptions() {
	using enum l10n::String;
	static char const* options[] = {l10n::get(STRING_FOR_OK)};
	return {options, 1};
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
	// Always true (never trigger ContextMenu::buttonAction()'s synchronous close()): runAccept() does the whole
	// sequence itself, including the UI teardown, and there is no failure branch to report -- the pre-existing code
	// discarded ensureAtLeastOneSessionClip()'s bool too.
	//
	// The gate closes around the call rather than inside it: `runAccept()` nullifies the UIs, and a press must not
	// reach ContextMenu::buttonAction() while that is true. See runAccept()'s comment.
	currentUIMode = UI_MODE_LOADING_SONG_ESSENTIAL_SAMPLES;
	runAccept();
	currentUIMode = UI_MODE_NONE;
	return true;
}
} // namespace deluge::gui::context_menu
