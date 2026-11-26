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

#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item.h"
#include "gui/menu_item/toggle.h"
#include "gui/ui/mic_velocity_calibration.h"
#include "hid/mic_velocity/mic_velocity.h"

/// Settings > Pads > Mic velocity (hid/mic_velocity/mic_velocity.h). The settings are saved on leaving the menu.
namespace deluge::gui::menu_item::mic_velocity {

/// Opens the calibration (gui/ui/mic_velocity_calibration.h)
class Calibrate final : public MenuItem {
public:
	using MenuItem::MenuItem;
	MenuItem* selectButtonPress() override {
		openUI(&micVelocityCalibration);
		return NO_NAVIGATION;
	}
	bool shouldEnterSubmenu() override { return false; }
};

/// On or off. Only there once it's been calibrated (calibrating turns it on).
class Enabled final : public Toggle {
public:
	using Toggle::Toggle;
	void readCurrentValue() override { this->setValue(deluge::mic_velocity::enabled); }
	void writeCurrentValue() override {
		deluge::mic_velocity::enabled = this->getValue();
		deluge::mic_velocity::settingsChanged();
		deluge::mic_velocity::update();
	}
	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) const override {
		return deluge::mic_velocity::calibrated();
	}
};

/// What the softest calibrated tap (and anything softer) plays at
class MinVelocity final : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 127; }
	void readCurrentValue() override { this->setValue(deluge::mic_velocity::curve.minVelocity); }
	void writeCurrentValue() override {
		deluge::mic_velocity::curve.minVelocity = this->getValue();
		deluge::mic_velocity::settingsChanged();
	}
};

/// What the hardest calibrated tap (and anything harder) plays at
class MaxVelocity final : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 127; }
	void readCurrentValue() override { this->setValue(deluge::mic_velocity::curve.maxVelocity); }
	void writeCurrentValue() override {
		deluge::mic_velocity::curve.maxVelocity = this->getValue();
		deluge::mic_velocity::settingsChanged();
	}
};

/// 0 follows the tap's loudness; up makes lighter taps play louder, down takes harder taps to get high
class Curve final : public Integer {
public:
	using Integer::Integer;
	[[nodiscard]] int32_t getMinValue() const override { return dsp::mic_velocity::VelocityCurve::kMinShape; }
	[[nodiscard]] int32_t getMaxValue() const override { return dsp::mic_velocity::VelocityCurve::kMaxShape; }
	void readCurrentValue() override { this->setValue(deluge::mic_velocity::curve.shape); }
	void writeCurrentValue() override {
		deluge::mic_velocity::curve.shape = static_cast<int8_t>(this->getValue());
		deluge::mic_velocity::settingsChanged();
	}
};

} // namespace deluge::gui::menu_item::mic_velocity
