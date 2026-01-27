/*
 * Copyright (c) 2024 Synthstrom Audible Limited
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
#include "deluge/gui/l10n/l10n.h"
#include "deluge/gui/menu_item/horizontal_menu.h"
#include "gui/ui/sound_editor.h"
#include "gui/ui/ui.h"
#include "gui/ui_timer_manager.h"
#include "hid/display/oled.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "playback/playback_handler.h"
#include "util/misc.h"

namespace deluge::gui::menu_item::submenu {

using namespace deluge::hid::display;

/// Specialized HorizontalMenu for DOTT that displays a GR meter in the header.
/// The meter shows:
/// - 3 bars for Low/Mid/High band gain reduction
/// - 1 bar for output level
/// - 1 dot for clip indicator
/// The meter updates via timer callback to avoid interfering with notifications.
class CompressorHorizontalMenu final : public HorizontalMenu {
public:
	using HorizontalMenu::HorizontalMenu;

	/// Meter refresh rate (100ms = 10fps)
	static constexpr int32_t kMeterRefreshMs = 100;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		HorizontalMenu::beginSession(navigatedBackwardFrom);
		meterEnabled_ = true;
		// Start meter refresh timer
		if (display->haveOLED()) {
			uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kMeterRefreshMs);
		}
	}

	void endSession() override {
		meterEnabled_ = false;
		uiTimerManager.unsetTimer(TimerName::UI_SPECIFIC);
		HorizontalMenu::endSession();
	}

	void renderOLED() override {
		HorizontalMenu::renderOLED();

		// Draw meter as part of normal rendering when enabled and no popup
		if (meterEnabled_ && shouldShowMeter() && !OLED::isPopupPresent()) {
			renderGRMeter();
		}
	}

	ActionResult timerCallback() override {
		// Trigger a full OLED refresh to update the meter
		if (meterEnabled_ && shouldShowMeter()) {
			renderUIsForOled();
		}
		// Restart timer for continuous updates
		uiTimerManager.setTimer(TimerName::UI_SPECIFIC, kMeterRefreshMs);
		return ActionResult::DEALT_WITH;
	}

private:
	bool meterEnabled_ = false;

	/// Check if meter should be displayed
	bool shouldShowMeter() {
		if (!display->haveOLED()) {
			return false;
		}
		if (soundEditor.currentModControllable == nullptr) {
			return false;
		}
		return soundEditor.currentModControllable->multibandCompressor.isEnabled();
	}
	/// Render GR meter with dual bars per band (output level + GR)
	/// Layout: [L:out|gr] [M:out|gr] [H:out|gr] | [Master] [Clip]
	void renderGRMeter() {
		const bool popupShowing = OLED::isPopupPresent();
		oled_canvas::Canvas& image = popupShowing ? OLED::popup : OLED::main;

		auto& compressor = soundEditor.currentModControllable->multibandCompressor;

		// Use full header height
		constexpr int32_t meterHeight = 14;
		constexpr int32_t halfHeight = meterHeight / 2;
		constexpr int32_t bandGap = 2; // Gap between bands

		// Position - moved left since "DOTT" is short
		constexpr int32_t meterX = 45;
		constexpr int32_t meterY = OLED_MAIN_TOPMOST_PIXEL;
		const int32_t centerY = meterY + halfHeight;
		const int32_t bottomY = meterY + meterHeight - 1;

		// Scale bipolar GR value to half-height pixels
		auto scaleBipolar = [](int8_t value, int32_t maxHalfHeight) -> int32_t {
			return (static_cast<int32_t>(value) * maxHalfHeight) / 127;
		};

		// Scale unipolar value (0-127) to full height
		auto scaleUnipolar = [](uint8_t value, int32_t maxHeight) -> int32_t {
			return (static_cast<int32_t>(value) * maxHeight) / 127;
		};

		// Draw a band meter: output level bar + GR bar + saturation indicator
		auto drawBandMeter = [&](int32_t xPos, size_t bandIndex) {
			uint8_t outputLevel = compressor.getBandOutputLevel(bandIndex);
			int8_t grValue = compressor.getBandGainReduction(bandIndex);
			bool saturating = compressor.isBandSaturating(bandIndex);

			// Bar 1: Output level (unipolar, grows upward from bottom)
			int32_t outH = scaleUnipolar(outputLevel, meterHeight);
			for (int32_t dy = 0; dy < outH; dy++) {
				image.drawPixel(xPos, bottomY - dy);
			}

			// Saturation indicator at top of output bar
			if (saturating) {
				image.drawPixel(xPos, meterY);
			}

			// Bar 2: GR (bipolar, from center)
			xPos += 1;
			int32_t h = scaleBipolar(grValue, halfHeight);
			// Draw center tick
			image.drawPixel(xPos, centerY);
			if (h > 0) {
				// Upward compression (gain boost)
				for (int32_t dy = 1; dy <= h; dy++) {
					image.drawPixel(xPos, centerY - dy);
				}
			}
			else if (h < 0) {
				// Downward compression (gain reduction)
				for (int32_t dy = 1; dy <= -h; dy++) {
					image.drawPixel(xPos, centerY + dy);
				}
			}
		};

		int32_t x = meterX;

		// Draw L/M/H band meters (each is 2px wide: output + GR)
		drawBandMeter(x, 0); // Low
		x += 2 + bandGap;

		drawBandMeter(x, 1); // Mid
		x += 2 + bandGap;

		drawBandMeter(x, 2); // High
		x += 2 + bandGap;

		// Separator (vertical dots at center)
		image.drawPixel(x, centerY - 1);
		image.drawPixel(x, centerY);
		image.drawPixel(x, centerY + 1);
		x += 2;

		// Master output level bar (2px wide for visibility)
		uint8_t outLevel = compressor.getOutputLevel();
		int32_t outH = scaleUnipolar(outLevel, meterHeight);
		for (int32_t dy = 0; dy < outH; dy++) {
			image.drawPixel(x, bottomY - dy);
			image.drawPixel(x + 1, bottomY - dy);
		}

		// Master clip indicator - top-right of output meter (2x2 dot when clipping)
		if (compressor.isClipping()) {
			int32_t clipX = x + 3;
			image.drawPixel(clipX, meterY);
			image.drawPixel(clipX + 1, meterY);
			image.drawPixel(clipX, meterY + 1);
			image.drawPixel(clipX + 1, meterY + 1);
		}

		OLED::markChanged();
	}
};

} // namespace deluge::gui::menu_item::submenu
