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

#include "multi_range.h"
#include "gui/l10n/l10n.h"
#include "gui/ui/keyboard/keyboard_screen.h"
#include "gui/ui/sound_editor.h"
#include "gui/views/instrument_clip_view.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h" //todo: this probably shouldn't be needed
#include "processing/engines/audio_engine.h"
#include "processing/sound/sound.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"
#include "util/functions.h"
#include <cstring>
#include <etl/vector.h>

namespace deluge::gui::menu_item {

MultiRange multiRangeMenu{};

void MultiRange::beginSession(MenuItem* navigatedBackwardFrom) {

	// If there's already a range (e.g. because we just came back out of a menu)...
	if (soundEditor.currentMultiRange != nullptr) {
		soundEditor.currentSource->defaultRangeI = soundEditor.currentMultiRangeIndex;
	}

	int32_t numRanges = soundEditor.currentSource->ranges.getNumElements();
	if (soundEditor.currentSource->defaultRangeI < 0
	    || soundEditor.currentSource->defaultRangeI >= numRanges) { // If default is invalid, work it out afresh
		soundEditor.currentSource->defaultRangeI = numRanges >> 1;
	}

	this->setValue(soundEditor.currentSource->defaultRangeI);
	soundEditor.currentSource->getOrCreateFirstRange(); // TODO: deal with error
	soundEditor.setCurrentMultiRange(this->getValue());

	if (display->haveOLED()) {
		currentScroll = this->getValue() - 1;
		if (currentScroll > this->getValue() - kOLEDMenuNumOptionsVisible + 1) {
			currentScroll = this->getValue() - kOLEDMenuNumOptionsVisible + 1;
		}
		if (currentScroll < 0) {
			currentScroll = 0;
		}
	}

	Range::beginSession(navigatedBackwardFrom);
}

void MultiRange::selectEncoderAction(int32_t offset) {

	if (display->hasPopup()) {
		return;
	}

	// If editing the range itself...
	if (soundEditor.editingColumn != RangeEdit::OFF) {

		// Editing left
		if (soundEditor.editingColumn == 1) {

			::MultiRange* lowerRange = soundEditor.currentSource->ranges.getElement(this->getValue() - 1);

			// Raising
			if (offset >= 0) {
				int32_t maximum;
				if (this->getValue() < soundEditor.currentSource->ranges.getNumElements() - 1) {
					::MultiRange* currentRange = soundEditor.currentSource->ranges.getElement(this->getValue());
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
				int32_t minimum;
				if (this->getValue() >= 2) {
					::MultiRange* lowerLowerRange = soundEditor.currentSource->ranges.getElement(this->getValue() - 2);
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
		else if (soundEditor.editingColumn == 2) {

			::MultiRange* currentRange = soundEditor.currentSource->ranges.getElement(this->getValue());

			// Raising
			if (offset >= 0) {
				int32_t maximum;
				if (this->getValue() < soundEditor.currentSource->ranges.getNumElements() - 2) {
					::MultiRange* higherRange = soundEditor.currentSource->ranges.getElement(this->getValue() + 1);
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
				int32_t minimum;
				if (this->getValue() >= 1) {
					::MultiRange* lowerRange = soundEditor.currentSource->ranges.getElement(this->getValue() - 1);
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

		// Editing sample pitch
		else {
			::MultiRange* range = soundEditor.currentSource->ranges.getElement(this->getValue());
			AudioFileHolder* holder = range->getAudioFileHolder();
			if (holder->audioFileType == AudioFileType::SAMPLE) {
				SampleHolderForVoice* sampleHolder = static_cast<SampleHolderForVoice*>(holder);
				offset = offset >= 1 ? 1 : -1;
				if (soundEditor.editingColumn == 3) {
					// transpose = 60 - note.
					// -> note 0 -> transpose 60
					// -> note 127 -> -67
					// ...and note going up means transpose going down.
					sampleHolder->transpose = std::clamp(sampleHolder->transpose - offset, -67L, 60L);
				}
				else if (soundEditor.editingColumn == 4) {
					int32_t cents = std::clamp(sampleHolder->getCents() + offset, -100L, 100L);
					if (cents > 50 && sampleHolder->transpose > -67) {
						sampleHolder->transpose -= 1;
						sampleHolder->setCents(cents - 100);
					}
					else if (cents < -50 && sampleHolder->transpose < 60) {
						sampleHolder->transpose += 1;
						sampleHolder->setCents(cents + 100);
					}
					else {
						sampleHolder->setCents(std::clamp(cents, -99L, 99L));
					}
				}
			}
		}

		drawValueForEditingRange(false);
	}

	// Or, normal mode
	else {

		// Inserting a range
		if (Buttons::isShiftButtonPressed()) {

			int32_t currentRangeBottom;
			if (this->getValue() == 0) {
				currentRangeBottom = soundEditor.currentSource->ranges.getElement(this->getValue())->topNote - 1;
				if (currentRangeBottom > 0) {
					currentRangeBottom = 0;
				}
			}
			else {
				currentRangeBottom = soundEditor.currentSource->ranges.getElement(this->getValue() - 1)->topNote + 1;
			}

			int32_t currentRangeTop;
			if (this->getValue() == soundEditor.currentSource->ranges.getNumElements() - 1) {
				currentRangeTop = currentRangeBottom + 1;
				if (currentRangeTop < 127) {
					currentRangeTop = 127;
				}
			}
			else {
				currentRangeTop = soundEditor.currentSource->ranges.getElement(this->getValue())->topNote;
			}

			if (currentRangeTop == currentRangeBottom) {
				display->displayPopup(l10n::get(l10n::String::STRING_FOR_RANGE_CONTAINS_ONE_NOTE));
				return;
			}

			int32_t midPoint = (currentRangeTop + currentRangeBottom) >> 1;

			int32_t newI = this->getValue();
			if (offset == 1) {
				newI++;
			}

			// Because range storage is about to change, must unassign all voices, and make sure no more can be assigned
			// during memory allocation
			soundEditor.currentSound->killAllVoices();
			AudioEngine::audioRoutineLocked = true;
			::MultiRange* newRange = soundEditor.currentSource->ranges.insertMultiRange(newI);
			AudioEngine::audioRoutineLocked = false;
			if (!newRange) {
				display->displayError(Error::INSUFFICIENT_RAM);
				return;
			}

			// Inserted after
			if (offset >= 0) {
				newRange->topNote = currentRangeTop;
				::MultiRange* oldRange = soundEditor.currentSource->ranges.getElement(this->getValue());
				oldRange->topNote = midPoint;
			}

			// Or if inserted before
			else {
				newRange->topNote = midPoint;
				// And can leave old range alone
				if (display->haveOLED()) {
					currentScroll++; // Won't go past end of list, cos list just grew.
				}
			}

			this->setValue(newI);
			if (display->haveOLED()) {
				display->consoleText(l10n::get(l10n::String::STRING_FOR_RANGE_INSERTED));
				if (currentScroll > this->getValue()) {
					currentScroll = this->getValue();
				}
				else if (currentScroll < this->getValue() - kOLEDMenuNumOptionsVisible + 1) {
					currentScroll = this->getValue() - kOLEDMenuNumOptionsVisible + 1;
				}
			}
			else {

				display->displayPopup(l10n::get(l10n::String::STRING_FOR_INSERT));
			}
		}

		// Or the normal thing of just flicking through existing ranges
		else {
			// Stay within bounds
			int32_t newValue = this->getValue() + offset;
			if (newValue < 0 || newValue >= soundEditor.currentSource->ranges.getNumElements()) {
				return;
			}

			this->setValue(newValue);
			soundEditor.currentSource->defaultRangeI = this->getValue();

			if (display->haveOLED()) {
				if (currentScroll > this->getValue()) {
					currentScroll = this->getValue();
				}
				else if (currentScroll < this->getValue() - kOLEDMenuNumOptionsVisible + 1) {
					currentScroll = this->getValue() - kOLEDMenuNumOptionsVisible + 1;
				}
			}
		}

		soundEditor.setCurrentMultiRange(this->getValue());
		soundEditor.possibleChangeToCurrentRangeDisplay();
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}
	RootUI* rootUI = getRootUI();
	if (rootUI == &keyboardScreen) {
		// refresh the keyboard grid to show the updated notes included in the multi range that has been edited /
		// selected
		uiNeedsRendering(rootUI, 0xFFFFFFFF, 0);
	}
}

void MultiRange::deletePress() {

	if (soundEditor.editingColumn != RangeEdit::OFF) {
		return;
	}
	if (display->hasPopup()) {
		return;
	}

	int32_t oldNum = soundEditor.currentSource->ranges.getNumElements();

	// Want to delete the current range
	if (oldNum <= 1) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_LAST_RANGE_CANT_DELETE));
		return;
	}

	::MultiRange* oldRange = soundEditor.currentSource->ranges.getElement(this->getValue());
	int32_t oldTopNote = oldRange->topNote;

	soundEditor.currentSound->deleteMultiRange(soundEditor.currentSourceIndex,
	                                           this->getValue()); // Unassigns all Voices

	// If bottom one, nothing to do
	if (this->getValue() == 0) {
		soundEditor.setCurrentMultiRange(this->getValue());
	}

	// Otherwise...
	else {

		this->setValue(this->getValue() - 1);
		soundEditor.setCurrentMultiRange(this->getValue());
		if (display->haveOLED()) {
			if (currentScroll > this->getValue()) {
				currentScroll = this->getValue();
			}
		}
		// If top one...
		if (this->getValue() == oldNum - 2) {
			soundEditor.currentMultiRange->topNote = 32767;
		}

		// If middle-ish one
		else {
			soundEditor.currentMultiRange->topNote = (soundEditor.currentMultiRange->topNote + oldTopNote) >> 1;
		}
	}

	display->displayPopup(l10n::get(l10n::String::STRING_FOR_RANGE_DELETED));
	soundEditor.possibleChangeToCurrentRangeDisplay();
	if (display->haveOLED()) {
		renderUIsForOled();
	}
	else {
		drawValue();
	}
}

// Layout:
//
// NOTE- NOTE (NOTE+00)
// 012345678901234567890
//
// NOTE can be upto 4 characters, eg. C#-2, but usually 2-3. We use this "usually"
// to get away with a bit less padding and fit more on the line.
//
const int32_t ColumnStart[] = {0, 6, 12, 16};
const int32_t ColumnEnd[] = {4, 10, 16, 19};
const uint8_t ROW_SIZE = 20;

void MultiRange::getText(char* buffer, int32_t* getLeftLength, int32_t* getRightLength, bool mayShowJustOne) {

	// Lower end
	if (this->getValue() == 0) {
		strcpy(buffer, l10n::get(l10n::String::STRING_FOR_BOTTOM));
		if (getLeftLength) {
			*getLeftLength = strlen(buffer);
		}
	}
	else {
		int32_t note = soundEditor.currentSource->ranges.getElement(this->getValue() - 1)->topNote + 1;
		noteCodeToString(note, buffer, getLeftLength);
	}
	char* bufferPos = buffer + strlen(buffer);

	// Pad
	if (display->haveOLED()) {
		while (bufferPos < &buffer[ColumnEnd[0]]) {
			*(bufferPos++) = ' ';
		}
	}
	*(bufferPos++) = '-';
	if (display->haveOLED()) {
		*(bufferPos++) = ' ';
	}

	// Upper end
	if (this->getValue() == soundEditor.currentSource->ranges.getNumElements() - 1) {
		char* p = bufferPos;
		strcpy(bufferPos, l10n::get(l10n::String::STRING_FOR_TOP));
		bufferPos = buffer + strlen(buffer);
		if (getRightLength) {
			*getRightLength = bufferPos - p;
		}
	}
	else {
		int32_t note = soundEditor.currentSource->ranges.getElement(this->getValue())->topNote;

		// FIXME: what, when, why?
		if (mayShowJustOne && this->getValue() > 0
		    && note == soundEditor.currentSource->ranges.getElement(this->getValue() - 1)->topNote + 1) {
			return;
		}

		noteCodeToString(note, bufferPos, getRightLength);
		bufferPos = buffer + strlen(buffer);
	}

	// Pad
	if (display->haveOLED()) {
		while (bufferPos <= &buffer[ColumnEnd[1]]) {
			*(bufferPos++) = ' ';
		}
	}
	*(bufferPos++) = '(';

	// Identify note ranges with wavetables with WT.
	::MultiRange* range = soundEditor.currentSource->ranges.getElement(this->getValue());
	AudioFileHolder* holder = range->getAudioFileHolder();
	if (holder->audioFileType == AudioFileType::WAVETABLE) {
		strcpy(bufferPos, "WT");
		bufferPos = buffer + strlen(buffer);
	}
	// Samples get =NOTE+CENTS
	else {
		SampleHolderForVoice* sampleHolder = static_cast<SampleHolderForVoice*>(holder);
		// transpose = 60 - midiNote <=> note = 60 - tranpose;
		noteCodeToString(60 - sampleHolder->transpose, bufferPos, nullptr, true);
		bufferPos = buffer + strlen(buffer);
		// Pad
		if (display->haveOLED()) {
			while (bufferPos < &buffer[ColumnEnd[2]]) {
				*(bufferPos++) = ' ';
			}
		}
		int32_t cents = sampleHolder->getCents();
		if (cents >= 0) {
			*(bufferPos++) = '+';
		}
		intToString(cents, bufferPos, 2);
		bufferPos = buffer + strlen(buffer);
		*(bufferPos++) = ')';
	}
	*(bufferPos++) = 0;
}

MenuItem* MultiRange::selectButtonPress() {
	return menuItemHeadingTo;
}

void MultiRange::noteOnToChangeRange(int32_t noteCode) {
	if (soundEditor.editingColumn == RangeEdit::OFF) {
		int32_t newI = soundEditor.currentSource->getRangeIndex(noteCode);
		if (newI != this->getValue()) {
			this->setValue(newI);
			soundEditor.setCurrentMultiRange(this->getValue());
			soundEditor.possibleChangeToCurrentRangeDisplay();
			if (display->haveOLED()) {
				if (currentScroll > this->getValue()) {
					currentScroll = this->getValue();
				}
				else if (currentScroll < this->getValue() - kOLEDMenuNumOptionsVisible + 1) {
					currentScroll = this->getValue() - kOLEDMenuNumOptionsVisible + 1;
				}

				renderUIsForOled();
			}
			else {
				drawValue();
			}
		}
	}
}

bool MultiRange::mayEditRangeEdge(int32_t col) {
	if (col == 1 && this->getValue() == 0) {
		return false; // BOTTOM
	}
	if (col == 2 && this->getValue() == soundEditor.currentSource->ranges.getNumElements() - 1) {
		return false; // TOP
	}
	if (col >= 3
	    && soundEditor.currentSource->ranges.getElement(this->getValue())->getAudioFileHolder()->audioFileType
	           == AudioFileType::WAVETABLE) {
		return false; // (WT)
	}
	return true;
}

ActionResult MultiRange::tryToggleColumn(int32_t col) {
	if (soundEditor.editingColumn == col) {
		cancelEditingIfItsOn();
	}
	else if (mayEditRangeEdge(col)) {
		editColumn(col);
	}
	return ActionResult::DEALT_WITH;
}

ActionResult MultiRange::buttonAction(deluge::hid::Button b, bool on, bool inCard) {
	(void)inCard;
	bool ok = on && display->haveOLED();
	if (ok && b == deluge::hid::button::SYNTH) {
		return tryToggleColumn(1);
	}
	else if (ok && b == deluge::hid::button::KIT) {
		return tryToggleColumn(2);
	}
	else if (ok && b == deluge::hid::button::MIDI) {
		return tryToggleColumn(3);
	}
	else if (ok && b == deluge::hid::button::CV) {
		return tryToggleColumn(4);
	}
	else {
		return ActionResult::NOT_DEALT_WITH;
	}
}

void MultiRange::drawPixelsForOled() {
	etl::vector<std::string_view, kOLEDMenuNumOptionsVisible> itemNames{};
	char nameBuffers[kOLEDMenuNumOptionsVisible][ROW_SIZE];
	int32_t actualCurrentRange = this->getValue();

	this->setValue(currentScroll);
	size_t idx = 0;
	for (idx = 0; idx < kOLEDMenuNumOptionsVisible; idx++) {
		if (this->getValue() >= soundEditor.currentSource->ranges.getNumElements()) {
			break;
		}
		getText(nameBuffers[idx], nullptr, nullptr, false);
		itemNames.push_back(nameBuffers[idx]);

		this->setValue(this->getValue() + 1);
	}
	this->setValue(actualCurrentRange);

	int32_t selectedOption = -1;
	if (soundEditor.editingColumn == RangeEdit::OFF) {
		selectedOption = this->getValue() - currentScroll;
	}
	drawItemsForOled(itemNames, selectedOption, 0);

	if (soundEditor.editingColumn != RangeEdit::OFF) {
		// Additional +1 because drawItemsForOled pads everything by one
		int32_t colStart = ColumnStart[soundEditor.editingColumn - 1];
		int32_t colEnd = ColumnEnd[soundEditor.editingColumn - 1];
		// colStart+1 because drawItemsForOled() pads by one
		int32_t highlightStartX = kTextSpacingX * (colStart + 1);
		int32_t highlightWidth = kTextSpacingX * (colEnd - colStart);

		int32_t baseY = (OLED_MAIN_HEIGHT_PIXELS == 64) ? 15 : 14;
		baseY += OLED_MAIN_TOPMOST_PIXEL;
		baseY += (this->getValue() - currentScroll) * kTextSpacingY;
		// -1 adjustment to invert the area 1px around the digits being rendered
		deluge::hid::display::OLED::main.invertArea(highlightStartX, highlightWidth, baseY, baseY + kTextSpacingY - 1);
	}
}
} // namespace deluge::gui::menu_item
