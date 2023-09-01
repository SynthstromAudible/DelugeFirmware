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

#include "hid/display/seven_segment.h"
#include "definitions_cxx.hpp"
#include "drivers/pic/pic.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/numeric_layer/numeric_layer_basic_text.h"
#include "hid/display/numeric_layer/numeric_layer_loading_animation.h"
#include "hid/display/numeric_layer/numeric_layer_scroll_transition.h"
#include "hid/display/numeric_layer/numeric_layer_scrolling_text.h"
#include "hid/hid_sysex.h"
#include "hid/led/indicator_leds.h"
#include "io/debug/print.h"
#include "memory/general_memory_allocator.h"
#include "model/action/action_logger.h"
#include "util/functions.h"
#include <cstdint>
#include <cstring>
#include <new>
#include <string_view>

extern "C" {
#include "RZA1/uart/sio_char.h"
#include "util/cfunctions.h"
}

namespace deluge::hid::display {
uint8_t numberSegments[10] = {0x7E, 0x30, 0x6D, 0x79, 0x33, 0x5B, 0x5F, 0x70, 0x7F, 0x7B};

uint8_t letterSegments[26] = {0x77, 0x1F, 0x4E, 0x3D, 0x4F,
                              0x47, //F
                              0x5E,
                              0x37, //H
                              0x04,
                              0x38, //J
                              0x57, //0x2F,
                              0x0E, //L
                              0x55,
                              0x15, //N
                              0x1D, 0x67,
                              0x73, //Q
                              0x05, 0x5B,
                              0x0F, //T
                              0x3E, 0x27, 0x5C, 0x49, 0x3B, 0x6D};

void SevenSegment::setTopLayer(NumericLayer* newTopLayer) {
	newTopLayer->next = topLayer;
	topLayer = newTopLayer;

	if (!popupActive) {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
		topLayer->isNowOnTop();
		render();
	}
}

void SevenSegment::deleteAllLayers() {
	while (topLayer) {
		NumericLayer* toDelete = topLayer;
		topLayer = topLayer->next;
		toDelete->~NumericLayer();
		GeneralMemoryAllocator::get().dealloc(toDelete);
	}
}

void SevenSegment::removeTopLayer() {
	if (!topLayer || !topLayer->next) {
		return;
	}

	NumericLayer* toDelete = topLayer;
	topLayer = topLayer->next;

	toDelete->~NumericLayer();
	GeneralMemoryAllocator::get().dealloc(toDelete);

	if (!popupActive) {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
		topLayer->isNowOnTop();
		render();
	}
}

void SevenSegment::setText(std::string_view newText, bool alignRight, uint8_t drawDot, bool doBlink,
                           uint8_t* newBlinkMask, bool blinkImmediately, bool shouldBlinkFast, int32_t scrollPos,
                           uint8_t* encodedAddition, bool justReplaceBottomLayer) {
	void* layerSpace = GeneralMemoryAllocator::get().alloc(sizeof(NumericLayerBasicText));
	if (!layerSpace) {
		return;
	}
	NumericLayerBasicText* newLayer = new (layerSpace) NumericLayerBasicText();

	encodeText(newText, newLayer->segments, alignRight, drawDot, true, scrollPos);

	if (encodedAddition) {
		for (int32_t i = 0; i < kNumericDisplayLength; i++) {
			newLayer->segments[i] |= encodedAddition[i];
		}
	}

	newLayer->blinkCount = -1;
	newLayer->currentlyBlanked = blinkImmediately;

	if (!doBlink) {
		newLayer->blinkSpeed = 0;
	}
	else {
		if (newBlinkMask) {
			for (int32_t i = 0; i < kNumericDisplayLength; i++) {
				newLayer->blinkedSegments[i] = newLayer->segments[i] & newBlinkMask[i];
			}
		}
		else {
			memset(newLayer->blinkedSegments, 0, kNumericDisplayLength);
		}

		if (!shouldBlinkFast) {
			newLayer->blinkSpeed = 1;
		}
		else {
			newLayer->blinkSpeed = 2;
		}
	}

	if (justReplaceBottomLayer) {
		replaceBottomLayer(newLayer);
	}
	else {
		transitionToNewLayer(newLayer);
	}
}

NumericLayerScrollingText* SevenSegment::setScrollingText(char const* newText, int32_t startAtTextPos,
                                                          int32_t initialDelay) {
	void* layerSpace = GeneralMemoryAllocator::get().alloc(sizeof(NumericLayerScrollingText));
	if (!layerSpace) {
		return NULL;
	}
	NumericLayerScrollingText* newLayer = new (layerSpace) NumericLayerScrollingText();

	newLayer->length = encodeText(newText, newLayer->text, false, 255, false);

	bool andAHalf;
	int32_t startAtEncodedPos = getEncodedPosFromLeft(startAtTextPos, newText, &andAHalf);

	startAtEncodedPos = std::min(startAtEncodedPos, (int32_t)newLayer->length - 4);
	startAtEncodedPos = std::max(startAtEncodedPos, 0_i32);

	newLayer->currentPos = startAtEncodedPos;
	newLayer->initialDelay = initialDelay;

	transitionToNewLayer(newLayer);

	return newLayer;
}

void SevenSegment::replaceBottomLayer(NumericLayer* newLayer) {
	NumericLayer** prevPointer = &topLayer;
	while ((*prevPointer)->next) {
		prevPointer = &(*prevPointer)->next;
	}

	NumericLayer* toDelete = *prevPointer;
	*prevPointer = newLayer;
	toDelete->~NumericLayer();
	GeneralMemoryAllocator::get().dealloc(toDelete);

	if (!popupActive && topLayer == newLayer) {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
		topLayer->isNowOnTop();
	}

	render();
}

void SevenSegment::transitionToNewLayer(NumericLayer* newLayer) {

	NumericLayerScrollTransition* scrollTransition = NULL;

	// If transition...
	if (!popupActive && nextTransitionDirection != 0 && topLayer != NULL) {

		void* layerSpace = GeneralMemoryAllocator::get().alloc(sizeof(NumericLayerScrollTransition));

		if (layerSpace) {
			scrollTransition = new (layerSpace) NumericLayerScrollTransition();
			scrollTransition->transitionDirection = nextTransitionDirection;

			scrollTransition->transitionProgress = -kNumericDisplayLength * scrollTransition->transitionDirection;

			topLayer->renderWithoutBlink(scrollTransition->segments);
		}
	}

	// Delete the old layers
	deleteAllLayers();

	// And if doing a transition, put that on top
	if (scrollTransition) {
		topLayer = newLayer;
		setTopLayer(scrollTransition);
	}
	else {
		setTopLayer(newLayer);
	}
	nextTransitionDirection = 0;
}

// Automatically stops at end of string
int32_t SevenSegment::getEncodedPosFromLeft(int32_t textPos, char const* text, bool* andAHalf) {

	int32_t encodedPos = 0;
	bool lastSegmentHasDot =
	    true; // Pretend this initially, cos the segment before the first one doesn't exist, obviously
	*andAHalf = false;

	for (int32_t i = 0; true; i++) {
		char thisChar = *text;
		if (!thisChar) {
			break; // Stop at end of string
		}
		bool isDot = (thisChar == '.' || thisChar == '#' || thisChar == ',');

		// If dot here, and we haven't just had a dot previously, then this dot just gets crammed into prev encoded char
		if (isDot && !lastSegmentHasDot) {
			lastSegmentHasDot = true;
			*andAHalf = true;
			encodedPos--;
		}
		else {
			lastSegmentHasDot = (isDot || thisChar == '!');
			*andAHalf = false;
		}

		if (i == textPos) {
			break;
		}

		text++;
		encodedPos++;
	}

	return encodedPos;
}

// Returns encoded length
// scrollPos may only be set when aligning left
int32_t SevenSegment::encodeText(std::string_view newText, uint8_t* destination, bool alignRight, uint8_t drawDot,
                                 bool limitToDisplayLength, int32_t scrollPos) {

	int32_t writePos;
	int32_t readPos;

	if (!alignRight) {
		readPos = 0;
		writePos = -scrollPos;
	}
	else {
		writePos = kNumericDisplayLength - 1;
		readPos = newText.length() - 1;
	}

	bool carryingDot = false;
	uint8_t prevSegment;

	while (true) {
		unsigned char thisChar = newText[readPos];
		uint8_t* segments = &destination[std::max(writePos, 0_i32)];

		// First, check if it's a dot, which we might want to add to a previous position
		bool isDot = (thisChar == '.' || thisChar == '#' || thisChar == ',');

		if (isDot) {
			if (alignRight) {

				// If already carrying a dot, we'd better just insert that old one
				if (carryingDot) {
					*segments = 0b10000000;
					if (!writePos) {
						writePos = -1;
						break;
					}
				}
				else {
					carryingDot = true;
					writePos++;
					segments++;
				}
			}

			else {
				// If we're not the first character, and the previous character didn't already have its dot illuminated, we'll just illuminate it
				if (writePos != -scrollPos && !(prevSegment & 0b10000000)) {
					writePos--;
					if (writePos >= 0) {
						segments--;
					}
					*segments = prevSegment | 0b10000000;
				}

				// Otherwise, we'll be our own new character
				else {
					*segments = 0b10000000;
				}
			}
		}
		else {

			// Now that we've checked the dot, we can see if we need to stop
			if (alignRight) {
				if (readPos < 0 || writePos < 0) {
					break;
				}
			}
			else {
				if (!newText[readPos] || (limitToDisplayLength && writePos >= kNumericDisplayLength)) {
					break;
				}
			}

			switch (thisChar) {
			case 'A' ... 'Z':
				*segments = letterSegments[thisChar - 'A']; // Letters
				break;

			case 'a' ... 'z':
				*segments = letterSegments[thisChar - 'a']; // Letters
				break;

			case '0' ... '9':
				*segments = numberSegments[thisChar - 48]; // Numbers
				break;

			case '-':
				*segments = 0x01;
				break;

			case '_':
				*segments = 0x08;
				break;

			case '\'':
				*segments = 0b00000010;
				break;

			case '!':
				*segments = 0b10100000;
				break;

			case '^':
				*segments = 0b01100010;
				break;

			default:
				*segments = 0;
			}

			// If we need to add a dot from before...
			if (alignRight && carryingDot) {

				// If this char already has some form of dot, we need to instead insert the carried dot to its right
				if (*segments & 0b10000000) {
					uint8_t newSegments = *segments;
					*segments = 0b10000000;

					writePos--;
					segments--;
					if (writePos < 0) {
						break; // If we've hit the left, get out
					}

					*segments = newSegments;
				}

				else {
					*segments |= 0b10000000;
				}
				carryingDot = false;
			}
		}

		prevSegment = *segments;

		if (alignRight) {
			readPos--;
			writePos--;
		}
		else {
			readPos++;
			writePos++;
		}
	}

	if (limitToDisplayLength) {
		// Fill whitespace
		if (alignRight) {
			while (writePos >= 0) {
				destination[writePos] = 0;
				writePos--;
			}
		}
		else {
			while (writePos < kNumericDisplayLength) {
				destination[writePos] = 0;
				writePos++;
			}
		}

		if (drawDot < kNumericDisplayLength) {
			destination[drawDot] |= 128;
		}
		else if ((drawDot & 0b11110000) == 0b10000000) {
			for (int32_t i = 0; i < kNumericDisplayLength; i++) {
				if ((drawDot >> (kNumericDisplayLength - 1 - i)) & 1) {
					destination[i] |= 128;
				}
			}
		}
	}
	return writePos;
}

void SevenSegment::setTextAsNumber(int16_t number, uint8_t drawDot, bool doBlink) {
	char text[12];
	intToString(number, text, 1);

	setText(text, true, drawDot, doBlink);
}

void SevenSegment::setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink,
                                 int32_t blinkPos, bool blinkImmediately) {
	char text[12];

	//int32_t minNumDigits = std::max(1, blinkPos + 1);
	int32_t minNumDigits = (blinkPos == -1) ? -1 : 3;

	slotToString(currentSlot, currentSubSlot, text, minNumDigits);

	uint8_t blinkMask[kNumericDisplayLength];
	if (blinkPos == -1) {
		memset(&blinkMask, 0, kNumericDisplayLength);
	}
	else {
		blinkPos++; // Move an extra space left if we have a sub-slot / letter suffix
		memset(&blinkMask, 255, kNumericDisplayLength);
		blinkMask[3 - blinkPos] = 0;
	}

	setText(text, (blinkPos == -1), currentSlotExists ? 3 : 255, doBlink, blinkMask, blinkImmediately);
}

void SevenSegment::setNextTransitionDirection(int8_t thisDirection) {
	nextTransitionDirection = thisDirection;
}

void SevenSegment::displayPopup(char const* newText, int8_t numFlashes, bool alignRight, uint8_t drawDot,
                                int32_t blinkSpeed) {
	encodeText(newText, popup.segments, alignRight, drawDot);
	memset(&popup.blinkedSegments, 0, kNumericDisplayLength);
	if (numFlashes == 0) {
		popup.blinkCount = -1;
	}
	else {
		popup.blinkCount = numFlashes * 2 + 1;
	}
	popup.currentlyBlanked = false;
	popupActive = true;
	popup.blinkSpeed = blinkSpeed;

	indicator_leds::ledBlinkTimeout(0, true);
	popup.isNowOnTop();
	render();
}

void SevenSegment::cancelPopup() {
	if (popupActive) {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
		popupActive = false;
		topLayer->isNowOnTop();
		render();
	}
}

void SevenSegment::timerRoutine() {
	NumericLayer* layer;
	if (popupActive) {
		layer = &popup;
	}
	else {
		layer = topLayer;
	}

	bool shouldRemoveLayer = layer->callBack();

	if (shouldRemoveLayer) {
		if (!popupActive) {
			removeTopLayer();
		}
		else {
			cancelPopup();
		}
	}
	else {
		render();
	}
}

void SevenSegment::render() {

	NumericLayer* layer;
	if (popupActive) {
		layer = &popup;
	}
	else {
		layer = topLayer;
	}

	std::array<uint8_t, kNumericDisplayLength> segments;
	layer->render(segments.data());
	lastDisplay_ = segments;

	PIC::update7SEG(segments);
	HIDSysex::sendDisplayIfChanged();
}

// Call this to make the loading animation happen
void SevenSegment::displayLoadingAnimation(bool delayed, bool transparent) {
	void* layerSpace = GeneralMemoryAllocator::get().alloc(sizeof(NumericLayerLoadingAnimation));
	if (!layerSpace) {
		return;
	}
	NumericLayerLoadingAnimation* loadingAnimation = new (layerSpace) NumericLayerLoadingAnimation();

	loadingAnimation->animationIsTransparent = transparent;

	setTopLayer(loadingAnimation);
}

void SevenSegment::setTextVeryBasicA1(char const* text) {
	std::array<uint8_t, kNumericDisplayLength> segments;
	encodeText(text, segments.data(), false, 255, true, 0);
	PIC::update7SEG(segments);
}

// Highest error code used, main branch: E451
// Highest error code used, fix branch: i041

void SevenSegment::freezeWithError(char const* text) {
	setTextVeryBasicA1(text);

	while (1) {
		PIC::flush();
		uartFlushIfNotSending(UART_ITEM_MIDI);

		uint8_t value;
		bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
		if (anything && value == 175) {
			break;
		}
	}

	setTextVeryBasicA1("OK");
}

bool SevenSegment::isLayerCurrentlyOnTop(NumericLayer* layer) {
	return (!popupActive && layer == topLayer);
}

extern std::string_view getErrorMessage(int32_t error);

void SevenSegment::displayError(int32_t error) {
	char const* message = nullptr;
	switch (error) {
	case NO_ERROR:
	case ERROR_ABORTED_BY_USER:
		return;
	default:
		message = getErrorMessage(error).data();
		break;
	}
	SevenSegment::displayPopup(message);
}
} // namespace deluge::hid::display
