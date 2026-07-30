/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "hid/display/screensaver.h"
#include "definitions_cxx.hpp"
#include "gui/ui_timer_manager.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "processing/stem_export/stem_export.h"
#include "storage/flash_storage.h"

namespace deluge::hid::display {

bool Screensaver::active_ = false;
bool Screensaver::frameDirty_ = false;
oled_canvas::Canvas Screensaver::canvas_;
Starfield Screensaver::starfield_;
Rainfall Screensaver::rainfall_;

// Rainfall and Starfield each repeat the panel dimensions rather than including cpu_specific.h,
// which is target-only and would break the host specs. This file does see the real macros, so it
// is where the two are held together.
static_assert(Rainfall::kWidth == OLED_MAIN_WIDTH_PIXELS);
static_assert(Rainfall::kHeight == OLED_MAIN_HEIGHT_PIXELS);
static_assert(Rainfall::kTopmost == OLED_MAIN_TOPMOST_PIXEL);

// kCentreY is deliberately the truncated integer division (26.0f), not the true centre (26.5) --
// project() truncates float positions the same way, so this matches what actually renders.
static_assert(Starfield::kCentreX == OLED_MAIN_WIDTH_PIXELS / 2.0f);
static_assert(Starfield::kCentreY == (OLED_MAIN_TOPMOST_PIXEL + OLED_MAIN_HEIGHT_PIXELS) / 2);

/// Starscape frame interval. For comparison, scrolling text re-arms at 15ms and
/// 5ms (see OLED::scrollingAndBlinkingTimerEvent), so this is the lighter load.
constexpr int32_t kFrameIntervalMS = 50;

/// Blank re-check interval while already showing. Blank has no per-frame animation
/// to drive, so this exists solely so a popup or working animation that appears
/// while we're showing gets noticed -- without pushing an unchanged frame every tick.
constexpr int32_t kInhibitRecheckMS = 1000;

constexpr int32_t kMillisecondsPerMinute = 60000;

namespace {
/// @brief True for the modes that redraw every frame, as opposed to BLANK.
constexpr bool isAnimated(ScreensaverMode mode) {
	return mode == ScreensaverMode::STARSCAPE || mode == ScreensaverMode::DELUGE;
}

/// @brief Draw a filled square, clipping it to the panel.
///
/// @note Rows 0-4 are off-panel, hence the clip at OLED_MAIN_TOPMOST_PIXEL rather than zero.
void drawBlock(oled_canvas::Canvas& canvas, int32_t left, int32_t top, int32_t size) {
	for (int32_t dy = 0; dy < size; dy++) {
		for (int32_t dx = 0; dx < size; dx++) {
			const int32_t x = left + dx;
			const int32_t y = top + dy;
			if (x >= 0 && x < OLED_MAIN_WIDTH_PIXELS && y >= OLED_MAIN_TOPMOST_PIXEL && y < OLED_MAIN_HEIGHT_PIXELS) {
				canvas.drawPixel(x, y);
			}
		}
	}
}
} // namespace

void Screensaver::arm() {
	uiTimerManager.setTimer(TimerName::SCREENSAVER, FlashStorage::screensaverTimeoutMinutes * kMillisecondsPerMinute);
}

void Screensaver::noteActivity() {
	if (active_) {
		active_ = false;
		// The screensaver never writes to `main`, so this doesn't need to restore
		// anything -- it just marks the display dirty so the current `main` content
		// gets pushed again.
		OLED::markChanged();
	}

	if (FlashStorage::screensaverMode == ScreensaverMode::OFF || !::display->haveOLED()) {
		return;
	}

	arm();
}

void Screensaver::timerEvent() {
	// Qualified as ::display: inside namespace deluge::hid::display, unqualified
	// "display" resolves to this very namespace, not the global Display pointer.
	if (FlashStorage::screensaverMode == ScreensaverMode::OFF || !::display->haveOLED()) {
		return;
	}

	// Covers the drawPermanentPopupLookingText() popup class (stem export, session view,
	// automation view) plus the working animation and stem export itself. Re-checked every tick,
	// not just at activation, so one of these appearing while we're showing gets the screen back.
	//
	// Deliberately does NOT cover Display::popupText(..., persistent=true), which sets
	// oledPopupWidth instead: inhibiting on that would let a stuck persistent popup permanently
	// disable burn-in protection, which is worse than the alternative. The popup itself returns on
	// the first input regardless, and the screensaver only appears after minutes of idle, so the
	// window where it could cover one is short.
	if (OLED::isPermanentPopupPresent() || OLED::isWorkingAnimationPresent() || stemExport.processStarted) {
		if (active_) {
			active_ = false;
			OLED::markChanged();
		}
		arm();
		return;
	}

	bool changed = false;
	if (!active_) {
		active_ = true;
		switch (FlashStorage::screensaverMode) {
		case ScreensaverMode::STARSCAPE:
			starfield_.scatter();
			break;
		case ScreensaverMode::DELUGE:
			rainfall_.scatter();
			break;
		default:
			break;
		}
		changed = true;
	}
	else {
		switch (FlashStorage::screensaverMode) {
		case ScreensaverMode::STARSCAPE:
			starfield_.advance();
			changed = true;
			break;
		case ScreensaverMode::DELUGE:
			rainfall_.advance();
			changed = true;
			break;
		default:
			// BLANK while already showing: nothing changed, this tick is only an inhibitor
			// re-check.
			break;
		}
	}

	if (changed) {
		render();
		OLED::markChanged();
	}

	uiTimerManager.setTimer(TimerName::SCREENSAVER,
	                        isAnimated(FlashStorage::screensaverMode) ? kFrameIntervalMS : kInhibitRecheckMS);
}

void Screensaver::settingsChanged() {
	if (active_) {
		active_ = false;
		OLED::markChanged();
	}

	if (FlashStorage::screensaverMode == ScreensaverMode::OFF || !::display->haveOLED()) {
		uiTimerManager.unsetTimer(TimerName::SCREENSAVER);
		return;
	}

	arm();
}

void Screensaver::render() {
	canvas_.clear();
	frameDirty_ = true;

	switch (FlashStorage::screensaverMode) {
	case ScreensaverMode::STARSCAPE:
		renderStarfield();
		break;
	case ScreensaverMode::DELUGE:
		renderRainfall();
		break;
	default:
		break; // BLANK: a cleared canvas is the whole picture.
	}
}

void Screensaver::renderStarfield() {
	for (size_t i = 0; i < Starfield::kNumStars; i++) {
		const Starfield::Projected star = starfield_.project(i);
		drawBlock(canvas_, star.x, star.y, star.size);
	}
}

void Screensaver::renderRainfall() {
	for (size_t drop = 0; drop < Rainfall::kNumDrops; drop++) {
		for (size_t cell = 0; cell < rainfall_.lengthOf(drop); cell++) {
			const Rainfall::Block block = rainfall_.cellAt(drop, cell);
			drawBlock(canvas_, block.x, block.y, block.size);
		}
	}
}

} // namespace deluge::hid::display
