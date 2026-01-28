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
		const bool popup_showing = OLED::isPopupPresent();
		oled_canvas::Canvas& image = popup_showing ? OLED::popup : OLED::main;

		auto& compressor = soundEditor.currentModControllable->multibandCompressor;

		// Use full header height
		constexpr int32_t kMeterHeight = 14;
		constexpr int32_t kHalfHeight = kMeterHeight / 2;
		constexpr int32_t kBandGap = 2; // Gap between bands

		// Position - moved left since "DOTT" is short
		constexpr int32_t kMeterX = 45;
		constexpr int32_t kMeterY = OLED_MAIN_TOPMOST_PIXEL;
		const int32_t center_y = kMeterY + kHalfHeight;
		// Max y coordinate to avoid extending into UI below (truncate bottom row)
		const int32_t max_y = kMeterY + kMeterHeight - 2;

		// Scale bipolar GR value to half-height pixels
		auto scale_bipolar = [](int8_t value, int32_t max_half_height) -> int32_t {
			return (static_cast<int32_t>(value) * max_half_height) / 127;
		};

		// Scale unipolar value (0-127) to full height
		auto scale_unipolar = [](uint8_t value, int32_t max_height) -> int32_t {
			return (static_cast<int32_t>(value) * max_height) / 127;
		};

		// Draw a band meter: output level bar + GR bar + saturation indicator
		auto draw_band_meter = [&](int32_t x_pos, size_t band_index) {
			uint8_t output_level = compressor.getBandOutputLevel(band_index);
			int8_t gr_value = compressor.getBandGainReduction(band_index);
			bool saturating = compressor.isBandSaturating(band_index);

			// Bar 1: Output level (unipolar, grows upward from bottom)
			int32_t out_h = scale_unipolar(output_level, kMeterHeight);
			for (int32_t dy = 0; dy < out_h; dy++) {
				int32_t y = max_y - dy;
				if (y >= kMeterY) {
					image.drawPixel(x_pos, y);
				}
			}

			// Saturation indicator at top of output bar
			if (saturating) {
				image.drawPixel(x_pos, kMeterY);
			}

			// Bar 2: GR (bipolar, from center)
			x_pos += 1;
			int32_t h = scale_bipolar(gr_value, kHalfHeight);
			// Draw center tick
			image.drawPixel(x_pos, center_y);
			if (h > 0) {
				// Upward compression (gain boost)
				for (int32_t dy = 1; dy <= h; dy++) {
					image.drawPixel(x_pos, center_y - dy);
				}
			}
			else if (h < 0) {
				// Downward compression (gain reduction)
				for (int32_t dy = 1; dy <= -h; dy++) {
					int32_t y = center_y + dy;
					if (y <= max_y) {
						image.drawPixel(x_pos, y);
					}
				}
			}
		};

		int32_t x = kMeterX;

		// Draw L/M/H band meters (each is 2px wide: output + GR)
		draw_band_meter(x, 0); // Low
		x += 2 + kBandGap;

		draw_band_meter(x, 1); // Mid
		x += 2 + kBandGap;

		draw_band_meter(x, 2); // High
		x += 2 + kBandGap;

		// Separator (vertical dots at center)
		image.drawPixel(x, center_y - 1);
		image.drawPixel(x, center_y);
		image.drawPixel(x, center_y + 1);
		x += 2;

		// Master output level bar (2px wide for visibility)
		uint8_t out_level = compressor.getOutputLevel();
		int32_t out_h = scale_unipolar(out_level, kMeterHeight);
		for (int32_t dy = 0; dy < out_h; dy++) {
			int32_t y = max_y - dy;
			if (y >= kMeterY) {
				image.drawPixel(x, y);
				image.drawPixel(x + 1, y);
			}
		}

		// Master clip indicator - top-right of output meter (2x2 dot when clipping)
		if (compressor.isClipping()) {
			int32_t clip_x = x + 3;
			image.drawPixel(clip_x, kMeterY);
			image.drawPixel(clip_x + 1, kMeterY);
			image.drawPixel(clip_x, kMeterY + 1);
			image.drawPixel(clip_x + 1, kMeterY + 1);
		}

		OLED::markChanged();
	}
};

} // namespace deluge::gui::menu_item::submenu
