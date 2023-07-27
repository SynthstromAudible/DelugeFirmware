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
#include "gui/views/instrument_clip_view.h"
#include "multi_range.h"
#include "processing/sound/sound.h"
#include "gui/ui/sound_editor.h"
#include "hid/display.h"
#include "storage/multi_range/multisample_range.h"
#include "processing/source.h"
#include <string.h>
#include "util/functions.h"
#include "io/debug/print.h"
#include "hid/matrix/matrix_driver.h"
#include "gui/views/view.h"
#include "gui/ui/keyboard_screen.h"
#include "hid/buttons.h"
#include "storage/multi_range/multi_wave_table_range.h"

namespace menu_item {

MultiRange multiRangeMenu{};

void MultiRange::beginSession(MenuItem* navigatedBackwardFrom) {

	// If there's already a range (e.g. because we just came back out of a menu)...
	if (soundEditor.currentMultiRange) {
		soundEditor.currentSource->defaultRangeI = soundEditor.currentMultiRangeIndex;
	}

	int numRanges = soundEditor.currentSource->ranges.getNumElements();
	if (soundEditor.currentSource->defaultRangeI < 0
	    || soundEditor.currentSource->defaultRangeI >= numRanges) { // If default is invalid, work it out afresh
		soundEditor.currentSource->defaultRangeI = numRanges >> 1;
	}

	soundEditor.currentValue = soundEditor.currentSource->defaultRangeI;
	soundEditor.currentSource->getOrCreateFirstRange(); // TODO: deal with error
	soundEditor.setCurrentMultiRange(soundEditor.currentValue);

	if (display.type == DisplayType::OLED) {
		soundEditor.menuCurrentScroll = soundEditor.currentValue - 1;
		if (soundEditor.menuCurrentScroll > soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1) {
			soundEditor.menuCurrentScroll = soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;
		}
		if (soundEditor.menuCurrentScroll < 0) {
			soundEditor.menuCurrentScroll = 0;
		}
	}

	Range::beginSession(navigatedBackwardFrom);
}

void MultiRange::selectEncoderAction(int offset) {

	if (display.popupActive) {
		return;
	}

	// If editing the range itself...
	if (soundEditor.editingRangeEdge != RangeEdit::OFF) {

		// Editing left
		if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {

			::MultiRange* lowerRange = soundEditor.currentSource->ranges.getElement(soundEditor.currentValue - 1);

			// Raising
			if (offset >= 0) {
				int maximum;
				if (soundEditor.currentValue < soundEditor.currentSource->ranges.getNumElements() - 1) {
					::MultiRange* currentRange = soundEditor.currentSource->ranges.getElement(soundEditor.currentValue);
					maximum = currentRange->topNote - 1;
				}
				else {
					maximum = 127;
				}

				if (lowerRange->topNote < maximum) {
					lowerRange->topNote++;
				}
			}

			// Lowering
			else {
				int minimum;
				if (soundEditor.currentValue >= 2) {
					::MultiRange* lowerLowerRange =
					    soundEditor.currentSource->ranges.getElement(soundEditor.currentValue - 2);
					minimum = lowerLowerRange->topNote + 1;
				}
				else {
					minimum = 0;
				}

				if (lowerRange->topNote > minimum) {
					lowerRange->topNote--;
				}
			}
		}

		// Editing right
		else {

			::MultiRange* currentRange = soundEditor.currentSource->ranges.getElement(soundEditor.currentValue);

			// Raising
			if (offset >= 0) {
				int maximum;
				if (soundEditor.currentValue < soundEditor.currentSource->ranges.getNumElements() - 2) {
					::MultiRange* higherRange =
					    soundEditor.currentSource->ranges.getElement(soundEditor.currentValue + 1);
					maximum = higherRange->topNote - 1;
				}
				else {
					maximum = 126;
				}

				if (currentRange->topNote < maximum) {
					currentRange->topNote++;
				}
			}

			// Lowering
			else {
				int minimum;
				if (soundEditor.currentValue >= 1) {
					::MultiRange* lowerRange =
					    soundEditor.currentSource->ranges.getElement(soundEditor.currentValue - 1);
					minimum = lowerRange->topNote + 1;
				}
				else {
					minimum = 1;
				}

				if (currentRange->topNote > minimum) {
					currentRange->topNote--;
				}
			}
		}

		if (display.type == DisplayType::OLED) {
			renderUIsForOled();
		}
		else {
			drawValueForEditingRange(false);
		}
	}

	// Or, normal mode
	else {

		// Inserting a range
		if (Buttons::isShiftButtonPressed()) {

			int currentRangeBottom;
			if (soundEditor.currentValue == 0) {
				currentRangeBottom =
				    soundEditor.currentSource->ranges.getElement(soundEditor.currentValue)->topNote - 1;
				if (currentRangeBottom > 0) {
					currentRangeBottom = 0;
				}
			}
			else {
				currentRangeBottom =
				    soundEditor.currentSource->ranges.getElement(soundEditor.currentValue - 1)->topNote + 1;
			}

			int currentRangeTop;
			if (soundEditor.currentValue == soundEditor.currentSource->ranges.getNumElements() - 1) {
				currentRangeTop = currentRangeBottom + 1;
				if (currentRangeTop < 127) {
					currentRangeTop = 127;
				}
			}
			else {
				currentRangeTop = soundEditor.currentSource->ranges.getElement(soundEditor.currentValue)->topNote;
			}

			if (currentRangeTop == currentRangeBottom) {
				display.displayPopup(HAVE_OLED ? "Range contains only 1 note" : "CANT");
				return;
			}

			int midPoint = (currentRangeTop + currentRangeBottom) >> 1;

			int newI = soundEditor.currentValue;
			if (offset == 1) {
				newI++;
			}

			// Because range storage is about to change, must unassign all voices, and make sure no more can be assigned during memory allocation
			soundEditor.currentSound->unassignAllVoices();
			AudioEngine::audioRoutineLocked = true;
			::MultiRange* newRange = soundEditor.currentSource->ranges.insertMultiRange(newI);
			AudioEngine::audioRoutineLocked = false;
			if (!newRange) {
				display.displayError(ERROR_INSUFFICIENT_RAM);
				return;
			}

			// Inserted after
			if (offset >= 0) {
				newRange->topNote = currentRangeTop;
				::MultiRange* oldRange = soundEditor.currentSource->ranges.getElement(soundEditor.currentValue);
				oldRange->topNote = midPoint;
			}

			// Or if inserted before
			else {
				newRange->topNote = midPoint;
				// And can leave old range alone
				if (display.type == DisplayType::OLED) {
					soundEditor.menuCurrentScroll++; // Won't go past end of list, cos list just grew.
				}
			}

			soundEditor.currentValue = newI;
#if HAVE_OLED
			OLED::consoleText("Range inserted");
			if (soundEditor.menuCurrentScroll > soundEditor.currentValue) {
				soundEditor.menuCurrentScroll = soundEditor.currentValue;
			}
			else if (soundEditor.menuCurrentScroll < soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1) {
				soundEditor.menuCurrentScroll = soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;
			}
#else
			display.displayPopup("INSERT");
#endif
		}

		// Or the normal thing of just flicking through existing ranges
		else {
			// Stay within bounds
			int newValue = soundEditor.currentValue + offset;
			if (newValue < 0 || newValue >= soundEditor.currentSource->ranges.getNumElements()) {
				return;
			}

			soundEditor.currentValue = newValue;
			soundEditor.currentSource->defaultRangeI = soundEditor.currentValue;

#if HAVE_OLED
			if (soundEditor.menuCurrentScroll > soundEditor.currentValue) {
				soundEditor.menuCurrentScroll = soundEditor.currentValue;
			}
			else if (soundEditor.menuCurrentScroll < soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1) {
				soundEditor.menuCurrentScroll = soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;
			}
#endif
		}

		soundEditor.setCurrentMultiRange(soundEditor.currentValue);
		soundEditor.possibleChangeToCurrentRangeDisplay();
#if HAVE_OLED
		renderUIsForOled();
#else
		drawValue();
#endif
	}
}

void MultiRange::deletePress() {

	if (soundEditor.editingRangeEdge != RangeEdit::OFF) {
		return;
	}
	if (display.popupActive) {
		return;
	}

	int oldNum = soundEditor.currentSource->ranges.getNumElements();

	// Want to delete the current range
	if (oldNum <= 1) {
		display.displayPopup(HAVE_OLED ? "Only 1 range - can't delete" : "CANT");
		return;
	}

	::MultiRange* oldRange = soundEditor.currentSource->ranges.getElement(soundEditor.currentValue);
	int oldTopNote = oldRange->topNote;

	soundEditor.currentSound->deleteMultiRange(soundEditor.currentSourceIndex,
	                                           soundEditor.currentValue); // Unassigns all Voices

	// If bottom one, nothing to do
	if (soundEditor.currentValue == 0) {
		soundEditor.setCurrentMultiRange(soundEditor.currentValue);
	}

	// Otherwise...
	else {

		soundEditor.currentValue--;
		soundEditor.setCurrentMultiRange(soundEditor.currentValue);
#if HAVE_OLED
		if (soundEditor.menuCurrentScroll > soundEditor.currentValue) {
			soundEditor.menuCurrentScroll = soundEditor.currentValue;
		}
#endif
		// If top one...
		if (soundEditor.currentValue == oldNum - 2) {
			soundEditor.currentMultiRange->topNote = 32767;
		}

		// If middle-ish one
		else {
			soundEditor.currentMultiRange->topNote = (soundEditor.currentMultiRange->topNote + oldTopNote) >> 1;
		}
	}

	display.displayPopup(HAVE_OLED ? "Range deleted" : "DELETE");
	soundEditor.possibleChangeToCurrentRangeDisplay();
#if HAVE_OLED
	renderUIsForOled();
#else
	drawValue();
#endif
}

void MultiRange::getText(char* buffer, int* getLeftLength, int* getRightLength, bool mayShowJustOne) {

	// Lower end
	if (soundEditor.currentValue == 0) {
		strcpy(buffer, HAVE_OLED ? "Bottom" : "BOT");
		if (getLeftLength) {
			*getLeftLength = HAVE_OLED ? 6 : 3;
		}
	}
	else {
		int note = soundEditor.currentSource->ranges.getElement(soundEditor.currentValue - 1)->topNote + 1;
		noteCodeToString(note, buffer, getLeftLength);
	}

	char* bufferPos = buffer + strlen(buffer);

#if HAVE_OLED
	while (bufferPos < &buffer[7]) {
		*bufferPos = ' ';
		bufferPos++;
	}
#endif

	// Upper end
	if (soundEditor.currentValue == soundEditor.currentSource->ranges.getNumElements() - 1) {
		*(bufferPos++) = '-';
#if HAVE_OLED
		*(bufferPos++) = ' ';
#endif
		*(bufferPos++) = 't';
		*(bufferPos++) = 'o';
		*(bufferPos++) = 'p';
		*(bufferPos++) = 0;
		if (getRightLength) {
			*getRightLength = 3;
		}
	}
	else {
		int note = soundEditor.currentSource->ranges.getElement(soundEditor.currentValue)->topNote;

		if (mayShowJustOne && soundEditor.currentValue > 0
		    && note == soundEditor.currentSource->ranges.getElement(soundEditor.currentValue - 1)->topNote + 1) {
			return;
		}

		*(bufferPos++) = '-';
		*(bufferPos++) = ' ';
		noteCodeToString(note, bufferPos, getRightLength);
	}
}

MenuItem* MultiRange::selectButtonPress() {
	return menuItemHeadingTo;
}

void MultiRange::noteOnToChangeRange(int noteCode) {
	if (soundEditor.editingRangeEdge == RangeEdit::OFF) {
		int newI = soundEditor.currentSource->getRangeIndex(noteCode);
		if (newI != soundEditor.currentValue) {
			soundEditor.currentValue = newI;
			soundEditor.setCurrentMultiRange(soundEditor.currentValue);
			soundEditor.possibleChangeToCurrentRangeDisplay();
#if HAVE_OLED
			if (soundEditor.menuCurrentScroll > soundEditor.currentValue) {
				soundEditor.menuCurrentScroll = soundEditor.currentValue;
			}
			else if (soundEditor.menuCurrentScroll < soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1) {
				soundEditor.menuCurrentScroll = soundEditor.currentValue - OLED_MENU_NUM_OPTIONS_VISIBLE + 1;
			}

			renderUIsForOled();
#else
			drawValue();
#endif
		}
	}
}

bool MultiRange::mayEditRangeEdge(RangeEdit whichEdge) {
	if (whichEdge == RangeEdit::LEFT) {
		return (soundEditor.currentValue != 0);
	}
	return (soundEditor.currentValue != soundEditor.currentSource->ranges.getNumElements() - 1);
}

#if HAVE_OLED
void MultiRange::drawPixelsForOled() {

	char const* itemNames[OLED_MENU_NUM_OPTIONS_VISIBLE];
	char nameBuffers[OLED_MENU_NUM_OPTIONS_VISIBLE][20];
	int actualCurrentRange = soundEditor.currentValue;

	soundEditor.currentValue = soundEditor.menuCurrentScroll;
	int i = 0;
	while (i < OLED_MENU_NUM_OPTIONS_VISIBLE) {
		if (soundEditor.currentValue >= soundEditor.currentSource->ranges.getNumElements()) {
			break;
		}
		getText(nameBuffers[i], NULL, NULL, false);
		itemNames[i] = nameBuffers[i];

		i++;
		soundEditor.currentValue++;
	}

	while (i < OLED_MENU_NUM_OPTIONS_VISIBLE) {
		itemNames[i] = NULL;
		i++;
	}

	soundEditor.currentValue = actualCurrentRange;

	int selectedOption;
	if (soundEditor.editingRangeEdge != RangeEdit::OFF) {
		selectedOption = -1;
	}
	else {
		selectedOption = soundEditor.currentValue - soundEditor.menuCurrentScroll;
	}
	drawItemsForOled(itemNames, selectedOption);

	int hilightStartX, hilightWidth;

	if (soundEditor.editingRangeEdge == RangeEdit::LEFT) {
		hilightStartX = TEXT_SPACING_X;
		hilightWidth = TEXT_SPACING_X * 6;
doHilightJustOneEdge:
		int baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
		baseY += OLED_MAIN_TOPMOST_PIXEL;
		baseY += (soundEditor.currentValue - soundEditor.menuCurrentScroll) * TEXT_SPACING_Y;
		OLED::invertArea(hilightStartX, hilightWidth, baseY, baseY + TEXT_SPACING_Y, OLED::oledMainImage);
	}
	else if (soundEditor.editingRangeEdge == RangeEdit::RIGHT) {
		hilightStartX = TEXT_SPACING_X * 10;
		hilightWidth = OLED_MAIN_WIDTH_PIXELS - hilightStartX;
		goto doHilightJustOneEdge;
	}
}
#endif
} // namespace menu_item
