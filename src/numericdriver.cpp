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

#include "numericdriver.h"
#include "uitimermanager.h"
#include "uart.h"
#include <string.h>
#include "NumericLayerScrollTransition.h"
#include "NumericLayerBasicText.h"
#include "NumericLayerLoadingAnimation.h"
#include "NumericLayerScrollingText.h"
#include "ActionLogger.h"
#include "GeneralMemoryAllocator.h"
#include <new>
#include "functions.h"
#include "IndicatorLEDs.h"
#if HAVE_OLED
#include "oled.h"
#endif

extern "C" {
#include "sio_char.h"
#include "cfunctions.h"
}

NumericDriver numericDriver;

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

NumericDriver::NumericDriver() {
	popupActive = false;
	topLayer = NULL;
	nextTransitionDirection = 0;
}

#if !HAVE_OLED
void NumericDriver::setTopLayer(NumericLayer* newTopLayer) {
	newTopLayer->next = topLayer;
	topLayer = newTopLayer;

	if (!popupActive) {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
		topLayer->isNowOnTop();
		render();
	}
}

void NumericDriver::deleteAllLayers() {
	while (topLayer) {
		NumericLayer* toDelete = topLayer;
		topLayer = topLayer->next;
		toDelete->~NumericLayer();
		generalMemoryAllocator.dealloc(toDelete);
	}
}

void NumericDriver::removeTopLayer() {
	if (!topLayer || !topLayer->next) return;

	NumericLayer* toDelete = topLayer;
	topLayer = topLayer->next;

	toDelete->~NumericLayer();
	generalMemoryAllocator.dealloc(toDelete);

	if (!popupActive) {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
		topLayer->isNowOnTop();
		render();
	}
}
#endif

void NumericDriver::setText(char const* newText, bool alignRight, uint8_t drawDot, bool doBlink, uint8_t* newBlinkMask,
                            bool blinkImmediately, bool shouldBlinkFast, int scrollPos, uint8_t* encodedAddition,
                            bool justReplaceBottomLayer) {
#if !HAVE_OLED
	void* layerSpace = generalMemoryAllocator.alloc(sizeof(NumericLayerBasicText));
	if (!layerSpace) return;
	NumericLayerBasicText* newLayer = new (layerSpace) NumericLayerBasicText();

	encodeText(newText, newLayer->segments, alignRight, drawDot, true, scrollPos);

	if (encodedAddition) {
		for (int i = 0; i < NUMERIC_DISPLAY_LENGTH; i++) {
			newLayer->segments[i] |= encodedAddition[i];
		}
	}

	newLayer->blinkCount = -1;
	newLayer->currentlyBlanked = blinkImmediately;

	if (!doBlink) newLayer->blinkSpeed = 0;
	else {
		if (newBlinkMask) {
			for (int i = 0; i < NUMERIC_DISPLAY_LENGTH; i++) {
				newLayer->blinkedSegments[i] = newLayer->segments[i] & newBlinkMask[i];
			}
		}
		else {
			memset(newLayer->blinkedSegments, 0, NUMERIC_DISPLAY_LENGTH);
		}

		if (!shouldBlinkFast) newLayer->blinkSpeed = 1;
		else newLayer->blinkSpeed = 2;
	}

	if (justReplaceBottomLayer) {
		replaceBottomLayer(newLayer);
	}
	else {
		transitionToNewLayer(newLayer);
	}
#endif
}

#if !HAVE_OLED
NumericLayerScrollingText* NumericDriver::setScrollingText(char const* newText, int startAtTextPos, int initialDelay) {
	void* layerSpace = generalMemoryAllocator.alloc(sizeof(NumericLayerScrollingText));
	if (!layerSpace) return NULL;
	NumericLayerScrollingText* newLayer = new (layerSpace) NumericLayerScrollingText();

	newLayer->length = encodeText(newText, newLayer->text, false, 255, false);

	bool andAHalf;
	int startAtEncodedPos = getEncodedPosFromLeft(startAtTextPos, newText, &andAHalf);

	startAtEncodedPos = getMin(startAtEncodedPos, (int)newLayer->length - 4);
	startAtEncodedPos = getMax(startAtEncodedPos, 0);

	newLayer->currentPos = startAtEncodedPos;
	newLayer->initialDelay = initialDelay;

	transitionToNewLayer(newLayer);

	return newLayer;
}

void NumericDriver::replaceBottomLayer(NumericLayer* newLayer) {
	NumericLayer** prevPointer = &topLayer;
	while ((*prevPointer)->next)
		prevPointer = &(*prevPointer)->next;

	NumericLayer* toDelete = *prevPointer;
	*prevPointer = newLayer;
	toDelete->~NumericLayer();
	generalMemoryAllocator.dealloc(toDelete);

	if (!popupActive && topLayer == newLayer) {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
		topLayer->isNowOnTop();
	}

	render();
}

void NumericDriver::transitionToNewLayer(NumericLayer* newLayer) {

	NumericLayerScrollTransition* scrollTransition = NULL;

	// If transition...
	if (!popupActive && nextTransitionDirection != 0 && topLayer != NULL) {

		void* layerSpace = generalMemoryAllocator.alloc(sizeof(NumericLayerScrollTransition));

		if (layerSpace) {
			scrollTransition = new (layerSpace) NumericLayerScrollTransition();
			scrollTransition->transitionDirection = nextTransitionDirection;

			scrollTransition->transitionProgress = -NUMERIC_DISPLAY_LENGTH * scrollTransition->transitionDirection;

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
int NumericDriver::getEncodedPosFromLeft(int textPos, char const* text, bool* andAHalf) {

	int encodedPos = 0;
	bool lastSegmentHasDot =
	    true; // Pretend this initially, cos the segment before the first one doesn't exist, obviously
	*andAHalf = false;

	for (int i = 0; true; i++) {
		char thisChar = *text;
		if (!thisChar) break; // Stop at end of string
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

		if (i == textPos) break;

		text++;
		encodedPos++;
	}

	return encodedPos;
}

// Returns encoded length
// scrollPos may only be set when aligning left
int NumericDriver::encodeText(char const* newText, uint8_t* destination, bool alignRight, uint8_t drawDot,
                              bool limitToDisplayLength, int scrollPos) {

	int writePos;
	int readPos;

	if (!alignRight) {
		readPos = 0;
		writePos = -scrollPos;
	}
	else {
		writePos = NUMERIC_DISPLAY_LENGTH - 1;
		readPos = strlen(newText) - 1;
	}

	bool carryingDot = false;
	uint8_t prevSegment;

	while (true) {
		unsigned char thisChar = newText[readPos];
		uint8_t* segments = &destination[getMax(writePos, 0)];

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
					if (writePos >= 0) segments--;
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
				if (readPos < 0 || writePos < 0) break;
			}
			else {
				if (!newText[readPos] || (limitToDisplayLength && writePos >= NUMERIC_DISPLAY_LENGTH)) break;
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
					if (writePos < 0) break; // If we've hit the left, get out

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
			while (writePos < NUMERIC_DISPLAY_LENGTH) {
				destination[writePos] = 0;
				writePos++;
			}
		}

		if (drawDot < NUMERIC_DISPLAY_LENGTH) {
			destination[drawDot] |= 128;
		}
		else if ((drawDot & 0b11110000) == 0b10000000) {
			for (int i = 0; i < NUMERIC_DISPLAY_LENGTH; i++) {
				if ((drawDot >> (NUMERIC_DISPLAY_LENGTH - 1 - i)) & 1) {
					destination[i] |= 128;
				}
			}
		}
	}
	return writePos;
}

void NumericDriver::setTextAsNumber(int16_t number, uint8_t drawDot, bool doBlink) {
	char text[12];
	intToString(number, text, 1);

	setText(text, true, drawDot, doBlink);
}

void NumericDriver::setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink,
                                  int blinkPos, bool blinkImmediately) {
	char text[12];

	//int minNumDigits = getMax(1, blinkPos + 1);
	int minNumDigits = (blinkPos == -1) ? -1 : 3;

	slotToString(currentSlot, currentSubSlot, text, minNumDigits);

	uint8_t blinkMask[NUMERIC_DISPLAY_LENGTH];
	if (blinkPos == -1) {
		memset(&blinkMask, 0, NUMERIC_DISPLAY_LENGTH);
	}
	else {
		blinkPos++; // Move an extra space left if we have a sub-slot / letter suffix
		memset(&blinkMask, 255, NUMERIC_DISPLAY_LENGTH);
		blinkMask[3 - blinkPos] = 0;
	}

	setText(text, (blinkPos == -1), currentSlotExists ? 3 : 255, doBlink, blinkMask, blinkImmediately);
}
#endif

void NumericDriver::setNextTransitionDirection(int8_t thisDirection) {
	nextTransitionDirection = thisDirection;
}

void NumericDriver::displayPopup(char const* newText, int8_t numFlashes, bool alignRight, uint8_t drawDot,
                                 int blinkSpeed) {

#if HAVE_OLED
	OLED::popupText(newText, !numFlashes);
#else
	encodeText(newText, popup.segments, alignRight, drawDot);
	memset(&popup.blinkedSegments, 0, NUMERIC_DISPLAY_LENGTH);
	if (numFlashes == 0) popup.blinkCount = -1;
	else popup.blinkCount = numFlashes * 2 + 1;
	popup.currentlyBlanked = false;
	popupActive = true;
	popup.blinkSpeed = blinkSpeed;

	IndicatorLEDs::ledBlinkTimeout(0, true);
	popup.isNowOnTop();
	render();
#endif
}

void NumericDriver::cancelPopup() {
#if HAVE_OLED
	OLED::removePopup();
#else
	if (popupActive) {
		uiTimerManager.unsetTimer(TIMER_DISPLAY);
		popupActive = false;
		topLayer->isNowOnTop();
		render();
	}
#endif
}

#if !HAVE_OLED
void NumericDriver::timerRoutine() {
	NumericLayer* layer;
	if (popupActive) layer = &popup;
	else layer = topLayer;

	bool shouldRemoveLayer = layer->callBack();

	if (shouldRemoveLayer) {
		if (!popupActive) {
			removeTopLayer();
		}
		else {
			cancelPopup();
		}
	}
	else render();
}

void NumericDriver::render() {

	NumericLayer* layer;
	if (popupActive) layer = &popup;
	else layer = topLayer;

	uint8_t segments[NUMERIC_DISPLAY_LENGTH];
	layer->render(segments);

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	bufferPICUart(116);
#else
	bufferPICUart(224);
#endif
	for (int whichChar = 0; whichChar < NUMERIC_DISPLAY_LENGTH; whichChar++) {
		bufferPICUart(segments[whichChar]);
	}
}

// Call this to make the loading animation happen
void NumericDriver::displayLoadingAnimation(bool delayed, bool transparent) {
	void* layerSpace = generalMemoryAllocator.alloc(sizeof(NumericLayerLoadingAnimation));
	if (!layerSpace) return;
	NumericLayerLoadingAnimation* loadingAnimation = new (layerSpace) NumericLayerLoadingAnimation();

	loadingAnimation->animationIsTransparent = transparent;

	setTopLayer(loadingAnimation);
}

void NumericDriver::setTextVeryBasicA1(char const* text) {

	uint8_t segments[NUMERIC_DISPLAY_LENGTH];
	encodeText(text, segments, false, 255, true, 0);
#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	bufferPICUart(116);
#else
	bufferPICUart(224);
#endif
	for (int whichChar = 0; whichChar < NUMERIC_DISPLAY_LENGTH; whichChar++) {
		bufferPICUart(segments[whichChar]);
	}
}
#endif

// Highest error code used, main branch: E448
// Highest error code used, fix branch: i041

void NumericDriver::freezeWithError(char const* text) {

#if HAVE_OLED
	OLED::freezeWithError(text);
#else
	setTextVeryBasicA1(text);

	while (1) {
		uartFlushIfNotSending(UART_ITEM_PIC);
		uartFlushIfNotSending(UART_ITEM_MIDI);

		uint8_t value;
		bool anything = uartGetChar(UART_ITEM_PIC, (char*)&value);
		if (anything && value == 175) break;
	}

	setTextVeryBasicA1("OK");
#endif
}

extern "C" void freezeWithError(char const* error) {
	if (ALPHA_OR_BETA_VERSION) numericDriver.freezeWithError(error);
}

extern "C" void displayPopup(char const* text) {
	numericDriver.displayPopup(text);
}

extern uint8_t usbInitializationPeriodComplete;

extern "C" void displayPopupIfAllBootedUp(char const* text) {
	if (usbInitializationPeriodComplete) {
		numericDriver.displayPopup(text);
	}
}

#if !HAVE_OLED
bool NumericDriver::isLayerCurrentlyOnTop(NumericLayer* layer) {
	return (!popupActive && layer == topLayer);
}
#endif

void NumericDriver::displayError(int error) {

	char const* message;

	switch (error) {
	case NO_ERROR:
	case ERROR_ABORTED_BY_USER:
		return;

#if HAVE_OLED
	case ERROR_INSUFFICIENT_RAM:
		message = "Insufficient RAM";
		break;
	case ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE:
		message = "Too many files in folder";
		break;
	case ERROR_SD_CARD:
		message = "SD card error";
		break;
	case ERROR_SD_CARD_NOT_PRESENT:
		message = "No SD card present";
		break;
	case ERROR_SD_CARD_NO_FILESYSTEM:
		message = "Please use FAT32-formatted card";
		break;
	case ERROR_FILE_CORRUPTED:
		message = "File corrupted";
		break;
	case ERROR_FILE_NOT_FOUND:
		message = "File not found";
		break;
	case ERROR_FILE_UNREADABLE:
		message = "File unreadable";
		break;
	case ERROR_FILE_UNSUPPORTED:
		message = "File unsupported";
		break;
	case ERROR_FILE_FIRMWARE_VERSION_TOO_NEW:
		message = "Your firmware version is too old to open this file";
		break;
	case ERROR_FOLDER_DOESNT_EXIST:
		message = "Folder not found";
		break;
	case ERROR_BUG:
		message = "Bug encountered";
		break;
	case ERROR_WRITE_FAIL:
		message = "SD card write error";
		break;
	case ERROR_FILE_TOO_BIG:
		message = "File too large";
		break;
	case ERROR_PRESET_IN_USE:
		message = "This preset is in-use elsewhere in your song";
		break;
	case ERROR_NO_FURTHER_PRESETS:
	case ERROR_NO_FURTHER_FILES_THIS_DIRECTION:
		message = "No more presets found";
		break;
	case ERROR_MAX_FILE_SIZE_REACHED:
		message = "Maximum file size reached";
		break;
	case ERROR_SD_CARD_FULL:
		message = "SD card full";
		break;
	case ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE:
		message = "File does not contain wavetable data";
		break;
	case ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
		message = "Stereo files cannot be used as wavetables";
		break;
	case ERROR_WRITE_PROTECTED:
		message = "Card is write-protected";
		break;
#else
	case ERROR_INSUFFICIENT_RAM_FOR_FOLDER_CONTENTS_SIZE:
	case ERROR_INSUFFICIENT_RAM:
		message = "RAM";
		break;
	case ERROR_SD_CARD:
	case ERROR_SD_CARD_NOT_PRESENT:
	case ERROR_SD_CARD_NO_FILESYSTEM:
		message = "CARD";
		break;
	case ERROR_FILE_CORRUPTED:
		message = "CORRUPTED";
		break;
	case ERROR_FILE_NOT_FOUND:
	case ERROR_FILE_UNREADABLE:
		message = "FILE";
		break;
	case ERROR_FILE_UNSUPPORTED:
		message = "UNSUPPORTED";
		break;
	case ERROR_FILE_FIRMWARE_VERSION_TOO_NEW:
		message = "FIRMWARE";
		break;
	case ERROR_FOLDER_DOESNT_EXIST:
		message = "FOLDER";
		break;
	case ERROR_BUG:
		message = "BUG";
		break;
	case ERROR_WRITE_FAIL:
		message = "FAIL";
		break;
	case ERROR_FILE_TOO_BIG:
		message = "BIG";
		break;
	case ERROR_PRESET_IN_USE:
		message = "USED";
		break;
	case ERROR_NO_FURTHER_PRESETS:
	case ERROR_NO_FURTHER_FILES_THIS_DIRECTION:
		message = "NONE";
		break;
	case ERROR_MAX_FILE_SIZE_REACHED:
		message = "4GB";
		break;
	case ERROR_SD_CARD_FULL:
		message = "FULL";
		break;
	case ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE:
		message = "CANT";
		break;
	case ERROR_FILE_NOT_LOADABLE_AS_WAVETABLE_BECAUSE_STEREO:
		message = "STEREO";
		break;
	case ERROR_WRITE_PROTECTED:
		message = "WRITE-PROTECTED";
		break;
#endif
	default:
		message = "ERROR";
	}

	displayPopup(message);
}
