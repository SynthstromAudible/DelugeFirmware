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

#include <QwertyUI.h>
#include <string.h>
#include "matrixdriver.h"
#include "numericdriver.h"
#include "functions.h"
#include "uitimermanager.h"
#include "uart.h"
#include "storagemanager.h"
#include "PadLEDs.h"
#include "IndicatorLEDs.h"
#include "FlashStorage.h"
#include "extern.h"

#if HAVE_OLED
#include "oled.h"
#endif

bool QwertyUI::predictionInterrupted;
String QwertyUI::enteredText;
int16_t QwertyUI::enteredTextEditPos;
int QwertyUI::scrollPosHorizontal;

QwertyUI::QwertyUI() {
}

bool QwertyUI::opened() {

	IndicatorLEDs::blinkLed(backLedX, backLedY);

	enteredTextEditPos = 0;
	enteredText.clear();
	return true;
}

// Won't "send"
void QwertyUI::drawKeys() {

	PadLEDs::clearTickSquares(false);

	// General key area
	memset(PadLEDs::image[QWERTY_HOME_ROW + 2][3], 64, 10 * 3); // 1234
	memset(PadLEDs::image[QWERTY_HOME_ROW + 2][13], 10, 3);
	memset(PadLEDs::image[QWERTY_HOME_ROW + 1][3], 10, 10 * 3); // qwer
	memset(PadLEDs::image[QWERTY_HOME_ROW][3], 10, 11 * 3);     // asdf
	memset(PadLEDs::image[QWERTY_HOME_ROW - 1][3], 10, 9 * 3);  // zxcv

	// Home row
	memset(PadLEDs::image[QWERTY_HOME_ROW][3], 64, 3 * 3);
	memset(PadLEDs::image[QWERTY_HOME_ROW][6], 160, 3);

	memset(PadLEDs::image[QWERTY_HOME_ROW][10], 64, 3 * 3);
	memset(PadLEDs::image[QWERTY_HOME_ROW][9], 160, 3);

	// Space bar
	memset(PadLEDs::image[QWERTY_HOME_ROW - 2][5], 160, 6 * 3);

	// Backspace
	for (int x = 14; x < 16; x++) {
		PadLEDs::image[QWERTY_HOME_ROW + 2][x][0] = 255;
		PadLEDs::image[QWERTY_HOME_ROW + 2][x][1] = 0;
		PadLEDs::image[QWERTY_HOME_ROW + 2][x][2] = 0;
	}

	// Enter
	for (int x = 14; x < 16; x++) {
		PadLEDs::image[QWERTY_HOME_ROW][x][0] = 0;
		PadLEDs::image[QWERTY_HOME_ROW][x][1] = 255;
		PadLEDs::image[QWERTY_HOME_ROW][x][2] = 0;
	}

	// Shift
	for (int x = 1; x < 3; x++) {
		PadLEDs::image[QWERTY_HOME_ROW - 1][x][0] = 0;
		PadLEDs::image[QWERTY_HOME_ROW - 1][x][1] = 0;
		PadLEDs::image[QWERTY_HOME_ROW - 1][x][2] = 255;
	}
	for (int x = 13; x < 15; x++) {
		PadLEDs::image[QWERTY_HOME_ROW - 1][x][0] = 0;
		PadLEDs::image[QWERTY_HOME_ROW - 1][x][1] = 0;
		PadLEDs::image[QWERTY_HOME_ROW - 1][x][2] = 255;
	}
}

#if HAVE_OLED
void QwertyUI::drawTextForOLEDEditing(int xPixel, int xPixelMax, int yPixel, int maxNumChars,
                                      uint8_t image[][OLED_MAIN_WIDTH_PIXELS]) {

	char const* displayName = enteredText.get();
	int displayStringLength = enteredText.getLength();

	bool atVeryEnd = (enteredTextEditPos == displayStringLength);

	int xScrollHere = 0;

	// Prevent being scrolled too far left.
	int minXScroll = getMin(enteredTextEditPos + 3 - maxNumChars, displayStringLength - maxNumChars + atVeryEnd);
	minXScroll = getMax(minXScroll, 0);
	scrollPosHorizontal = getMax(scrollPosHorizontal, minXScroll);

	// Prevent being scrolled too far right.
	int maxXScroll = getMin(displayStringLength - maxNumChars + atVeryEnd,
	                        enteredTextEditPos - 3); // First part of this might not be needed I think?
	maxXScroll = getMax(maxXScroll, 0);
	scrollPosHorizontal = getMin(scrollPosHorizontal, maxXScroll);

	OLED::drawString(&displayName[scrollPosHorizontal], xPixel, yPixel, image[0], OLED_MAIN_WIDTH_PIXELS,
	                 TEXT_SPACING_X, TEXT_SPACING_Y);

	int hilightStartX = xPixel + TEXT_SPACING_X * (enteredTextEditPos - scrollPosHorizontal);
	//int hilightEndX = xPixel + TEXT_SIZE_X * (displayStringLength - scrollPosHorizontal);
	//if (hilightEndX > OLED_MAIN_WIDTH_PIXELS || !enteredTextEditPos) hilightEndX = OLED_MAIN_WIDTH_PIXELS;
	int hilightWidth = xPixelMax - hilightStartX;

	if (atVeryEnd) {
		if (getCurrentUI() == this) {
			int cursorStartX = xPixel + (displayStringLength - scrollPosHorizontal) * TEXT_SPACING_X;
			int textBottomY = yPixel + TEXT_SPACING_Y;
			OLED::setupBlink(cursorStartX, TEXT_SPACING_X, textBottomY - 4, textBottomY - 2, true);
		}
	}
	else {
		OLED::invertArea(hilightStartX, hilightWidth, yPixel, yPixel + TEXT_SPACING_Y - 1, image);
	}
}

#else
void QwertyUI::displayText(bool blinkImmediately) {

	int totalTextLength = enteredText.getLength();

	bool encodedEditPosAndAHalf;
	int encodedEditPos =
	    numericDriver.getEncodedPosFromLeft(enteredTextEditPos, enteredText.get(), &encodedEditPosAndAHalf);

	bool encodedEndPosAndAHalf;
	int encodedEndPos = numericDriver.getEncodedPosFromLeft(totalTextLength, enteredText.get(), &encodedEndPosAndAHalf);

	int scrollPos = encodedEditPos - (NUMERIC_DISPLAY_LENGTH >> 1) + encodedEditPosAndAHalf;
	int maxScrollPos = encodedEndPos - NUMERIC_DISPLAY_LENGTH;
	if (totalTextLength == enteredTextEditPos) maxScrollPos++;
	scrollPos = getMin(scrollPos, maxScrollPos);
	scrollPos = getMax(scrollPos, 0);

	int editPosOnscreen = encodedEditPos - scrollPos;

	// Place the '_' for editing
	uint8_t encodedAddition[NUMERIC_DISPLAY_LENGTH];
	memset(encodedAddition, 0, NUMERIC_DISPLAY_LENGTH);
	if (totalTextLength == enteredTextEditPos || enteredText.get()[enteredTextEditPos] == ' ') {
		if (ALPHA_OR_BETA_VERSION && (editPosOnscreen < 0 || editPosOnscreen >= NUMERIC_DISPLAY_LENGTH))
			numericDriver.freezeWithError("E292");
		encodedAddition[editPosOnscreen] = 0x08;
		encodedEditPosAndAHalf =
		    false; // Hard to put into words why this is needed, but without it, the blinking _ after a . just won't blink
	}

	uint8_t blinkMask[NUMERIC_DISPLAY_LENGTH];
	for (int i = 0; i < NUMERIC_DISPLAY_LENGTH; i++) {
		if (i < editPosOnscreen) blinkMask[i] = 255;                                        // Blink none
		else if (i == editPosOnscreen && encodedEditPosAndAHalf) blinkMask[i] = 0b01111111; // Blink the dot
		else blinkMask[i] = 0;                                                              // Blink all
	}

	IndicatorLEDs::ledBlinkTimeout(0, true, !blinkImmediately);

	// Set the text, replacing the bottom layer - cos in some cases, we want this to slip under an existing loading animation layer
	numericDriver.setText(enteredText.get(), false, 255, true, blinkMask, false, false, scrollPos, encodedAddition,
	                      false);
}
#endif

const char keyboardChars[][5][11] = {{
                                         {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
                                         {'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0},
                                         {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 0, '\''},
                                         {'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', 0, 0},
                                         {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
                                     },
                                     {
                                         {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
                                         {'A', 'Z', 'E', 'R', 'T', 'Y', 'U', 'I', 'O', 'P', 0},
                                         {'Q', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'M', '\''},
                                         {'W', 'X', 'C', 'V', 'B', 'N', ',', '.', 0, 0, 0},
                                         {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
                                     },
                                     {
                                         {'1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '-'},
                                         {'Q', 'W', 'E', 'R', 'T', 'Z', 'U', 'I', 'O', 'P', 'U'},
                                         {'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K', 'L', 'O', 'A'},
                                         {'Y', 'X', 'C', 'V', 'B', 'N', 'M', ',', '.', '\'', 0},
                                         {0, 0, ' ', ' ', ' ', ' ', ' ', ' ', 0, 0, 0},
                                     }};

int QwertyUI::padAction(int x, int y, int on) {

	// Backspace
	if (y == QWERTY_HOME_ROW + 2 && x >= 14 && x < 16) {
		if (on) {
			if (currentUIMode == UI_MODE_PREDICTING_QWERTY_TEXT) {
				predictionInterrupted = true;
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			else if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {
				predictionInterrupted = true;
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			else if (!currentUIMode) {
				processBackspace();
				uiTimerManager.setTimer(TIMER_UI_SPECIFIC, 500);
				currentUIMode = UI_MODE_HOLDING_BACKSPACE;
			}
		}
		else {
			if (currentUIMode == UI_MODE_HOLDING_BACKSPACE) {
				currentUIMode = UI_MODE_NONE;
				uiTimerManager.unsetTimer(TIMER_UI_SPECIFIC);
			}
		}
	}

	// Enter
	else if (y == QWERTY_HOME_ROW && x >= 14 && x < 16) {
		if (on) {
			if (currentUIMode == UI_MODE_PREDICTING_QWERTY_TEXT) {
				predictionInterrupted = true;
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// If currently loading preset, definitely don't abort that - make the user wait and press button again when finished
			else if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {
				return ACTION_RESULT_DEALT_WITH;
			}

			else if (!currentUIMode) {
				if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
				enterKeyPress();
			}
		}
	}

	// Normal keys
	else if (x >= 3 && x < 14 && y >= QWERTY_HOME_ROW - 2 && y <= QWERTY_HOME_ROW + 2) {
		if (on) {

			// If predicting, gotta interrupt that
			if (currentUIMode == UI_MODE_PREDICTING_QWERTY_TEXT) {
				predictionInterrupted = true;
				return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// Otherwise, if we still might want to use this press...
			else if (!currentUIMode || currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {

				char newChar = keyboardChars[FlashStorage::keyboardLayout][QWERTY_HOME_ROW - y + 2][x - 3];
				if (newChar == 0) return ACTION_RESULT_DEALT_WITH;

				// First character must be alphanumerical
				if (!enteredTextEditPos) {
					if ((newChar >= 'A' && newChar <= 'Z') || (newChar >= '0' && newChar <= '9')) {
					} // Then everything's fine
					else return ACTION_RESULT_DEALT_WITH;
				}

				// If holding shift...
				if (y == QWERTY_HOME_ROW + 2) {
					if (matrixDriver.isPadPressed(1, QWERTY_HOME_ROW - 1)
					    || matrixDriver.isPadPressed(2, QWERTY_HOME_ROW - 1)
					    || matrixDriver.isPadPressed(13, QWERTY_HOME_ROW - 1)
					    || matrixDriver.isPadPressed(14, QWERTY_HOME_ROW - 1)) {

						// Apply that to keys which have a shift character
						if (newChar == '-') newChar = '_';
						else if (newChar == '1') newChar = '!';
						else if (newChar == '3') newChar = '#';
						else if (newChar == '6') newChar = '^';
					}
				}

				// If this char was already predicted, that's easy and we can just move forward
				if (charCaseEqual(enteredText.get()[enteredTextEditPos], newChar)) {

					// If currently loading a preset, that's fine!
					if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {}

					// But if otherwise accessing card, not fine - e.g. if loading song visual preview
					else if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

					enteredTextEditPos++;
				}

				// Otherwise, char is all new, so add it on
				else {

					// But if currently loading a preset, gotta abort that first
					if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {
						predictionInterrupted = true;
						return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					// Or if the card is just generally being accessed (e.g. samples being buffered), come back soon,
					// because we couldn't do anything like a "prediction" right now
					else if (sdRoutineLock) return ACTION_RESULT_REMIND_ME_OUTSIDE_CARD_ROUTINE;

					else {
						// If char is a letter...
						if (newChar >= 'A' && newChar <= 'Z') {

							// If following another letter, make it lowercase
							if (enteredTextEditPos > 0) {
								char prevChar = enteredText.get()[enteredTextEditPos - 1];
								if ((prevChar >= 'A' && prevChar <= 'Z') || (prevChar >= 'a' && prevChar <= 'z')) {
									newChar += 32;
								}
							}
						}

						char stringToConcat[2];
						stringToConcat[0] = newChar;
						stringToConcat[1] = 0;

						int error = enteredText.concatenateAtPos(stringToConcat, enteredTextEditPos);

						if (error) {
							numericDriver.displayError(error);
							return ACTION_RESULT_DEALT_WITH;
						}

						enteredTextEditPos++;

						bool success =
						    predictExtendedText(); // This may get cut short if user interrupts by pressing another pad
						if (!success) {
							enteredTextEditPos--;
						}

						predictionInterrupted = false;
					}
				}

				displayText(); // We could actually skip this if the user had intervened during our own predictExtendedText() call above... kinda...
			}
		}
	}

	return ACTION_RESULT_DEALT_WITH;
}

void QwertyUI::processBackspace() {
	if (enteredTextEditPos > 0) {
		enteredTextEditPos--;
	}
	if (!enteredText.isEmpty()) {
		enteredText.shorten(enteredTextEditPos);
		displayText();
	}
}

int QwertyUI::horizontalEncoderAction(int offset) {
	if (offset == 1) {

		// If already at far right end, just see if we can predict any further characters
		if (enteredTextEditPos == enteredText.getLength()) {
			predictExtendedText();

			// If not, get out
			if (enteredTextEditPos == enteredText.getLength()) return ACTION_RESULT_DEALT_WITH;

			goto doDisplayText;
		}
	}
	else {
		if (enteredTextEditPos == 0) return ACTION_RESULT_DEALT_WITH;
	}

	enteredTextEditPos += offset;

doDisplayText:
	displayText();

	return ACTION_RESULT_DEALT_WITH;
}

int QwertyUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BACKSPACE) {
		processBackspace();
		uiTimerManager.setTimer(TIMER_UI_SPECIFIC, HAVE_OLED ? 80 : 125);
	}

	return ACTION_RESULT_DEALT_WITH;
}
