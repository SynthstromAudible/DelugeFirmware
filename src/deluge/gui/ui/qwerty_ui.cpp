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

#include "gui/ui/qwerty_ui.h"
#include "definitions_cxx.hpp"
#include "extern.h"
#include "gui/colour/colour.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/led/indicator_leds.h"
#include "hid/led/pad_leds.h"
#include "hid/matrix/matrix_driver.h"
#include "storage/flash_storage.h"
#include "storage/storage_manager.h"
#include "util/functions.h"
#include "util/misc.h"
#include <string.h>

using namespace deluge::gui;

bool QwertyUI::predictionInterrupted;
String QwertyUI::enteredText{};
// entered text edit position is the first difference from
// the previously seen name while browsing/editing
int16_t QwertyUI::enteredTextEditPos;
int32_t QwertyUI::scrollPosHorizontal;

uint8_t QwertyUI::currentBank = 0;
std::optional<uint8_t> QwertyUI::currentFavourite = std::nullopt;
bool QwertyUI::favouritesVisible = false;
uint8_t QwertyUI::favouriteRow = 6;
bool QwertyUI::banksVisible = false;
FavouritesDefaultLayout QwertyUI::favouritesLayoutSelected = FavouritesDefaultLayout::FavouritesDefaultLayoutFavorites;

static constexpr float colourStep = 192 / 16;

bool QwertyUI::opened() {

	indicator_leds::blinkLed(IndicatorLED::BACK);
	enteredTextEditPos = 0;
	enteredText.clear();
	switch (FlashStorage::defaultFavouritesLayout) {
	case FavouritesDefaultLayoutFavorites: {
		QwertyUI::favouritesLayoutSelected = FavouritesDefaultLayoutFavorites;
		QwertyUI::favouriteRow = 7;
		QwertyUI::banksVisible = false;
		break;
	}
	case FavouritesDefaultLayoutFavoritesAndBanks: {
		QwertyUI::favouritesLayoutSelected = FavouritesDefaultLayoutFavoritesAndBanks;
		QwertyUI::favouriteRow = 6;
		QwertyUI::banksVisible = true;
		break;
	}
	default: {
		QwertyUI::favouritesLayoutSelected = FavouritesDefaultLayoutOff;
		break;
	}
	}
	return true;
}

void QwertyUI::drawKeys() {
	PadLEDs::clearMainPadsWithoutSending();

	PadLEDs::clearTickSquares(false);

	// General key area
	for (size_t i = 0; i < 11; i++) {
		if (i < 10) {
			PadLEDs::image[kQwertyHomeRow + 2][3 + i] = RGB::monochrome(64); // 1234
			PadLEDs::image[kQwertyHomeRow + 1][3 + i] = RGB::monochrome(10); // qwer
		}
		PadLEDs::image[kQwertyHomeRow][3 + i] = RGB::monochrome(10); // asdf
		if (i < 9) {
			PadLEDs::image[kQwertyHomeRow - 1][3 + i] = RGB::monochrome(10); // zxcv
		}
	}

	PadLEDs::image[kQwertyHomeRow + 2][13] = RGB::monochrome(10);

	// Home rows
	PadLEDs::image[kQwertyHomeRow][3] = RGB::monochrome(64);
	PadLEDs::image[kQwertyHomeRow][4] = RGB::monochrome(64);
	PadLEDs::image[kQwertyHomeRow][5] = RGB::monochrome(64);
	PadLEDs::image[kQwertyHomeRow][6] = RGB::monochrome(160);
	PadLEDs::image[kQwertyHomeRow][9] = RGB::monochrome(160);
	PadLEDs::image[kQwertyHomeRow][10] = RGB::monochrome(64);
	PadLEDs::image[kQwertyHomeRow][11] = RGB::monochrome(64);
	PadLEDs::image[kQwertyHomeRow][12] = RGB::monochrome(64);

	// Space bar
	for (size_t i = 0; i < 6; i++) {
		PadLEDs::image[kQwertyHomeRow - 2][5 + i] = RGB::monochrome(160);
	}

	for (int32_t x = 0; x < 2; x++) {
		// Backspace
		PadLEDs::image[kQwertyHomeRow + 2][14 + x] = colours::red;

		// Enter
		PadLEDs::image[kQwertyHomeRow][14 + x] = colours::green;

		// Shift
		PadLEDs::image[kQwertyHomeRow - 1][1 + x] = colours::blue;
		PadLEDs::image[kQwertyHomeRow - 1][13 + x] = colours::blue;
	}

	if (favouritesVisible) {
		renderFavourites();
	}
	PadLEDs::sendOutMainPadColours();
}

void QwertyUI::drawTextForOLEDEditing(int32_t xPixel, int32_t xPixelMax, int32_t yPixel, int32_t maxNumChars,
                                      deluge::hid::display::oled_canvas::Canvas& canvas) {

	char const* displayName = enteredText.get();
	int32_t displayStringLength = enteredText.getLength();

	bool atVeryEnd = (enteredTextEditPos == displayStringLength);

	int32_t xScrollHere = 0;

	// Prevent being scrolled too far left.
	int32_t minXScroll = std::min(enteredTextEditPos + 3 - maxNumChars, displayStringLength - maxNumChars + atVeryEnd);
	minXScroll = std::max(minXScroll, 0_i32);
	scrollPosHorizontal = std::max(scrollPosHorizontal, minXScroll);

	// Prevent being scrolled too far right.
	int32_t maxXScroll = std::min<int32_t>(displayStringLength - maxNumChars + atVeryEnd,
	                                       enteredTextEditPos - 3); // First part of this might not be needed I think?
	maxXScroll = std::max(maxXScroll, 0_i32);
	scrollPosHorizontal = std::min(scrollPosHorizontal, maxXScroll);

	canvas.drawString(&displayName[scrollPosHorizontal], xPixel, yPixel, kTextSpacingX, kTextSpacingY, 0,
	                  xPixel + maxNumChars * kTextSpacingX);

	int32_t highlightStartX = 0;
	int32_t highlightWidth = xPixelMax - highlightStartX;

	if (atVeryEnd) {
		if (getCurrentUI() == this) {
			int32_t cursorStartX = xPixel + (displayStringLength - scrollPosHorizontal) * kTextSpacingX;
			int32_t textBottomY = yPixel + kTextSpacingY;
			deluge::hid::display::OLED::setupBlink(cursorStartX, kTextSpacingX, textBottomY - 4, textBottomY - 2, true);
		}
	}
	else {
		canvas.invertLeftEdgeForMenuHighlighting(highlightStartX, highlightWidth, yPixel, yPixel + kTextSpacingY - 1);
	}
}

void QwertyUI::displayText(bool blinkImmediately) {

	int32_t totalTextLength = enteredText.getLength();

	bool encodedEditPosAndAHalf;
	int32_t encodedEditPos =
	    display->getEncodedPosFromLeft(enteredTextEditPos, enteredText.get(), &encodedEditPosAndAHalf);

	bool encodedEndPosAndAHalf;
	int32_t encodedEndPos = display->getEncodedPosFromLeft(totalTextLength, enteredText.get(), &encodedEndPosAndAHalf);

	int32_t scrollPos = encodedEditPos - (kNumericDisplayLength >> 1) + encodedEditPosAndAHalf;
	int32_t maxScrollPos = encodedEndPos - kNumericDisplayLength;
	if (totalTextLength == enteredTextEditPos) {
		maxScrollPos++;
	}
	scrollPos = std::min(scrollPos, maxScrollPos);
	scrollPos = std::max(scrollPos, 0_i32);

	int32_t editPosOnscreen = encodedEditPos - scrollPos;

	// Place the '_' for editing
	uint8_t encodedAddition[kNumericDisplayLength];
	memset(encodedAddition, 0, kNumericDisplayLength);
	if (totalTextLength == enteredTextEditPos || enteredText.get()[enteredTextEditPos] == ' ') {
		if (ALPHA_OR_BETA_VERSION && (editPosOnscreen < 0 || editPosOnscreen >= kNumericDisplayLength)) {
			FREEZE_WITH_ERROR("E292");
		}
		encodedAddition[editPosOnscreen] = 0x08;
		encodedEditPosAndAHalf = false; // Hard to put into words why this is needed, but without it, the blinking _
		                                // after a . just won't blink
	}

	uint8_t blinkMask[kNumericDisplayLength];
	for (int32_t i = 0; i < kNumericDisplayLength; i++) {
		if (i < editPosOnscreen) {
			blinkMask[i] = 255; // Blink none
		}
		else if (i == editPosOnscreen && encodedEditPosAndAHalf) {
			blinkMask[i] = 0b01111111; // Blink the dot
		}
		else {
			blinkMask[i] = 0; // Blink all
		}
	}

	indicator_leds::ledBlinkTimeout(0, true, !blinkImmediately);

	// Set the text, replacing the bottom layer - cos in some cases, we want this to slip under an existing loading
	// animation layer
	display->setText(enteredText.get(), false, 255, true, blinkMask, false, false, scrollPos, encodedAddition, false);
}

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

ActionResult QwertyUI::padAction(int32_t x, int32_t y, int32_t on) {

	// Backspace
	if (y == kQwertyHomeRow + 2 && x >= 14 && x < 16) {
		if (on) {
			if (currentUIMode == UI_MODE_PREDICTING_QWERTY_TEXT) {
				predictionInterrupted = true;
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			else if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {
				predictionInterrupted = true;
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			else if (!currentUIMode) {
				currentUIMode = UI_MODE_HOLDING_BACKSPACE;
				processBackspace();
				uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500);
			}
		}
		else {
			if (currentUIMode == UI_MODE_HOLDING_BACKSPACE) {
				currentUIMode = UI_MODE_NONE;
				uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
			}
		}
	}

	// Enter
	else if (y == kQwertyHomeRow && x >= 14 && x < 16) {
		if (on) {
			if (currentUIMode == UI_MODE_PREDICTING_QWERTY_TEXT) {
				predictionInterrupted = true;
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// If currently loading preset, definitely don't abort that - make the user wait and press button again when
			// finished
			else if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {
				return ActionResult::DEALT_WITH;
			}

			else if (!currentUIMode) {
				if (sdRoutineLock) {
					return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
				}
				enterKeyPress();
			}
		}
	}

	// Normal keys
	else if (x >= 3 && x < 14 && y >= kQwertyHomeRow - 2 && y <= kQwertyHomeRow + 2) {
		if (on) {

			// If predicting, gotta interrupt that
			if (currentUIMode == UI_MODE_PREDICTING_QWERTY_TEXT) {
				predictionInterrupted = true;
				return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
			}

			// Otherwise, if we still might want to use this press...
			else if (!currentUIMode || currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {

				char newChar =
				    keyboardChars[util::to_underlying(FlashStorage::keyboardLayout)][kQwertyHomeRow - y + 2][x - 3];
				if (newChar == 0) {
					return ActionResult::DEALT_WITH;
				}

				// First character must be alphanumerical
				if (!enteredTextEditPos) {
					if ((newChar >= 'A' && newChar <= 'Z') || (newChar >= '0' && newChar <= '9')) {
					} // Then everything's fine
					else {
						return ActionResult::DEALT_WITH;
					}
				}

				// If holding shift...
				if (y == kQwertyHomeRow + 2) {
					if (matrixDriver.isPadPressed(1, kQwertyHomeRow - 1)
					    || matrixDriver.isPadPressed(2, kQwertyHomeRow - 1)
					    || matrixDriver.isPadPressed(13, kQwertyHomeRow - 1)
					    || matrixDriver.isPadPressed(14, kQwertyHomeRow - 1)) {

						// Apply that to keys which have a shift character
						if (newChar == '-') {
							newChar = '_';
						}
						else if (newChar == '1') {
							newChar = '!';
						}
						else if (newChar == '3') {
							newChar = '#';
						}
						else if (newChar == '6') {
							newChar = '^';
						}
					}
				}

				// If this char was already predicted, that's easy and we can just move forward
				if (charCaseEqual(enteredText.get()[enteredTextEditPos], newChar)) {

					// If currently loading a preset, that's fine!
					if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {}

					// But if otherwise accessing card, not fine - e.g. if loading song visual preview
					else if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					enteredTextEditPos++;
				}

				// Otherwise, char is all new, so add it on
				else {

					// But if currently loading a preset, gotta abort that first
					if (currentUIMode == UI_MODE_LOADING_BUT_ABORT_IF_SELECT_ENCODER_TURNED) {
						predictionInterrupted = true;
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}

					// Or if the card is just generally being accessed (e.g. samples being buffered), come back soon,
					// because we couldn't do anything like a "prediction" right now
					else if (sdRoutineLock) {
						return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
					}
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

						Error error = enteredText.concatenateAtPos(stringToConcat, enteredTextEditPos);

						if (error != Error::NONE) {
							display->displayError(error);
							return ActionResult::DEALT_WITH;
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

				displayText(); // We could actually skip this if the user had intervened during our own
				               // predictExtendedText() call above... kinda...
			}
		}
	}

	return ActionResult::DEALT_WITH;
}

void QwertyUI::renderFavourites() {

	if (!favouritesVisible) {
		return;
	}
	std::array<std::optional<uint8_t>, 16> colours = favouritesManager.getFavouriteColours();
	currentBank = favouritesManager.currentBankNumber;
	currentFavourite = favouritesManager.currentFavouriteNumber;

	if (favouritesLayoutSelected == FavouritesDefaultLayout::FavouritesDefaultLayoutOff) {
		PadLEDs::sendOutMainPadColours();
		return;
	}

	for (uint8_t i = 0; i < 16; i++) {
		// Render Banks
		if (favouritesLayoutSelected == FavouritesDefaultLayout::FavouritesDefaultLayoutFavoritesAndBanks) {
			PadLEDs::image[favouriteBankRow][i] = RGB::fromHue(static_cast<int16_t>(i * colourStep));
		}
		// Render Favourites
		if (colours[i].has_value()) {
			PadLEDs::image[favouriteRow][i] = RGB::fromHue(static_cast<int16_t>(colours[i].value() * colourStep));
		}
		else {
			PadLEDs::image[favouriteRow][i] = RGB::fromHue(static_cast<int16_t>(8 * colourStep)).dim(6);
		}
	}

	PadLEDs::sendOutMainPadColours();

	// Flash selected Bank & Favourite Pads
	uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500);
}

void QwertyUI::processBackspace() {
	if (enteredTextEditPos > 0) {
		enteredTextEditPos--;
	}
	if (!enteredText.isEmpty()) {
		enteredText.shorten(enteredTextEditPos);
		predictExtendedText();
		predictionInterrupted = false;
		displayText();
	}
}

ActionResult QwertyUI::horizontalEncoderAction(int32_t offset) {
	if (offset == 1) {

		// If already at far right end, just see if we can predict any further characters
		if (enteredTextEditPos == enteredText.getLength()) {
			predictExtendedText();

			// If not, get out
			if (enteredTextEditPos == enteredText.getLength()) {
				return ActionResult::DEALT_WITH;
			}

			goto doDisplayText;
		}
	}
	else {
		if (enteredTextEditPos == 0) {
			return ActionResult::DEALT_WITH;
		}
	}

	enteredTextEditPos += offset;

doDisplayText:
	displayText();

	return ActionResult::DEALT_WITH;
}

ActionResult QwertyUI::timerCallback() {
	if (currentUIMode == UI_MODE_HOLDING_BACKSPACE) {
		processBackspace();
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, display->haveOLED() ? 80 : 125);
	}

	// Flash selected Bank & Favourite Pads
	if (favouritesVisible) {

		if (QwertyUI::currentFavourite.has_value()) {
			PadLEDs::flashMainPad(QwertyUI::currentFavourite.value(), favouriteRow);
		}

		if (QwertyUI::favouritesLayoutSelected == FavouritesDefaultLayout::FavouritesDefaultLayoutFavoritesAndBanks) {
			PadLEDs::flashMainPad(QwertyUI::currentBank, favouriteBankRow);
		}

		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, 500);
	}

	return ActionResult::DEALT_WITH;
}
