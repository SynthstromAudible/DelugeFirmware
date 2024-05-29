/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "gui/l10n/language.h"
#ifdef __cplusplus
#include "definitions_cxx.hpp"
#include "display.h"
#include "oled_canvas/canvas.h"

#define OLED_LOG_TIMING (0 && ENABLE_TEXT_OUTPUT)

#if OLED_LOG_TIMING
#include "io/debug/log.h"
#endif

namespace deluge::hid::display {
class OLED : public Display {
public:
	OLED() : Display(DisplayType::OLED) {
		if (l10n::chosenLanguage == nullptr || l10n::chosenLanguage == &l10n::built_in::seven_segment) {
			l10n::chosenLanguage = &l10n::built_in::english;
		}
	}

	/// Clear the canvas currently being used as the main image.
	///
	/// Marks the OLED as dirty, so you don't need to do that later yourself.
	static void clearMainImage();

	static void setupBlink(int32_t minX, int32_t width, int32_t minY, int32_t maxY, bool shouldBlinkImmediately);
	static void stopBlink();

	static void sendMainImage();

	static void setupPopup(int32_t width, int32_t height);
	static void removePopup();
	static void popupText(char const* text, bool persistent = false, DisplayPopupType type = DisplayPopupType::GENERAL);
	static bool isPopupPresent();
	static bool isPopupPresentOfType(DisplayPopupType type = DisplayPopupType::GENERAL);

	static void displayWorkingAnimation(char const* word);

	static int32_t setupConsole(int32_t height);
	static void drawConsoleTopLine();

	static void stopScrollingAnimation();
	static void setupSideScroller(int32_t index, std::string_view text, int32_t startX, int32_t endX, int32_t startY,
	                              int32_t endY, int32_t textSpacingX, int32_t textSizeY, bool doHighlight);
	static void drawPermanentPopupLookingText(char const* text);

	/// Call this after doing any rendering work so the next trip through the UI rendering loop actually sends the image
	/// via \ref sendMainImage.
	static void markChanged() {
#if OLED_LOG_TIMING
		if (!needsSending) {
			D_PRINTLN("Fresh dirty mark");
		}
#endif
		needsSending = true;
	}

	void consoleTimerEvent();
	static void scrollingAndBlinkingTimerEvent();

	static void renderEmulated7Seg(const std::array<uint8_t, kNumericDisplayLength>& display);

	static oled_canvas::Canvas main;
	static oled_canvas::Canvas popup;
	static oled_canvas::Canvas console;

	// pointer to one of the three above (the one currently displayed)
	static uint8_t (*oledCurrentImage)[OLED_MAIN_WIDTH_PIXELS];

	static const uint8_t folderIcon[];
	static const uint8_t waveIcon[];
	static const uint8_t songIcon[];
	static const uint8_t synthIcon[];
	static const uint8_t kitIcon[];
	static const uint8_t downArrowIcon[];
	static const uint8_t rightArrowIcon[];
	static const uint8_t lockIcon[];

	void removeWorkingAnimation() override;
	void timerRoutine() override;
	void consoleText(char const* text) override;
	void freezeWithError(char const* text) override;

	//************************ Display Interface stuff ***************************/

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }

	void displayPopup(char const* newText, int8_t numFlashes = 3, bool = false, uint8_t = 255, int32_t = 1,
	                  DisplayPopupType type = DisplayPopupType::GENERAL) override {
		popupText(newText, !numFlashes, type);
	}

	void popupText(char const* text, DisplayPopupType type = DisplayPopupType::GENERAL) override {
		popupText(text, true, type);
	}
	void popupTextTemporary(char const* text, DisplayPopupType type = DisplayPopupType::GENERAL) override {
		popupText(text, false, type);
	}

	void cancelPopup() override { removePopup(); }
	bool isLayerCurrentlyOnTop(NumericLayer* layer) override { return (!this->hasPopup()); }
	void displayError(Error error) override;

	// Loading animations
	void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) override {
		displayWorkingAnimation(text);
	}
	void removeLoadingAnimation() override { removeWorkingAnimation(); }

	bool hasPopup() override { return isPopupPresent(); }
	bool hasPopupOfType(DisplayPopupType type) override { return isPopupPresentOfType(type); }

private:
	static bool needsSending;
};

} // namespace deluge::hid::display
#endif
