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

#ifndef NUMERICDRIVER_H
#define NUMERICDRIVER_H

#include "definitions.h"
#include "NumericLayerBasicText.h"
class NumericLayerScrollingText;

class NumericDriver {
public:
	NumericDriver();

	void setText(char const* newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	             uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	             int scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false);
	void setNextTransitionDirection(int8_t thisDirection);
	void displayPopup(char const* newText, int8_t numFlashes = 3, bool alignRight = false, uint8_t drawDot = 255,
	                  int blinkSpeed = 1);
	void freezeWithError(char const* text);
	void cancelPopup();
	void displayError(int error);

#if !HAVE_OLED
	void setTextAsNumber(int16_t number, uint8_t drawDot = 255, bool doBlink = false);
	void setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink = false,
	                   int blinkPos = -1, bool blinkImmediately = false);
	void timerRoutine();
	void removeTopLayer();
	NumericLayerScrollingText* setScrollingText(char const* newText, int startAtPos = 0, int initialDelay = 600);
	int getEncodedPosFromLeft(int textPos, char const* text, bool* andAHalf);
	void render();
	void displayLoadingAnimation(bool delayed = false, bool transparent = false);
	bool isLayerCurrentlyOnTop(NumericLayer* layer);
#endif

	bool popupActive;

private:
	NumericLayer* topLayer;
	NumericLayerBasicText popup;
	int8_t nextTransitionDirection;

	void deleteAllLayers();

#if !HAVE_OLED
	int encodeText(char const* newText, uint8_t* destination, bool alignRight, uint8_t drawDot = 255,
	               bool limitToDisplayLength = true, int scrollPos = 0);
	void replaceBottomLayer(NumericLayer* newLayer);
	void setTopLayer(NumericLayer* newTopLayer);
	void transitionToNewLayer(NumericLayer* newLayer);
	void setTextVeryBasicA1(char const* text);
#endif
};

extern "C" void displayPopupIfAllBootedUp(char const* text);

extern NumericDriver numericDriver;

#endif // NUMERICDRIVER_H
