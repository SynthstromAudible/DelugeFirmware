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
#include "gui/menu_item/integer.h"
#include "gui/menu_item/patched_param/integer.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/menu_item/zone_based.h"
#include "gui/ui/sound_editor.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"
#include "util/d_string.h"
#include <cstdint>
#include <hid/buttons.h>
#include <hid/display/display.h>
#include <hid/display/oled.h>
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
		if (getP() == params::LOCAL_TABLE_SHAPER_MIX) {
			return params::UNPATCHED_TABLE_SHAPER_MIX;
		}
		if (getP() == params::LOCAL_SINE_SHAPER_DRIVE) {
			return params::UNPATCHED_SINE_SHAPER_DRIVE;
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

/// Harmonic zone control - 8 zones with triangle-modulated Chebyshev harmonics
/// Secret menu: Push+twist encoder to adjust harmonicPhaseOffset (per-patch phase offset)
/// Press encoder (no twist): Opens mod matrix source selection
class SineShaperHarmonic final : public ZoneBasedDualParam<params::LOCAL_SINE_SHAPER_HARMONIC> {
public:
	using ZoneBasedDualParam::ZoneBasedDualParam;

	[[nodiscard]] q31_t getFieldValue() const override {
		return soundEditor.currentModControllable->sineShaper.harmonic;
	}

	void setFieldValue(q31_t value) override { soundEditor.currentModControllable->sineShaper.harmonic = value; }

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		switch (zoneIndex) {
		case 0:
			return "3579";
		case 1:
			return "3579wm";
		case 2:
			return "FM";
		case 3:
			return "Fold";
		case 4:
			return "Ring";
		case 5:
			return "Add";
		case 6:
			return "Mod";
		case 7:
			return "Poly";
		default:
			return "?";
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust harmonicPhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->sineShaper.harmonicPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 0.1f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "offset:%d", static_cast<int32_t>(phase * 10.0f));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			ZoneBasedDualParam::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

private:
	mutable bool suppressNotification_ = false;
};

/// Twist zone control - 8 zones with different modifiers
/// When twistPhaseOffset or gammaPhase > 0: all zones become meta/twist zones (shows "p:z" coords)
/// When phaseOffset == 0: zones 0-3 are special (Width, Evens, Rect, Fdbk), zones 4-7 are twist
/// Secret menu: Push+twist encoder to adjust twistPhaseOffset (per-patch phase offset)
/// Press encoder (no twist): Opens mod matrix source selection
class SineShaperTwist final : public ZoneBasedDualParam<params::LOCAL_SINE_SHAPER_TWIST> {
	static constexpr int32_t kTwistResolution = 1024;
	static constexpr int32_t kTwistNumZones = 8;

public:
	using ZoneBasedDualParam::ZoneBasedDualParam;

	[[nodiscard]] q31_t getFieldValue() const override { return soundEditor.currentModControllable->sineShaper.twist; }

	void setFieldValue(q31_t value) override { soundEditor.currentModControllable->sineShaper.twist = value; }

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		// Zone names only shown when phaseOffset == 0 (special mode)
		switch (zoneIndex) {
		case 0:
			return "Width";
		case 1:
			return "Evens";
		case 2:
			return "Rect";
		case 3:
			return "Fdbk";
		case 4:
			return "Twist1";
		case 5:
			return "Twist2";
		case 6:
			return "Twist3";
		case 7:
			return "Twist4";
		default:
			return "---";
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust twistPhaseOffset (same scale as gammaPhase for consistency)
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->sineShaper.twistPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 1.0f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "T:%d", static_cast<int32_t>(phase));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			ZoneBasedDualParam::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	// Override rendering to show numeric coordinates when phase offset > 0
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		float phase_offset = effectivePhaseOffset();
		if (phase_offset != 0.0f) {
			cacheCoordDisplay(phase_offset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kTwistResolution, kTwistNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kTwistResolution, kTwistNumZones,
			                           [this](int32_t z) { return this->getZoneName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		float phase_offset = effectivePhaseOffset();
		if (phase_offset != 0.0f) {
			cacheCoordDisplay(phase_offset, this->getValue());
			drawZoneForOled(this->getValue(), kTwistResolution, kTwistNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kTwistResolution, kTwistNumZones,
			                [this](int32_t z) { return this->getZoneName(z); });
		}
	}

private:
	mutable bool suppressNotification_ = false;

	/// Effective phase offset: twistPhaseOffset + kResolution * gammaPhase
	/// When > 0, all zones become meta/twist zones with numeric display
	[[nodiscard]] float effectivePhaseOffset() const {
		auto& ss = soundEditor.currentModControllable->sineShaper;
		return ss.twistPhaseOffset + static_cast<float>(kTwistResolution) * ss.gammaPhase;
	}

	static inline char coord_buffer_[12] = {};
	static void cacheCoordDisplay(float phase_offset, int32_t value) {
		int32_t p = static_cast<int32_t>(phase_offset);
		int32_t z = value >> 7; // 0-1023 → 0-7 (zone index)
		snprintf(coord_buffer_, sizeof(coord_buffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coord_buffer_; }
};

/// Mix: wet/dry blend (0-127, 0 = bypass)
/// Secret menu: Push+twist encoder to adjust gammaPhase (same scale as table shaper)
class SineShaperMix final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->sineShaper.mix); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		int32_t current_value = this->getValue();

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->sineShaper.mix = current_value;
				}
			}
		}
		else {
			soundEditor.currentModControllable->sineShaper.mix = current_value;
		}
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust gammaPhase (same scale as table shaper: 1.0 per velocity-scaled click)
			Buttons::selectButtonPressUsedUp = true;
			float& gamma = soundEditor.currentModControllable->sineShaper.gammaPhase;
			gamma = std::max(0.0f, gamma + static_cast<float>(velocity_.getScaledOffset(offset)) * 1.0f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "G:%d", static_cast<int32_t>(gamma));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			IntegerWithOff::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	[[nodiscard]] int32_t getMaxValue() const override { return 127; }

private:
	mutable VelocityEncoder velocity_;
	mutable bool suppressNotification_ = false;
};

} // namespace deluge::gui::menu_item::fx
