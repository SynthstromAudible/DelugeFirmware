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

#include "dsp/zone_param.hpp" // For ZoneBasedParam
#include "gui/menu_item/automation/automation.h"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/menu_item/velocity_encoder.h" // VelocityEncoder and zone render helpers
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/params/param_set.h"
#include <cstdint>

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item {

// Re-export ZoneBasedParam from dsp namespace for convenience
using dsp::ZoneBasedParam;

/// Compute bit shift for resolution (31 - log2(resolution))
/// Only valid for power-of-2 resolutions
constexpr int32_t resolutionToShift(int32_t resolution) {
	// Count trailing zeros to get log2
	int32_t shift = 31;
	while (resolution > 1) {
		resolution >>= 1;
		shift--;
	}
	return shift;
}

/// Convert q31 param value to menu value with given resolution
template <int32_t RESOLUTION>
inline int32_t paramToMenuValue(q31_t value) {
	constexpr int32_t kShift = resolutionToShift(RESOLUTION);
	constexpr q31_t kRounding = 1 << (kShift - 1);
	constexpr q31_t kOverflowThreshold = 2147483647 - kRounding;
	if (value > kOverflowThreshold) {
		return RESOLUTION;
	}
	return (value + kRounding) >> kShift;
}

/// Convert menu value to q31 param value with given resolution
template <int32_t RESOLUTION>
inline q31_t menuValueToParam(int32_t menuValue) {
	constexpr int32_t kShift = resolutionToShift(RESOLUTION);
	if (menuValue >= RESOLUTION) {
		return 2147483647; // INT32_MAX
	}
	return menuValue << kShift;
}

// Legacy aliases for 1024-step resolution (used by existing code)
constexpr int32_t kZoneHighResSteps = 1024;
inline int32_t zoneParamToMenuValue(q31_t value) {
	return paramToMenuValue<1024>(value);
}
inline q31_t zoneMenuValueToParam(int32_t menuValue) {
	return menuValueToParam<1024>(menuValue);
}

/**
 * Base class for zone-based high-resolution menu items
 *
 * Provides:
 * - Configurable resolution with velocity-sensitive encoder
 * - Zone name rendering (OLED and horizontal menu)
 * - Common display value scaling (0-50)
 *
 * Derived classes must implement:
 * - readCurrentValue() / writeCurrentValue() for storage
 * - getZoneName(int32_t) for zone labels
 * - isRelevant() if gating is needed
 *
 * @tparam NUM_ZONES Number of zones (typically 8)
 * @tparam RESOLUTION Encoder steps (typically 1024 for zone params)
 */
template <int32_t NUM_ZONES = 8, int32_t RESOLUTION = 1024>
class ZoneBasedMenuItem : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	/// Override to provide zone name for each index (0 to NUM_ZONES-1)
	[[nodiscard]] virtual const char* getZoneName(int32_t zoneIndex) const = 0;

	/// Override to provide 2-char abbreviation for 7-segment display (default: first 2 chars of zone name)
	[[nodiscard]] virtual const char* getShortZoneName(int32_t zoneIndex) const { return getZoneName(zoneIndex); }

	[[nodiscard]] int32_t getMaxValue() const override { return RESOLUTION; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	// Scale to 0-50 for display (matches gold knob popup range)
	[[nodiscard]] float getDisplayValue() override { return (this->getValue() * 50.0f) / RESOLUTION; }

	void selectEncoderAction(int32_t offset) override {
		DecimalWithoutScrolling::selectEncoderAction(velocity_.getScaledOffset(offset));
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		// Capture 'this' to call virtual getZoneName
		renderZoneInHorizontalMenu(slot, this->getValue(), RESOLUTION, NUM_ZONES,
		                           [this](int32_t z) { return this->getZoneName(z); });
	}

protected:
	void drawPixelsForOled() override {
		drawZoneForOled(this->getValue(), RESOLUTION, NUM_ZONES, [this](int32_t z) { return this->getZoneName(z); });
	}

	// 7-segment: show zone abbreviation + position (e.g., "SY50" for Sync at 50%)
	void drawActualValue(bool justDidHorizontalScroll = false) override {
		int32_t zoneWidth = RESOLUTION / NUM_ZONES;
		int32_t zoneIndex = std::min(this->getValue() / zoneWidth, NUM_ZONES - 1);
		int32_t posInZone = this->getValue() - (zoneIndex * zoneWidth);
		int32_t posPercent = (posInZone * 99) / zoneWidth; // 0-99

		const char* abbrev = getShortZoneName(zoneIndex);
		char buffer[5];
		// Take first 2 chars of abbreviation
		buffer[0] = abbrev[0];
		buffer[1] = (abbrev[1] != '\0') ? abbrev[1] : ' ';
		// Add 2-digit position
		buffer[2] = '0' + (posPercent / 10);
		buffer[3] = '0' + (posPercent % 10);
		buffer[4] = '\0';
		display->setText(buffer);
	}

	mutable VelocityEncoder velocity_;
};

/**
 * Zone-based menu item backed by an unpatched param
 *
 * Provides CC learning and reads/writes via UnpatchedParamSet.
 *
 * @tparam PARAM_ID The UnpatchedShared param ID
 * @tparam NUM_ZONES Number of zones (derived from PARAM_ID via getZoneParamInfo)
 * @tparam RESOLUTION Encoder steps (derived from PARAM_ID via getZoneParamInfo)
 */
template <params::UnpatchedShared PARAM_ID, int32_t NUM_ZONES = params::getZoneParamInfo(PARAM_ID).zoneCount,
          int32_t RESOLUTION = params::getZoneParamInfo(PARAM_ID).resolution>
class ZoneBasedUnpatchedParam : public ZoneBasedMenuItem<NUM_ZONES, RESOLUTION>,
                                public MenuItemWithCCLearning,
                                public Automation {
public:
	using ZoneBasedMenuItem<NUM_ZONES, RESOLUTION>::ZoneBasedMenuItem;

	void readCurrentValue() override {
		q31_t value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(PARAM_ID);
		this->setValue(paramToMenuValue<RESOLUTION>(value));
	}

	void writeCurrentValue() override {
		q31_t value = menuValueToParam<RESOLUTION>(this->getValue());
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
		ModelStackWithAutoParam* modelStackWithParam = modelStack->getUnpatchedAutoParamFromId(PARAM_ID);
		modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(PARAM_ID + params::UNPATCHED_START);
		return paramDescriptor;
	}

	// Automation interface for gold knob
	ModelStackWithAutoParam* getModelStackWithParam(void* memory) override {
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
		return modelStack->getUnpatchedAutoParamFromId(PARAM_ID);
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}
};

/**
 * Zone-based menu item backed by a field on ModControllableAudio
 *
 * No CC learning by default. Derived class implements read/write
 * to access the specific field.
 *
 * @tparam NUM_ZONES Number of zones (typically 8)
 */
template <int32_t NUM_ZONES = 8>
class ZoneBasedFieldItem : public ZoneBasedMenuItem<NUM_ZONES> {
public:
	using ZoneBasedMenuItem<NUM_ZONES>::ZoneBasedMenuItem;
	// Derived class must implement readCurrentValue/writeCurrentValue
};

/**
 * Zone-based menu item with patched param for mod matrix routing
 *
 * Design: Menu controls a FIELD on the sound (base value), patched param provides
 * pure modulation (neutral = 0). DSP combines: field + scaledMod.
 *
 * Provides:
 * - Zone-based display with configurable resolution
 * - Mod matrix routing via PatchedParam (press encoder to access sources)
 * - CC learning for the patched param
 *
 * Derived class must implement:
 * - getZoneName(int32_t) for zone labels
 * - getFieldValue() / setFieldValue() for the sound's field
 *
 * @tparam PARAM_ID The patched param ID for mod routing (LOCAL or GLOBAL zone param)
 * @tparam NUM_ZONES Number of zones (derived from PARAM_ID via getZoneParamInfo)
 * @tparam RESOLUTION Encoder steps (derived from PARAM_ID via getZoneParamInfo)
 */
template <params::ParamType PARAM_ID, int32_t NUM_ZONES = params::getZoneParamInfo(PARAM_ID).zoneCount,
          int32_t RESOLUTION = params::getZoneParamInfo(PARAM_ID).resolution>
class ZoneBasedPatchedParam : public DecimalWithoutScrolling, public MenuItemWithCCLearning, public Automation {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	/// Override to provide zone name for each index (0 to NUM_ZONES-1)
	[[nodiscard]] virtual const char* getZoneName(int32_t zoneIndex) const = 0;

	/// Override to provide 2-char abbreviation for 7-segment display (default: first 2 chars of zone name)
	[[nodiscard]] virtual const char* getShortZoneName(int32_t zoneIndex) const { return getZoneName(zoneIndex); }

	/// Override to get the field value from the sound (q31_t, 0 to ONE_Q31)
	/// Default returns 0 - override if you have a separate field to sync
	[[nodiscard]] virtual q31_t getFieldValue() const { return 0; }

	/// Override to set the field value on the sound
	/// Default does nothing - override if you have a separate field to sync
	virtual void setFieldValue([[maybe_unused]] q31_t value) {}

	[[nodiscard]] int32_t getMaxValue() const override { return RESOLUTION; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	// Scale to 0-50 for display (matches gold knob popup range)
	[[nodiscard]] float getDisplayValue() override { return (this->getValue() * 50.0f) / RESOLUTION; }

	void selectEncoderAction(int32_t offset) override {
		DecimalWithoutScrolling::selectEncoderAction(velocity_.getScaledOffset(offset));
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		renderZoneInHorizontalMenu(slot, this->getValue(), RESOLUTION, NUM_ZONES,
		                           [this](int32_t z) { return this->getZoneName(z); });
	}

	// === PatchedParam interface for mod matrix routing ===
	[[nodiscard]] int32_t getP() const { return PARAM_ID; }

	MenuItem* selectButtonPress() override {
		// If shift held down, user wants to delete automation
		if (Buttons::isShiftButtonPressed()) {
			return DecimalWithoutScrolling::selectButtonPress();
		}
		// Press encoder (without twist) opens mod matrix source selection
		soundEditor.patchingParamSelected = PARAM_ID;
		return &source_selection::regularMenu;
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		paramDescriptor.setToHaveParamOnly(PARAM_ID);
		return paramDescriptor;
	}

	[[nodiscard]] deluge::modulation::params::Kind getParamKind() const {
		return deluge::modulation::params::Kind::PATCHED;
	}

	// Automation interface for gold knob (returns patched param for modulation automation)
	ModelStackWithAutoParam* getModelStackWithParam(void* memory) override {
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
		return modelStack->getPatchedAutoParamFromId(PARAM_ID);
	}

	// Read from patched param preset (automation/gold knob modify this)
	void readCurrentValue() override {
		q31_t value = soundEditor.currentParamManager->getPatchedParamSet()->getValue(PARAM_ID);
		this->setValue(paramToMenuValue<RESOLUTION>(value));
	}

	// Write to patched param preset
	void writeCurrentValue() override {
		q31_t value = menuValueToParam<RESOLUTION>(this->getValue());
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(modelStackMemory);
		ModelStackWithAutoParam* modelStackWithParam = modelStack->getPatchedAutoParamFromId(PARAM_ID);
		modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

protected:
	void drawPixelsForOled() override {
		drawZoneForOled(this->getValue(), RESOLUTION, NUM_ZONES, [this](int32_t z) { return this->getZoneName(z); });
	}

	// 7-segment: show zone abbreviation + position (e.g., "SY50" for Sync at 50%)
	void drawActualValue(bool justDidHorizontalScroll = false) override {
		int32_t zoneWidth = RESOLUTION / NUM_ZONES;
		int32_t zoneIndex = std::min(this->getValue() / zoneWidth, NUM_ZONES - 1);
		int32_t posInZone = this->getValue() - (zoneIndex * zoneWidth);
		int32_t posPercent = (posInZone * 99) / zoneWidth; // 0-99

		const char* abbrev = getShortZoneName(zoneIndex);
		char buffer[5];
		// Take first 2 chars of abbreviation
		buffer[0] = abbrev[0];
		buffer[1] = (abbrev[1] != '\0') ? abbrev[1] : ' ';
		// Add 2-digit position
		buffer[2] = '0' + (posPercent / 10);
		buffer[3] = '0' + (posPercent % 10);
		buffer[4] = '\0';
		display->setText(buffer);
	}

	mutable VelocityEncoder velocity_;
};

/**
 * Zone-based menu item with automatic patched/unpatched fallback support
 *
 * Extends ZoneBasedPatchedParam to work in both Sound contexts (using patched params)
 * and GlobalEffectable contexts like Kit clips and AudioClips (using unpatched params).
 * If an unpatched fallback exists (via getUnpatchedFallback()), it's used automatically.
 * If no fallback exists, behaves like ZoneBasedPatchedParam (patched only).
 *
 * @tparam PATCHED_ID The patched param ID (LOCAL or GLOBAL) for Sound contexts
 * @tparam NUM_ZONES Number of zones (derived from PATCHED_ID via getZoneParamInfo)
 * @tparam RESOLUTION Encoder steps (derived from PATCHED_ID via getZoneParamInfo)
 */
template <params::ParamType PATCHED_ID, int32_t NUM_ZONES = params::getZoneParamInfo(PATCHED_ID).zoneCount,
          int32_t RESOLUTION = params::getZoneParamInfo(PATCHED_ID).resolution>
class ZoneBasedDualParam : public ZoneBasedPatchedParam<PATCHED_ID, NUM_ZONES, RESOLUTION> {
	static constexpr int32_t UNPATCHED_ID = params::getUnpatchedFallback(PATCHED_ID);
	static constexpr bool HAS_FALLBACK = (UNPATCHED_ID >= 0);

public:
	using ZoneBasedPatchedParam<PATCHED_ID, NUM_ZONES, RESOLUTION>::ZoneBasedPatchedParam;

	void readCurrentValue() override {
		q31_t value;
		if constexpr (HAS_FALLBACK) {
			value = soundEditor.currentParamManager->getValueWithFallback(PATCHED_ID);
		}
		else {
			value = soundEditor.currentParamManager->getPatchedParamSet()->getValue(PATCHED_ID);
		}
		this->setValue(paramToMenuValue<RESOLUTION>(value));
	}

	ModelStackWithAutoParam* getModelStackWithParam(void* memory) override {
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
		if constexpr (HAS_FALLBACK) {
			if (!soundEditor.currentParamManager->containsPatchedParamSetCollection()) {
				return modelStack->getUnpatchedAutoParamFromId(UNPATCHED_ID);
			}
		}
		return modelStack->getPatchedAutoParamFromId(PATCHED_ID);
	}

	void writeCurrentValue() override {
		q31_t value = menuValueToParam<RESOLUTION>(this->getValue());
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStackMemory);
		modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);
	}

	// MIDI learning support - use appropriate param based on context
	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		if constexpr (HAS_FALLBACK) {
			if (!soundEditor.currentParamManager->containsPatchedParamSetCollection()) {
				paramDescriptor.setToHaveParamOnly(UNPATCHED_ID + params::UNPATCHED_START);
				return paramDescriptor;
			}
		}
		paramDescriptor.setToHaveParamOnly(PATCHED_ID);
		return paramDescriptor;
	}

	[[nodiscard]] deluge::modulation::params::Kind getParamKind() override {
		if constexpr (HAS_FALLBACK) {
			if (!soundEditor.currentParamManager->containsPatchedParamSetCollection()) {
				return deluge::modulation::params::Kind::UNPATCHED_SOUND;
			}
		}
		return deluge::modulation::params::Kind::PATCHED;
	}

	// Override to skip mod matrix source selection in unpatched contexts (no PatchCableSet)
	MenuItem* selectButtonPress() override {
		// If shift held down, user wants to delete automation
		if (Buttons::isShiftButtonPressed()) {
			return DecimalWithoutScrolling::selectButtonPress();
		}
		if constexpr (HAS_FALLBACK) {
			// In unpatched context (GlobalEffectable), no mod matrix available
			if (!soundEditor.currentParamManager->containsPatchedParamSetCollection()) {
				return nullptr;
			}
		}
		// In patched context (Sound), open mod matrix source selection
		soundEditor.patchingParamSelected = PATCHED_ID;
		return &source_selection::regularMenu;
	}
};

} // namespace deluge::gui::menu_item
