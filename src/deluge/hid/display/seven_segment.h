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
#include "gui/l10n/language.h"
#include "hid/display/display.h"
#include "hid/display/numeric_layer/numeric_layer_basic_text.h"
#include <array>
#include <string>

class NumericLayerScrollingText;

namespace deluge::hid::display {

class SevenSegment : public Display {
public:
	SevenSegment() : Display(DisplayType::SEVENSEG) { l10n::chosenLanguage = &l10n::built_in::seven_segment; }

	void setText(std::string_view newText, bool alignRight = false, uint8_t drawDot = 255, bool doBlink = false,
	             uint8_t* newBlinkMask = nullptr, bool blinkImmediately = false, bool shouldBlinkFast = false,
	             int32_t scrollPos = 0, uint8_t* blinkAddition = nullptr, bool justReplaceBottomLayer = false) override;

	void setNextTransitionDirection(int8_t thisDirection) override;
	void displayPopup(char const* newText, int8_t numFlashes = 3, bool alignRight = false, uint8_t drawDot = 255,
	                  int32_t blinkSpeed = 1, PopupType type = PopupType::GENERAL) override;
	void freezeWithError(char const* text) override;
	void cancelPopup() override;
	void displayError(Error error) override;

	void setTextAsNumber(int16_t number, uint8_t drawDot = 255, bool doBlink = false) override;
	void setTextAsSlot(int16_t currentSlot, int8_t currentSubSlot, bool currentSlotExists, bool doBlink = false,
	                   int32_t blinkPos = -1, bool blinkImmediately = false) override;
	void setTextWithMultipleDots(std::string_view newText, std::vector<uint8_t> dotPositions, bool alignRight = false,
	                             bool doBlink = false, uint8_t* newBlinkMask = nullptr,
	                             bool blinkImmediately = false) override;

	void timerRoutine() override;
	void removeTopLayer();
	NumericLayerScrollingText* setScrollingText(char const* newText, int32_t startAtPos = 0, int32_t initialDelay = 600,
	                                            int count = -1, uint8_t fixedDot = 255);
	int32_t getEncodedPosFromLeft(int32_t textPos, char const* text, bool* andAHalf) override;
	void render();
	void displayLoadingAnimation(bool delayed = false, bool transparent = false);
	bool isLayerCurrentlyOnTop(NumericLayer* layer);
	std::array<uint8_t, kNumericDisplayLength> getLast() override { return lastDisplay_; }

	bool hasPopup() override { return this->popupActive; }
	bool hasPopupOfType(PopupType type) override { return this->popupActive && type == this->popupType; }

	constexpr size_t getNumBrowserAndMenuLines() override { return 1; }

	void consoleText(char const* text) override { SevenSegment::displayPopup(text); }
	void popupText(char const* text, PopupType type = PopupType::GENERAL) override {
		SevenSegment::displayPopup(text, 0, false, 255, 1, type);
	}
	void popupTextTemporary(char const* text, PopupType type = PopupType::GENERAL) override {
		SevenSegment::displayPopup(text, 3, false, 255, 1, type);
	}

	void removeWorkingAnimation() override {}

	// Loading animations
	void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) override {
		SevenSegment::displayLoadingAnimation(delayed, transparent);
	}
	void removeLoadingAnimation() override { SevenSegment::removeTopLayer(); }

	void enableLowercase() { use_lowercase = true; }
	void disableLowercase() { use_lowercase = false; }

private:
	NumericLayerBasicText popup;
	NumericLayer* topLayer = nullptr;
	int8_t nextTransitionDirection = 0;
	bool popupActive = false;
	PopupType popupType = PopupType::NONE;

	void deleteAllLayers();

	void innerSetText(std::string_view newText, bool alignRight = false, std::vector<uint8_t> dotPositions = {},
	                  bool doBlink = false, uint8_t* newBlinkMask = nullptr, bool blinkImmediately = false,
	                  bool shouldBlinkFast = false, int32_t scrollPos = 0, uint8_t* blinkAddition = nullptr,
	                  bool justReplaceBottomLayer = false);
	int32_t encodeText(std::string_view newText, uint8_t* destination, bool alignRight,
	                   const std::vector<uint8_t>& dotPositions = {}, bool limitToDisplayLength = true,
	                   int32_t scrollPos = 0) const;
	void replaceBottomLayer(NumericLayer* newLayer);
	void setTopLayer(NumericLayer* newTopLayer);
	void transitionToNewLayer(NumericLayer* newLayer);
	void setTextVeryBasicA1(char const* text);
	std::array<uint8_t, kNumericDisplayLength> lastDisplay_ = {0};
	bool use_lowercase = false;
};
} // namespace deluge::hid::display
