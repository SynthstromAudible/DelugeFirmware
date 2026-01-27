/*
 * Copyright (c) 2014-2023 Synthstrom Audible Limited
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
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include <hid/buttons.h>
#include <hid/display/display.h>
#include <limits>

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::fx {

// Helpers for dual patched/unpatched param access (Sound vs GlobalEffectable contexts)
inline q31_t getShapingParamValue(params::ParamType patched, params::ParamType unpatched) {
	if (soundEditor.currentParamManager->containsAnyMainParamCollections()) {
		return soundEditor.currentParamManager->getPatchedParamSet()->getValue(patched);
	}
	return soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(unpatched);
}

inline ModelStackWithAutoParam* getShapingModelStack(void* memory, params::ParamType patched,
                                                     params::ParamType unpatched) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	if (soundEditor.currentParamManager->containsAnyMainParamCollections()) {
		return modelStack->getPatchedAutoParamFromId(patched);
	}
	return modelStack->getUnpatchedAutoParamFromId(unpatched);
}

/// UnpatchedParam for learnable drive parameters in the shaping submenu.
/// Used in menus.cpp for sineShaperDriveMenu and shaperDriveMenu.
/// Visibility is gated at the submenu level by submenu::Shaping.
class DynamicsUnpatchedParam : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;
};

/// PatchedParam for mod-matrix-routable drive parameters in the shaping submenu.
/// Used in menus.cpp for sineShaperDriveMenu and shaperDriveMenu.
/// Uses bipolar range (-128 to +128) where 0 = unity, negative = below unity, -128 = -inf.
/// Visibility is gated at the submenu level by submenu::Shaping.
class DynamicsPatchedParam : public patched_param::Integer {
public:
	static constexpr int32_t kDriveMenuHalfRange = 128;

	using patched_param::Integer::Integer;

	/// Override selectEncoderAction to enforce our bipolar range (-128 to +128)
	/// We bypass Integer::selectEncoderAction to avoid its getMaxValue/getMinValue clamping
	void selectEncoderAction(int32_t offset) override {
		int32_t newValue = this->getValue() + offset;
		// Clamp to our bipolar range
		if (newValue > kDriveMenuHalfRange) {
			newValue = kDriveMenuHalfRange;
		}
		else if (newValue < -kDriveMenuHalfRange) {
			newValue = -kDriveMenuHalfRange;
		}
		this->setValue(newValue);

		// Trigger value write and display update (mimics Value::selectEncoderAction)
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			Buttons::selectButtonPressUsedUp = true;
		}
		writeCurrentValue();
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}

protected:
	[[nodiscard]] int32_t getMinValue() const override { return -kDriveMenuHalfRange; }
	[[nodiscard]] int32_t getMaxValue() const override { return kDriveMenuHalfRange; }

	params::ParamType getUnpatchedP() {
		// Map patched drive/mix params to their unpatched equivalents
		// (sine shaper params will be added when that effect is ported)
		if (getP() == params::LOCAL_TABLE_SHAPER_MIX) {
			return params::UNPATCHED_TABLE_SHAPER_MIX;
		}
		return params::UNPATCHED_TABLE_SHAPER_DRIVE;
	}

	void readCurrentValue() override { this->setValue(getShapingParamValue(getP(), getUnpatchedP()) >> 24); }

	ModelStackWithAutoParam* getModelStack(void* memory) override {
		return getShapingModelStack(memory, getP(), getUnpatchedP());
	}

	int32_t getFinalValue() override {
		int32_t value = this->getValue();
		if (value >= kDriveMenuHalfRange) {
			return std::numeric_limits<int32_t>::max();
		}
		else if (value <= -kDriveMenuHalfRange) {
			return std::numeric_limits<int32_t>::min();
		}
		else {
			// Scale -128..+128 back to q31 range: shift left by 24 bits
			return value << 24;
		}
	}
};

// NOTE: SineShaperHarmonic, SineShaperTwist, SineShaperMix classes will be added
// when sine shaper effect is ported (requires sineShaper field on ModControllableAudio)

} // namespace deluge::gui::menu_item::fx
