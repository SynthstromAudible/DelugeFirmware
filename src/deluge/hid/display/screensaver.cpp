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

/// Starscape frame interval. For comparison, scrolling text re-arms at 15ms and
/// 5ms (see OLED::scrollingAndBlinkingTimerEvent), so this is the lighter load.
constexpr int32_t kFrameIntervalMS = 50;

/// Blank re-check interval while already showing. Blank has no per-frame animation
/// to drive, so this exists solely so a popup or working animation that appears
/// while we're showing gets noticed -- without pushing an unchanged frame every tick.
constexpr int32_t kInhibitRecheckMS = 1000;

constexpr int32_t kMillisecondsPerMinute = 60000;

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

	// Never cover a message the display is asking the user to read. Re-checked every
	// tick, not just at activation, so a card error or loading animation that appears
	// while we're showing gets the screen back.
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
		starfield_.scatter();
		changed = true;
	}
	else if (FlashStorage::screensaverMode == ScreensaverMode::STARSCAPE) {
		starfield_.advance();
		changed = true;
	}
	// BLANK while already showing: nothing changed, this tick is only an inhibitor re-check.

	if (changed) {
		render();
		OLED::markChanged();
	}

	uiTimerManager.setTimer(TimerName::SCREENSAVER, FlashStorage::screensaverMode == ScreensaverMode::STARSCAPE
	                                                    ? kFrameIntervalMS
	                                                    : kInhibitRecheckMS);
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

	if (FlashStorage::screensaverMode != ScreensaverMode::STARSCAPE) {
		return; // BLANK: a cleared canvas is the whole picture.
	}

	for (size_t i = 0; i < Starfield::kNumStars; i++) {
		const Starfield::Projected star = starfield_.project(i);
		for (int32_t dy = 0; dy < star.size; dy++) {
			for (int32_t dx = 0; dx < star.size; dx++) {
				const int32_t x = star.x + dx;
				const int32_t y = star.y + dy;
				// Rows 0-4 are off-panel, so clip at OLED_MAIN_TOPMOST_PIXEL.
				if (x >= 0 && x < OLED_MAIN_WIDTH_PIXELS && y >= OLED_MAIN_TOPMOST_PIXEL
				    && y < OLED_MAIN_HEIGHT_PIXELS) {
					canvas_.drawPixel(x, y);
				}
			}
		}
	}
}

} // namespace deluge::hid::display
