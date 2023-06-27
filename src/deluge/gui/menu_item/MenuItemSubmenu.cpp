/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include <AudioEngine.h>
#include <InstrumentClip.h>
#include <sound.h>
#include <sounddrum.h>
#include "MenuItemSubmenu.h"
#include "numericdriver.h"
#include "soundeditor.h"
#include "song.h"
#include "instrument.h"

extern "C" {
#if HAVE_OLED
#include <oled_low_level.h>
#endif
}

void MenuItemSubmenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentSubmenuItem = items;
	soundEditor.menuCurrentScroll = 0;
	soundEditor.currentMultiRange = NULL;
	if (navigatedBackwardFrom) {
		while (*soundEditor.currentSubmenuItem != navigatedBackwardFrom) {
			if (!*soundEditor.currentSubmenuItem) { // If desired item not found
				soundEditor.currentSubmenuItem = items;
				break;
			}
			soundEditor.currentSubmenuItem++;
		}
	}
	while (!(*soundEditor.currentSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex)) {
		soundEditor.currentSubmenuItem++;
		if (!*soundEditor.currentSubmenuItem) { // Not sure we need this since we don't wrap submenu items?
			soundEditor.currentSubmenuItem = items;
		}
	}
#if !HAVE_OLED
	updateDisplay();
#endif
}

void MenuItemSubmenu::updateDisplay() {
#if HAVE_OLED
	renderUIsForOled();
#else
	(*soundEditor.currentSubmenuItem)->drawName();
#endif
}

#if HAVE_OLED
void MenuItemSubmenu::drawPixelsForOled() {
	char const* itemNames[OLED_MENU_NUM_OPTIONS_VISIBLE];
	for (int i = 0; i < OLED_MENU_NUM_OPTIONS_VISIBLE; i++) {
		itemNames[i] = NULL;
	}

	int selectedRow = soundEditor.menuCurrentScroll;
	itemNames[selectedRow] = (*soundEditor.currentSubmenuItem)->getName();

	MenuItem** thisSubmenuItem = soundEditor.currentSubmenuItem;
	for (int i = selectedRow + 1; i < OLED_MENU_NUM_OPTIONS_VISIBLE; i++) {
		do {
			thisSubmenuItem++;
			if (!*thisSubmenuItem) goto searchBack;
		} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex));

		itemNames[i] = (*thisSubmenuItem)->getName();
	}

searchBack:
	thisSubmenuItem = soundEditor.currentSubmenuItem;
	for (int i = selectedRow - 1; i >= 0; i--) {
		do {
			if (thisSubmenuItem == items) goto doneSearching;
			thisSubmenuItem--;
		} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex));

		itemNames[i] = (*thisSubmenuItem)->getName();
	}

doneSearching:
	drawItemsForOled(itemNames, selectedRow);
}
#endif

void MenuItemSubmenu::selectEncoderAction(int offset) {

	MenuItem** thisSubmenuItem = soundEditor.currentSubmenuItem;

	do {
		if (offset >= 0) {
			thisSubmenuItem++;
			if (!*thisSubmenuItem) {
#if HAVE_OLED
				return;
#else
				thisSubmenuItem = items;
#endif
			}
		}
		else {
			if (thisSubmenuItem == items) {
#if HAVE_OLED
				return;
#else
				while (*(thisSubmenuItem + 1)) {
					thisSubmenuItem++;
				}
#endif
			}
			else {
				thisSubmenuItem--;
			}
		}
	} while (!(*thisSubmenuItem)->isRelevant(soundEditor.currentSound, soundEditor.currentSourceIndex));

	soundEditor.currentSubmenuItem = thisSubmenuItem;

#if HAVE_OLED
	soundEditor.menuCurrentScroll += offset;
	if (soundEditor.menuCurrentScroll < 0) soundEditor.menuCurrentScroll = 0;
	else if (soundEditor.menuCurrentScroll > OLED_MENU_NUM_OPTIONS_VISIBLE - 1)
		soundEditor.menuCurrentScroll = OLED_MENU_NUM_OPTIONS_VISIBLE - 1;
#endif

	updateDisplay();
}

MenuItem* MenuItemSubmenu::selectButtonPress() {
	return *soundEditor.currentSubmenuItem;
}

void MenuItemSubmenu::unlearnAction() {
	if (soundEditor.getCurrentMenuItem() == this) (*soundEditor.currentSubmenuItem)->unlearnAction();
}

bool MenuItemSubmenu::allowsLearnMode() {
	if (soundEditor.getCurrentMenuItem() == this) return (*soundEditor.currentSubmenuItem)->allowsLearnMode();
	else return false;
}

void MenuItemSubmenu::learnKnob(MIDIDevice* fromDevice, int whichKnob, int modKnobMode, int midiChannel) {
	if (soundEditor.getCurrentMenuItem() == this)
		(*soundEditor.currentSubmenuItem)->learnKnob(fromDevice, whichKnob, modKnobMode, midiChannel);
}

bool MenuItemSubmenu::learnNoteOn(MIDIDevice* fromDevice, int channel, int noteCode) {
	if (soundEditor.getCurrentMenuItem() == this)
		return (*soundEditor.currentSubmenuItem)->learnNoteOn(fromDevice, channel, noteCode);
	else return false;
}

// -----------------------------------------------
void MenuItemSubmenuReferringToOneThing::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentSourceIndex = thingIndex;
	soundEditor.currentSource = &soundEditor.currentSound->sources[thingIndex];
	soundEditor.currentSampleControls = &soundEditor.currentSource->sampleControls;
	MenuItemSubmenu::beginSession(navigatedBackwardFrom);
}

// -----------------------------------------------
void MenuItemCompressorSubmenu::beginSession(MenuItem* navigatedBackwardFrom) {
	soundEditor.currentCompressor =
	    forReverbCompressor ? &AudioEngine::reverbCompressor : &soundEditor.currentSound->compressor;
	MenuItemSubmenu::beginSession(navigatedBackwardFrom);
}

// -----------------------------------------------
void MenuItemArpeggiatorSubmenu::beginSession(MenuItem* navigatedBackwardFrom) {

	soundEditor.currentArpSettings = soundEditor.editingKit()
	                                     ? &((SoundDrum*)soundEditor.currentSound)->arpSettings
	                                     : &((InstrumentClip*)currentSong->currentClip)->arpSettings;
	MenuItemSubmenu::beginSession(navigatedBackwardFrom);
}
