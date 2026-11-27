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

#include "gui/ui/mic_velocity_calibration.h"
#include "definitions_cxx.hpp"
#include "gui/colour/colour.h"
#include "gui/colour/palette.h"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "hid/mic_velocity/mic_velocity.h"
#include "lib/printf.h" // IWYU pragma: keep (snprintf without newlib stdio)

using namespace deluge;
using namespace deluge::dsp::mic_velocity;
namespace colours = deluge::gui::colours;

MicVelocityCalibration micVelocityCalibration{};

bool MicVelocityCalibration::opened() {
	if (!mic_velocity::micAvailable()) {
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_UNPLUG_MIC_AND_LINE_IN));
		return false;
	}
	trying_ = false;
	point_ = 0;
	tap_ = 0;
	tryX_ = tryY_ = -1;
	mic_velocity::setCalibrating(true);
	focusRegained();
	return true;
}

void MicVelocityCalibration::focusRegained() {
	redraw();
}

void MicVelocityCalibration::leave() {
	mic_velocity::setCalibrating(false);
	close();
}

void MicVelocityCalibration::finishPoint() {
	float soft = 0;
	float hard = 0;
	switch (checkPoint(soft_, hard_, &soft, &hard)) {
	case TapCheck::OK:
		softDb_[point_] = soft;
		hardDb_[point_] = hard;
		point_++;
		if (point_ == kNumCalibrationPoints) {
			candidate_ = Calibration{softDb_, hardDb_};
			trying_ = true;
		}
		break;
	case TapCheck::SOFT_UNEVEN:
	case TapCheck::HARD_UNEVEN:
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_UNEVEN_TAPS));
		break;
	case TapCheck::TOO_CLOSE:
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_SOFTER_AND_HARDER));
		break;
	}
	tap_ = 0; // This point again, or the next
}

ActionResult MicVelocityCalibration::padAction(int32_t x, int32_t y, int32_t velocity) {
	if (velocity == 0 || x >= kDisplayWidth) {
		return ActionResult::DEALT_WITH;
	}
	float const level = mic_velocity::lastPressLevelDb(); // Measured for this press, before it got here
	if (trying_) {
		tryX_ = static_cast<int8_t>(x);
		tryY_ = static_cast<int8_t>(y);
		tryVelocity_ = velocityFor(candidate_, mic_velocity::curve, x, y, level);
		redraw();
		return ActionResult::DEALT_WITH;
	}
	if (x != kCalibrationPoints[point_].x || y != kCalibrationPoints[point_].y) {
		return ActionResult::DEALT_WITH; // Only the lit pad counts
	}
	if (tappingSoft()) {
		soft_[tap_] = level;
	}
	else {
		hard_[tap_ - kTapsPerStrength] = level;
	}
	if (++tap_ == 2 * kTapsPerStrength) {
		finishPoint();
	}
	redraw();
	return ActionResult::DEALT_WITH;
}

ActionResult MicVelocityCalibration::buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine) {
	using namespace deluge::hid::button;
	if (!on) {
		return ActionResult::DEALT_WITH;
	}
	if (b == BACK) {
		leave(); // Without saving
	}
	else if (b == SELECT_ENC && trying_) {
		if (inCardRoutine) {
			return ActionResult::REMIND_ME_OUTSIDE_CARD_ROUTINE;
		}
		mic_velocity::setCalibration(candidate_);
		mic_velocity::enabled = true;
		mic_velocity::settingsChanged();
		mic_velocity::writeSettingsIfChanged();
		display->displayPopup(l10n::get(l10n::String::STRING_FOR_CALIBRATED));
		leave();
	}
	return ActionResult::DEALT_WITH;
}

void MicVelocityCalibration::redraw() {
	uiNeedsRendering(this, 0xFFFFFFFF, 0xFFFFFFFF);
	if (display->haveOLED()) {
		renderUIsForOled();
		return;
	}
	char text[8];
	if (trying_) {
		if (tryX_ >= 0) {
			display->setTextAsNumber(tryVelocity_);
			return;
		}
		snprintf(text, sizeof(text), "%s", l10n::get(l10n::String::STRING_FOR_TAP_ANY_PAD_TO_TRY));
	}
	else {
		// "SOF1": the strength and which tap, on four digits
		snprintf(text, sizeof(text), "%.3s%d",
		         l10n::get(tappingSoft() ? l10n::String::STRING_FOR_SOFT_TAP : l10n::String::STRING_FOR_HARD_TAP),
		         static_cast<int>(tap_ % kTapsPerStrength + 1));
	}
	display->setText(text);
}

bool MicVelocityCalibration::renderMainPads(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                            uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth],
                                            bool drawUndefinedArea) {
	if (image == nullptr) {
		return true;
	}
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		for (int32_t x = 0; x < kDisplayWidth; x++) {
			image[y][x] = colours::black;
		}
	}
	if (trying_) {
		if (tryX_ >= 0) {
			// The pad tried, as bright as it played
			image[tryY_][tryX_] = colours::white_full.dim(7 - tryVelocity_ * 7 / 128);
		}
		return true;
	}
	for (int32_t p = 0; p < point_; p++) { // Done
		image[kCalibrationPoints[p].y][kCalibrationPoints[p].x] = colours::green.dim(4);
	}
	image[kCalibrationPoints[point_].y][kCalibrationPoints[point_].x] = tappingSoft() ? colours::blue : colours::red;
	return true;
}

bool MicVelocityCalibration::renderSidebar(uint32_t whichRows, RGB image[][kDisplayWidth + kSideBarWidth],
                                           uint8_t occupancyMask[][kDisplayWidth + kSideBarWidth]) {
	if (image == nullptr) {
		return true;
	}
	// The taps done, as a bar up the left-hand column
	int32_t const done = trying_ ? kNumCalibrationPoints * 2 * kTapsPerStrength : point_ * 2 * kTapsPerStrength + tap_;
	int32_t const total = kNumCalibrationPoints * 2 * kTapsPerStrength;
	for (int32_t y = 0; y < kDisplayHeight; y++) {
		image[y][kDisplayWidth] = y < done * kDisplayHeight / total ? colours::green.dim(3) : colours::black;
		image[y][kDisplayWidth + 1] = colours::black;
	}
	return true;
}

void MicVelocityCalibration::renderOLED(deluge::hid::display::oled_canvas::Canvas& canvas) {
	canvas.drawScreenTitle(l10n::get(l10n::String::STRING_FOR_MIC_VELOCITY));
	// Three lines fit under the title; drawString doesn't clip, so never below the panel
	int32_t y = OLED_MAIN_TOPMOST_PIXEL + 14;
	auto line = [&](char const* text) {
		if (y + kTextSpacingY > OLED_MAIN_HEIGHT_PIXELS) {
			return;
		}
		canvas.drawString(text, 0, y, kTextSpacingX, kTextSpacingY);
		y += kTextSpacingY;
	};
	char text[32];
	if (trying_) {
		line(l10n::get(l10n::String::STRING_FOR_TAP_ANY_PAD_TO_TRY));
		if (tryX_ >= 0) {
			snprintf(text, sizeof(text), "%s %d", l10n::get(l10n::String::STRING_FOR_VELOCITY), tryVelocity_);
			line(text);
		}
		else {
			line("");
		}
		line(l10n::get(l10n::String::STRING_FOR_SELECT_SAVES));
		return;
	}
	snprintf(text, sizeof(text), "%s %d/%d", l10n::get(l10n::String::STRING_FOR_PAD_OF), static_cast<int>(point_ + 1),
	         static_cast<int>(kNumCalibrationPoints));
	line(text);
	snprintf(text, sizeof(text), "%s %d/%d",
	         l10n::get(tappingSoft() ? l10n::String::STRING_FOR_SOFT_TAP : l10n::String::STRING_FOR_HARD_TAP),
	         static_cast<int>(tap_ % kTapsPerStrength + 1), static_cast<int>(kTapsPerStrength));
	line(text);
}
