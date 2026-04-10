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
#include <string_view>
#ifdef __cplusplus
#include "definitions_cxx.hpp"
#include "display.h"
#include "oled_canvas/canvas.h"
#include <sys/types.h>
#include <vector>

#define OLED_LOG_TIMING (0 && ENABLE_TEXT_OUTPUT)

#if OLED_LOG_TIMING
#include "io/debug/log.h"
#endif

namespace deluge::hid::display {

struct Icon {
	const uint8_t* data;
	uint8_t width;
	uint8_t height;
	uint8_t numBytesTall;
};

class OLED : public Display {
public:
	OLED() : Display(DisplayType::OLED) {
		if (l10n::chosenLanguage == nullptr || l10n::chosenLanguage == &l10n::built_in::seven_segment) {
			l10n::chosenLanguage = &l10n::built_in::english;
		}
	}

	/// Clear the canvas currently being used as the main image.
	///
	/// This also stops any active scrolling and blinking animations.
	///
	/// Marks the OLED as dirty, so you don't need to do that later yourself.
	static void clearMainImage();

	static void setupBlink(int32_t minX, int32_t width, int32_t minY, int32_t maxY, bool shouldBlinkImmediately);
	static void stopBlink();

	static void sendMainImage();

	static void setupPopup(PopupType type, int32_t width, int32_t height, std::optional<int32_t> startX = std::nullopt,
	                       std::optional<int32_t> startY = std::nullopt);
	static void removePopup();
	static void popupText(std::string_view text, bool persistent = false, PopupType type = PopupType::GENERAL);
	static bool isPopupPresent();
	static bool isPopupPresentOfType(PopupType type = PopupType::GENERAL);
	static bool isPermanentPopupPresent();

	static void displayWorkingAnimation(std::string_view word);

	static int32_t setupConsole(int32_t height);
	static void drawConsoleTopLine();

	static void stopScrollingAnimation();
	static void setupSideScroller(int32_t index, std::string_view text, int32_t startX, int32_t endX, int32_t startY,
	                              int32_t endY, int32_t textSpacingX, int32_t textSizeY, bool doHighlight);
	static void drawPermanentPopupLookingText(std::string_view);

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
	static const uint8_t midiIcon[];
	static const uint8_t midiIconPt2[];
	static const uint8_t downArrowIcon[];
	static const uint8_t rightArrowIcon[];
	static const uint8_t lockIcon[];
	static const uint8_t checkedBoxIcon[];
	static const uint8_t uncheckedBoxIcon[];
	static const uint8_t submenuArrowIcon[];
	static const uint8_t submenuArrowIconBold[];
	static const uint8_t metronomeIcon[];
	static const Icon sineIcon;
	static const Icon squareIcon;
	static const Icon sawIcon;
	static const Icon triangleIcon;
	static const Icon sampleHoldIcon;
	static const Icon randomWalkIcon;
	static const Icon warblerIcon;
	static const Icon syncTypeEvenIcon;
	static const Icon syncTypeDottedIcon;
	static const Icon syncTypeTripletsIcon;
	static const Icon switcherIconOff;
	static const Icon switcherIconOn;
	static const Icon arpModeIconUp;
	static const Icon arpModeIconUpDown;
	static const Icon arpModeIconWalk;
	static const Icon arpModeIconCustom;
	static const Icon lockedIconBig;
	static const Icon unlockedIconBig;
	static const Icon diceIcon;
	static const Icon directionIcon;
	static const Icon knobArcIcon;
	static const Icon infinityIcon;
	static const Icon sampleIcon;
	static const Icon wavetableIcon;
	static const Icon inputIcon;
	static const Icon micIcon;
	static const Icon folderIconBig;
	static const Icon loopPointIcon;
	static const Icon sampleModeCutIcon;
	static const Icon sampleModeOnceIcon;
	static const Icon sampleModeLoopIcon;
	static const Icon sampleModeStretchIcon;
	static const Icon keyboardIcon;
	static const Icon crossedOutKeyboardIcon;

	void removeWorkingAnimation() override;
	void timerRoutine() override;
	void consoleText(std::string_view text) override;
	void freezeWithError(std::string_view) override;

	//************************ Display Interface stuff ***************************/

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }

	void displayPopup(std::string_view newText, int8_t numFlashes = 3, bool = false, uint8_t = 255, int32_t = 1,
	                  PopupType type = PopupType::GENERAL) override {
		popupText(newText, !numFlashes, type);
	}

	void popupText(std::string_view text, PopupType type = PopupType::GENERAL) override { popupText(text, true, type); }
	void popupTextTemporary(std::string_view text, PopupType type = PopupType::GENERAL) override {
		popupText(text, false, type);
	}

	void cancelPopup() override { removePopup(); }
	bool isLayerCurrentlyOnTop(NumericLayer* layer) override { return (!this->hasPopup()); }
	void displayError(Error error) override;

	// Loading animations
	void displayLoadingAnimationText(std::string_view text, bool delayed = false, bool transparent = false) override {
		displayWorkingAnimation(text);
	}
	void removeLoadingAnimation() override { removeWorkingAnimation(); }

	bool hasPopup() override { return isPopupPresent(); }
	bool hasPopupOfType(PopupType type) override { return isPopupPresentOfType(type); }

	// Horizontal menus
	void displayNotification(std::string_view param_title, std::optional<std::string_view> param_value) override;

private:
	static bool needsSending;
};

} // namespace deluge::hid::display
#endif
