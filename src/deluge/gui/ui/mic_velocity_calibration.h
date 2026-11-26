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

#pragma once

#include "dsp/mic_velocity/velocity_map.h"
#include "gui/ui/ui.h"
#include "hid/button.h"
#include <array>
#include <cstdint>

/// Settings > Pads > Mic velocity > Calibrate: the centre pad and the four corners, each lit in turn, tapped twice
/// softly then twice hard (20 taps). Then any pad can be tried, its velocity shown; SELECT saves the calibration and
/// turns mic velocity on, BACK leaves without saving.
class MicVelocityCalibration final : public UI {
public:
	MicVelocityCalibration() { oledShowsUIUnderneath = false; }

	bool opened() override;
	void focusRegained() override;
	ActionResult padAction(int32_t x, int32_t y, int32_t velocity) override;
	ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) override;
	void modEncoderAction(int32_t whichModEncoder, int32_t offset) override {} // The knobs don't reach the song
	void modButtonAction(uint8_t whichButton, bool on) override {}

	bool renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                    uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth], bool drawUndefinedArea) override;
	bool renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
	                   uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) override;
	void renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) override;
	UIType getUIType() override { return UIType::MIC_VELOCITY; }

	/// Whether a press on (x, y) should be measured: every main pad, while this is open
	[[nodiscard]] bool measuresPad(int32_t x, int32_t y) const { return x < kDisplayWidth; }

private:
	static constexpr int32_t kTapsPerStrength = 2;

	void finishPoint();
	void leave();
	void redraw();
	[[nodiscard]] bool tappingSoft() const { return tap_ < kTapsPerStrength; }

	bool trying_ = false;
	int32_t point_ = 0; // Which calibration point
	int32_t tap_ = 0;   // Which of its taps: soft ones, then hard
	std::array<float, kTapsPerStrength> soft_{};
	std::array<float, kTapsPerStrength> hard_{};
	deluge::dsp::mic_velocity::Calibration::PointLevels softDb_{};
	deluge::dsp::mic_velocity::Calibration::PointLevels hardDb_{};
	deluge::dsp::mic_velocity::Calibration candidate_{};

	// Trying: the last pad pressed and its velocity
	int8_t tryX_ = -1;
	int8_t tryY_ = -1;
	uint8_t tryVelocity_ = 0;
};

extern MicVelocityCalibration micVelocityCalibration;
