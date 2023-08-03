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

#pragma once

#include "definitions_cxx.hpp"
#include "hid/display/numeric_layer/numeric_layer_basic_text.h"
#include <string>

class NumericLayerScrollingText;

class NumericDriver {
public:
	NumericDriver();

	void setText(std::string_view newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	             uint8_t* newBlinkMask = NULL, bool blinkImmediately = false, bool shouldBlinkFast = false,
	             int32_t scrollPos = 0, uint8_t* blinkAddition = NULL, bool justReplaceBottomLayer = false);
	void setNextTransitionDirection(int8_t thisDirection);
	void displayPopup(char const* newText, int8_t numFlashes = 3, bool alignRight = false, uint8_t drawDot = 255,
	                  int32_t blinkSpeed = 1);
	void freezeWithError(char const* text);
	void cancelPopup();
	void displayError(int32_t error);

	void setTextAsNumber(int16_t number, uint8_t drawDot = 255, bool doBlink = false);
	void setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink = false,
	                   int32_t blinkPos = -1, bool blinkImmediately = false);
	void timerRoutine();
	void removeTopLayer();
	NumericLayerScrollingText* setScrollingText(char const* newText, int32_t startAtPos = 0,
	                                            int32_t initialDelay = 600);
	int32_t getEncodedPosFromLeft(int32_t textPos, char const* text, bool* andAHalf);
	void render();
	void displayLoadingAnimation(bool delayed = false, bool transparent = false);
	bool isLayerCurrentlyOnTop(NumericLayer* layer);
	uint8_t lastDisplay[kNumericDisplayLength];

	bool hasPopup() { return this->popupActive; }

private:
	NumericLayer* topLayer;
	NumericLayerBasicText popup;
	int8_t nextTransitionDirection;
	bool popupActive;

	void deleteAllLayers();

	int32_t encodeText(std::string_view newText, uint8_t* destination, bool alignRight, uint8_t drawDot = 255,
	                   bool limitToDisplayLength = true, int32_t scrollPos = 0);
	void replaceBottomLayer(NumericLayer* newLayer);
	void setTopLayer(NumericLayer* newTopLayer);
	void transitionToNewLayer(NumericLayer* newLayer);
	void setTextVeryBasicA1(char const* text);
};
