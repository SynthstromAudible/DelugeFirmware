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
#include "gfx/oled/icons.h"
#include "oled_canvas/canvas.h"
#include <sys/types.h>
#include <vector>

#define OLED_LOG_TIMING (0 && ENABLE_TEXT_OUTPUT)

#if OLED_LOG_TIMING
#include "io/debug/log.h"
#endif

namespace deluge::hid::display {

class OLED : public Display {
public:
	OLED() { l10n::chosenLanguage = &l10n::built_in::english; }

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
	static void popupText(char const* text, bool persistent = false, PopupType type = PopupType::GENERAL);
	static bool isPopupPresent();
	static bool isPopupPresentOfType(PopupType type = PopupType::GENERAL);
	static bool isPermanentPopupPresent();

	/// @return True while the working/loading animation is being shown.
	static bool isWorkingAnimationPresent();

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
	static inline const Icon& sineIcon = deluge::gfx::oled::icons::sine;
	static inline const Icon& squareIcon = deluge::gfx::oled::icons::square;
	static inline const Icon& sawIcon = deluge::gfx::oled::icons::saw;
	static inline const Icon& triangleIcon = deluge::gfx::oled::icons::triangle;
	static inline const Icon& sampleHoldIcon = deluge::gfx::oled::icons::sampleHold;
	static inline const Icon& randomWalkIcon = deluge::gfx::oled::icons::randomWalk;
	static inline const Icon& warblerIcon = deluge::gfx::oled::icons::warbler;
	static inline const Icon& syncTypeEvenIcon = deluge::gfx::oled::icons::syncTypeEven;
	static inline const Icon& syncTypeDottedIcon = deluge::gfx::oled::icons::syncTypeDotted;
	static inline const Icon& syncTypeTripletsIcon = deluge::gfx::oled::icons::syncTypeTriplets;
	static inline const Icon& switcherIconOff = deluge::gfx::oled::icons::switcherOff;
	static inline const Icon& switcherIconOn = deluge::gfx::oled::icons::switcherOn;
	static inline const Icon& arpModeIconUp = deluge::gfx::oled::icons::arpModeUp;
	static inline const Icon& arpModeIconUpDown = deluge::gfx::oled::icons::arpModeUpDown;
	static inline const Icon& arpModeIconWalk = deluge::gfx::oled::icons::arpModeWalk;
	static inline const Icon& arpModeIconCustom = deluge::gfx::oled::icons::arpModeCustom;
	static inline const Icon& lockedIconBig = deluge::gfx::oled::icons::lockedBig;
	static inline const Icon& unlockedIconBig = deluge::gfx::oled::icons::unlockedBig;
	static inline const Icon& diceIcon = deluge::gfx::oled::icons::dice;
	static inline const Icon& directionIcon = deluge::gfx::oled::icons::direction;
	static inline const Icon& knobArcIcon = deluge::gfx::oled::icons::knobArc;
	static inline const Icon& infinityIcon = deluge::gfx::oled::icons::infinity;
	static inline const Icon& sampleIcon = deluge::gfx::oled::icons::sample;
	static inline const Icon& wavetableIcon = deluge::gfx::oled::icons::wavetable;
	static inline const Icon& inputIcon = deluge::gfx::oled::icons::input;
	static inline const Icon& micIcon = deluge::gfx::oled::icons::mic;
	static inline const Icon& folderIconBig = deluge::gfx::oled::icons::folderBig;
	static inline const Icon& loopPointIcon = deluge::gfx::oled::icons::loopPoint;
	static inline const Icon& sampleModeCutIcon = deluge::gfx::oled::icons::sampleModeCut;
	static inline const Icon& sampleModeOnceIcon = deluge::gfx::oled::icons::sampleModeOnce;
	static inline const Icon& sampleModeLoopIcon = deluge::gfx::oled::icons::sampleModeLoop;
	static inline const Icon& sampleModeStretchIcon = deluge::gfx::oled::icons::sampleModeStretch;
	static inline const Icon& keyboardIcon = deluge::gfx::oled::icons::keyboard;
	static inline const Icon& crossedOutKeyboardIcon = deluge::gfx::oled::icons::crossedOutKeyboard;

	void removeWorkingAnimation() override;
	void timerRoutine() override;
	void consoleText(char const* text) override;
	void freezeWithError(char const* text) override;

	//************************ Display Interface stuff ***************************/

	constexpr size_t getNumBrowserAndMenuLines() override { return 3; }

	void displayPopup(char const* newText, int8_t numFlashes = 3, bool = false, uint8_t = 255, int32_t = 1,
	                  PopupType type = PopupType::GENERAL) override {
		popupText(newText, !numFlashes, type);
	}

	void popupText(char const* text, PopupType type = PopupType::GENERAL) override { popupText(text, true, type); }
	void popupTextTemporary(char const* text, PopupType type = PopupType::GENERAL) override {
		popupText(text, false, type);
	}

	void cancelPopup() override { removePopup(); }
	void displayError(Error error) override;

	// Loading animations
	void displayLoadingAnimationText(char const* text, bool delayed = false, bool transparent = false) override {
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
