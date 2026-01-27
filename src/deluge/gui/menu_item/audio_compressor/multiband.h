/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include <cstdio>
#include <cstring>

#include "definitions_cxx.hpp"
#include "dsp/compressor/multiband.h"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/menu_item/velocity_encoder.h"
#include "gui/menu_item/zone_based.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "modulation/params/param_descriptor.h"
#include "modulation/params/param_set.h"

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::audio_compressor {

// Helper to convert param value (0 to INT32_MAX) to menu value (0 to 128)
// Handles overflow that would occur when adding rounding term to values near INT32_MAX
inline int32_t paramToMenuValue128(q31_t value) {
	// Values above this threshold would overflow when adding (1 << 23)
	constexpr q31_t kOverflowThreshold = 2147483647 - (1 << 23); // INT32_MAX - rounding term
	if (value > kOverflowThreshold) {
		return 128;
	}
	return (value + (1 << 23)) >> 24;
}

// High resolution: 1024 steps (8x more precision than 128)
// Used for params that display as percentages where finer control is beneficial
constexpr int32_t kHighResSteps = 1024;
constexpr int32_t kHighResShift = 21; // 31 - 10 = 21 (2^10 = 1024)

inline int32_t paramToMenuValueHighRes(q31_t value) {
	constexpr q31_t kOverflowThreshold = 2147483647 - (1 << 20); // INT32_MAX - rounding term
	if (value > kOverflowThreshold) {
		return kHighResSteps;
	}
	return (value + (1 << 20)) >> kHighResShift;
}

inline q31_t menuValueToParamHighRes(int32_t menuValue) {
	if (menuValue >= kHighResSteps) {
		return 2147483647; // INT32_MAX
	}
	return menuValue << kHighResShift;
}

/// Menu item for low crossover frequency (Hz)
/// Range: 50Hz to 2000Hz. Clamped to stay below high crossover.
class LowCrossover final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	static constexpr float kMinFreq = 50.0f;
	static constexpr float kMaxFreq = 2000.0f;
	static constexpr float kMinGap = 100.0f; // Minimum gap between low and high crossovers

	void readCurrentValue() override {
		q31_t value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(
		    params::UNPATCHED_MB_COMPRESSOR_LOW_CROSSOVER);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_LOW_CROSSOVER);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_LOW_CROSSOVER + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getLowCrossoverHz();
	}

	const char* getUnit() override { return "HZ"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Menu item for high crossover frequency (Hz)
/// Range: 200Hz to 8000Hz. Clamped to stay above low crossover.
/// Click encoder to toggle soft clipping on/off.
class HighCrossover final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	static constexpr float kMinFreq = 200.0f; // Fixed minimum for consistent knob feel
	static constexpr float kMaxFreq = 8000.0f;
	static constexpr float kMinGap = 100.0f; // Minimum gap between low and high crossovers

	void readCurrentValue() override {
		q31_t value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(
		    params::UNPATCHED_MB_COMPRESSOR_HIGH_CROSSOVER);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_HIGH_CROSSOVER);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_HIGH_CROSSOVER + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getHighCrossoverHz();
	}

	const char* getUnit() override { return "HZ"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Linked threshold control - sets threshold for all bands simultaneously
class LinkedThreshold final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		q31_t value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(
		    params::UNPATCHED_MB_COMPRESSOR_THRESHOLD);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_THRESHOLD);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_THRESHOLD + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getBand(0).getThresholdForDisplay();
	}

	const char* getUnit() override { return "DB"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return BAR; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Linked ratio control - sets ratio for all bands simultaneously
class LinkedRatio final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		q31_t value =
		    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_MB_COMPRESSOR_RATIO);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_RATIO);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getBand(0).getRatioForDisplay();
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_RATIO + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	const char* getUnit() override { return " : 1"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Linked attack control - sets attack for all bands simultaneously
class LinkedAttack final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		q31_t value =
		    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_MB_COMPRESSOR_ATTACK);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_ATTACK);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getBand(0).getAttackMS();
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_ATTACK + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	const char* getUnit() override { return "MS"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return ATTACK; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Linked release control - sets release for all bands simultaneously
class LinkedRelease final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		q31_t value =
		    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_MB_COMPRESSOR_RELEASE);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_RELEASE);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getBand(0).getReleaseMS();
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_RELEASE + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	const char* getUnit() override { return "MS"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return RELEASE; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Character control (replaces knee) - controls width, knee, timing, skew via zones
/// Zones: Width, Timing, Skew, Punch, Air, Rich, OTT, OWLTT
/// Secret menu: push+turn encoder to adjust feel meta phase offset
class Character final : public ZoneBasedUnpatchedParam<params::UNPATCHED_MB_COMPRESSOR_CHARACTER> {
public:
	using ZoneBasedUnpatchedParam::ZoneBasedUnpatchedParam;

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		static constexpr const char* kNames[] = {"Width", "Timing", "Skew", "Punch", "Air", "Rich", "OTT", "OWLTT"};
		return (zoneIndex >= 0 && zoneIndex < 8) ? kNames[zoneIndex] : "?";
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}

	// Override to add secret menu for feel meta phase adjustment
	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust feelPhaseOffset (gated ≥0 for fast floor optimization)
			Buttons::selectButtonPressUsedUp = true;
			auto& comp = soundEditor.currentModControllable->multibandCompressor;
			float phase = comp.getFeelPhaseOffset();
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 0.1f);
			comp.setFeelPhaseOffset(phase);
			// Show current value on display
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "offset:%d", static_cast<int32_t>(phase * 10.0f));
			display->displayPopup(buffer);
			renderUIsForOled(); // Refresh display for consistency
			suppressNotification_ = true;
		}
		else {
			ZoneBasedUnpatchedParam::selectEncoderAction(offset);
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

/// Up/Down ratio skew control (balance between upward and downward compression)
class UpDownSkew final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		q31_t value =
		    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_MB_COMPRESSOR_SKEW);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_SKEW);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_SKEW + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Vibe control - controls phase relationships between oscillations in Feel
/// Zones: Sync, Spread, Pairs, Cascade, Invert, Pulse, Drift, Twist
/// When vibePhaseOffset > 0: Full phi-triangle evolution across ALL zones, shows coordinates
/// Secret menu: push+turn encoder to adjust twist phase offset
class Vibe final : public ZoneBasedUnpatchedParam<params::UNPATCHED_MB_COMPRESSOR_VIBE> {
public:
	using ZoneBasedUnpatchedParam::ZoneBasedUnpatchedParam;

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override {
		static constexpr const char* kNames[] = {"Sync",   "Spread", "Pairs", "Cascade",
		                                         "Invert", "Pulse",  "Drift", "Twist"};
		return (zoneIndex >= 0 && zoneIndex < 8) ? kNames[zoneIndex] : "?";
	}

	[[nodiscard]] const char* getShortZoneName(int32_t zoneIndex) const override {
		static constexpr const char* kNames[] = {"SY", "SP", "PA", "CA", "IN", "PU", "DR", "TW"};
		return (zoneIndex >= 0 && zoneIndex < 8) ? kNames[zoneIndex] : "??";
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}

	// Override to add secret menu for twist phase adjustment
	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust vibePhaseOffset (gated ≥0 for fast floor optimization)
			Buttons::selectButtonPressUsedUp = true;
			auto& comp = soundEditor.currentModControllable->multibandCompressor;
			float phase = comp.getVibePhaseOffset();
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 0.1f);
			comp.setVibePhaseOffset(phase);
			// Show current value on display
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "offset:%d", static_cast<int32_t>(phase * 10.0f));
			display->displayPopup(buffer);
			renderUIsForOled(); // Refresh display to show updated coordinate format
			suppressNotification_ = true;
		}
		else {
			ZoneBasedUnpatchedParam::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	// Override rendering to show numeric coordinates when phaseOffset > 0
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		float phaseOffset = soundEditor.currentModControllable->multibandCompressor.getVibePhaseOffset();
		if (phaseOffset != 0.0f) {
			// When secret knob is engaged, show "P:Z" (phase:zone) with visual indicator
			cacheCoordDisplay(phaseOffset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kVibeResolution, kVibeNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kVibeResolution, kVibeNumZones,
			                           [this](int32_t z) { return this->getZoneName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		float phaseOffset = soundEditor.currentModControllable->multibandCompressor.getVibePhaseOffset();
		if (phaseOffset != 0.0f) {
			// When secret knob is engaged, show numeric coordinates
			cacheCoordDisplay(phaseOffset, this->getValue());
			drawZoneForOled(this->getValue(), kVibeResolution, kVibeNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kVibeResolution, kVibeNumZones,
			                [this](int32_t z) { return this->getZoneName(z); });
		}
	}

private:
	mutable bool suppressNotification_ = false;

	// Resolution and zone count for vibe (1024 steps, 8 zones - must match kZoneParamDefault)
	static constexpr int32_t kVibeResolution = 1024;
	static constexpr int32_t kVibeNumZones = 8;

	// Static storage for coordinate display (used by getCoordName callback)
	static inline char coordBuffer_[12] = {};
	static void cacheCoordDisplay(float phaseOffset, int32_t value) {
		// Format: "P:Z" where P=phaseOffset (int), Z=zone index (0-7)
		// 128 encoder clicks = 1 zone (1024 / 8 = 128)
		int32_t p = static_cast<int32_t>(phaseOffset * 10.0f);
		int32_t z = value >> 7; // 0-1023 → 0-7 (zone index)
		snprintf(coordBuffer_, sizeof(coordBuffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coordBuffer_; }
};

/// Global output gain control
class OutputGain final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		q31_t value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(
		    params::UNPATCHED_MB_COMPRESSOR_OUTPUT_GAIN);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_OUTPUT_GAIN);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_OUTPUT_GAIN + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	[[nodiscard]] float getDisplayValue() override {
		float linear = soundEditor.currentModControllable->multibandCompressor.getOutputGainLinear();
		// Convert to dB for display
		return 20.0f * std::log10(linear);
	}

	const char* getUnit() override { return "DB"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Per-band threshold control (parameterized by band index)
/// Displays and modifies the actual per-band threshold value
template <size_t BAND_INDEX>
class BandThreshold final : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		// Display actual per-band value
		q31_t value = soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).getThresholdDown();
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		// Set actual per-band value directly
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).setThresholdDown(value);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).getThresholdForDisplay();
	}

	const char* getUnit() override { return "DB"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return BAR; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Per-band ratio control (parameterized by band index)
/// Displays and modifies the actual per-band ratio value
template <size_t BAND_INDEX>
class BandRatio final : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		// Display actual per-band value
		q31_t value = soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).getRatioDown();
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		// Set actual per-band value directly (both up and down ratios)
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).setRatioDown(value);
		soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).setRatioUp(value);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).getRatioForDisplay();
	}

	const char* getUnit() override { return " : 1"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Per-band bandwidth control (gap between up/down thresholds)
/// Displays and modifies the actual per-band bandwidth value
template <size_t BAND_INDEX>
class BandBandwidth final : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		// Display actual per-band value
		q31_t value = soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).getBandwidth();
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		// Set actual per-band value directly
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).setBandwidth(value);
	}

	[[nodiscard]] float getDisplayValue() override {
		return soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).getBandwidthForDisplay();
	}

	const char* getUnit() override { return "DB"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Per-band output level control (post-compression, like OTT's L/M/H sliders)
/// CCW = -inf, 12:00 = 0dB, CW = +16dB
template <size_t BAND_INDEX>
class BandOutputLevel final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	static constexpr int32_t getParamId() {
		if constexpr (BAND_INDEX == 0) {
			return params::UNPATCHED_MB_COMPRESSOR_LOW_LEVEL;
		}
		else if constexpr (BAND_INDEX == 1) {
			return params::UNPATCHED_MB_COMPRESSOR_MID_LEVEL;
		}
		else {
			return params::UNPATCHED_MB_COMPRESSOR_HIGH_LEVEL;
		}
	}

	void readCurrentValue() override {
		q31_t value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(getParamId());
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param = model_stack->getUnpatchedAutoParamFromId(getParamId());
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(getParamId() + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	[[nodiscard]] float getDisplayValue() override {
		float linear =
		    soundEditor.currentModControllable->multibandCompressor.getBand(BAND_INDEX).getOutputLevelLinear();
		// Convert to dB for display
		return 20.0f * std::log10(linear + 1e-10f);
	}

	const char* getUnit() override { return "DB"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

/// Mode selector - first item in DOTT menu
/// 11 modes: Off, AP 6dB, Quirky, Twisted, Weird, LR2 Fast, LR2, LR4 Fast, LR4, Inverted, Twist3
/// One encoder click per mode, ordered by CPU cost (cheapest to most expensive CW)
class ModeZone final : public DecimalWithoutScrolling {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	static constexpr int32_t kNumModes = 11;

	void readCurrentValue() override {
		auto& comp = soundEditor.currentModControllable->multibandCompressor;
		if (!comp.isEnabled()) {
			this->setValue(0); // Off
		}
		else {
			this->setValue(comp.getCrossoverType() + 1);
		}
	}

	void writeCurrentValue() override {
		auto& comp = soundEditor.currentModControllable->multibandCompressor;
		int32_t mode = this->getValue();

		if (mode == 0) {
			comp.setEnabledZone(0);
		}
		else {
			comp.setEnabledZone(ONE_Q31);
			comp.setCrossoverType(mode - 1);
		}
	}

	[[nodiscard]] int32_t getMaxValue() const override { return kNumModes - 1; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }

	/// Click encoder to toggle soft clipping
	MenuItem* selectButtonPress() override {
		auto& comp = soundEditor.currentModControllable->multibandCompressor;
		bool newState = !comp.isSoftClipEnabled();
		comp.setSoftClipEnabled(newState);
		display->displayPopup(newState ? "CLIP" : "noCL");
		return NO_NAVIGATION;
	}

	/// Prevent entering as submenu - stay on horizontal menu
	bool shouldEnterSubmenu() override { return false; }

	// Display mode name as text (not zone knob visualization)
	void renderInHorizontalMenu(const SlotPosition& slot) override {
		deluge::hid::display::OLED::main.drawStringCentered(getModeName(this->getValue()), slot.start_x, slot.start_y,
		                                                    kTextSmallSpacingX, kTextSmallSizeY, slot.width);
	}

protected:
	void drawPixelsForOled() override {
		deluge::hid::display::OLED::main.drawStringCentered(getModeName(this->getValue()), 0,
		                                                    OLED_MAIN_TOPMOST_PIXEL + 20, kTextSpacingX, kTextSpacingY,
		                                                    OLED_MAIN_WIDTH_PIXELS);
	}

	// 7-segment display: show mode name (scrolls if longer than 4 chars)
	void drawActualValue(bool justDidHorizontalScroll = false) override {
		display->setScrollingText(getModeName(this->getValue()));
	}

private:
	static const char* getModeName(int32_t modeIndex) {
		switch (modeIndex) {
		case 0:
			return "Off";
		case 1:
			return "AP 6dB";
		case 2:
			return "Quirky";
		case 3:
			return "Twisted";
		case 4:
			return "Weird";
		case 5:
			return "LR2 Fast";
		case 6:
			return "LR2";
		case 7:
			return "LR4 Fast";
		case 8:
			return "LR4";
		case 9:
			return "Inverted";
		case 10:
			return "Twist3";
		default:
			return "?";
		}
	}
};

/// Multiband wet/dry blend control
class MultibandBlend final : public DecimalWithoutScrolling, public MenuItemWithCCLearning {
public:
	using DecimalWithoutScrolling::DecimalWithoutScrolling;

	void readCurrentValue() override {
		q31_t value =
		    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_MB_COMPRESSOR_BLEND);
		this->setValue(paramToMenuValue128(value));
	}

	void writeCurrentValue() override {
		q31_t value = lshiftAndSaturate<24>(this->getValue());
		char model_stack_memory[MODEL_STACK_MAX_SIZE];
		ModelStackWithThreeMainThings* model_stack = soundEditor.getCurrentModelStack(model_stack_memory);
		ModelStackWithAutoParam* model_stack_with_param =
		    model_stack->getUnpatchedAutoParamFromId(params::UNPATCHED_MB_COMPRESSOR_BLEND);
		model_stack_with_param->autoParam->setCurrentValueInResponseToUserInput(value, model_stack_with_param);
	}

	ParamDescriptor getLearningThing() override {
		ParamDescriptor param_descriptor;
		param_descriptor.setToHaveParamOnly(params::UNPATCHED_MB_COMPRESSOR_BLEND + params::UNPATCHED_START);
		return param_descriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	[[nodiscard]] float getDisplayValue() override {
		q31_t value =
		    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_MB_COMPRESSOR_BLEND);
		return (static_cast<float>(value) / ONE_Q31f) * 100.0f;
	}

	const char* getUnit() override { return "%"; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxKnobPos; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 0; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return modControllable->multibandCompressor.isEnabled();
	}
};

} // namespace deluge::gui::menu_item::audio_compressor
