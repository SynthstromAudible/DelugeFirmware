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
#include "gui/menu_item/automation/automation.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/menu_item/toggle.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/fx/stutterer.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "model/song/song.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::stutter {

/// Toggle-based Quantize menu for Classic and Burst modes
class QuantizedStutter final : public Toggle {
public:
	using Toggle::Toggle;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->stutterConfig.quantized); }

	void writeCurrentValue() override {
		bool current_value = this->getValue();
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					if (!soundEditor.currentModControllable->stutterConfig.useSongStutter) {
						soundDrum->stutterConfig.quantized = current_value;
					}
				}
			}
		}
		else {
			soundEditor.currentModControllable->stutterConfig.quantized = current_value;
		}
	}

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Only relevant for Classic mode
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode == ScatterMode::Classic;
	}
};

/// pWrite param for looper scatter modes - dual patched/unpatched param for modulation
/// Uses GLOBAL_SCATTER_PWRITE when in Sound context, UNPATCHED_SCATTER_PWRITE for GlobalEffectable
/// Controls buffer write-back probability for all modes:
/// - CCW (0) = 0% writes (freeze buffer content)
/// - CW (50) = 100% writes (always overwrite with fresh content)
/// Exception: Pitch mode repurposes this as scale selection (0-50 → 0-11 scales)
class ScatterModeParam final : public IntegerContinuous, public MenuItemWithCCLearning, public Automation {
public:
	using IntegerContinuous::IntegerContinuous;
	/// Compatibility constructor matching patched_param::Integer signature
	ScatterModeParam(l10n::String name, l10n::String title, int32_t /*paramId*/) : IntegerContinuous(name, title) {}

	// Scale names for Pitch mode (scales + triads)
	static constexpr const char* kScaleNames[] = {
	    "Chromatic", "Major",   "Minor",  "MajPent", "MinPent", "Blues",
	    "Dorian",    "Mixolyd", "MajTri", "MinTri",  "Sus4",    "Dim",
	};
	static constexpr const char* kScaleShort[] = {
	    "Chr", "Maj", "Min", "Ma5", "Mi5", "Blu", "Dor", "Mix", "MAJ", "MIN", "Su4", "Dim",
	};
	static constexpr int32_t kNumScales = 12;

	/// Get current scatter mode
	ScatterMode currentMode() const { return soundEditor.currentModControllable->stutterConfig.scatterMode; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Only relevant for modes that use a slider param (not Classic/Burst which use Toggle)
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode != ScatterMode::Classic && mode != ScatterMode::Burst;
	}

	// === Value read/write with dual context support ===

	void readCurrentValue() override {
		q31_t value;
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			value = soundEditor.currentParamManager->getPatchedParamSet()->getValue(params::GLOBAL_SCATTER_PWRITE);
		}
		else {
			value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_SCATTER_PWRITE);
		}
		// Use standard half-precision scaling (unipolar 0-1 param)
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(value));
	}

	void writeCurrentValue() override {
		q31_t value = computeFinalValueForHalfPrecisionMenuItem(this->getValue());
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStackMemory);
		modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);

		// Also update StutterConfig for live changes (convert from menu value 0-50)
		uint8_t configValue = static_cast<uint8_t>(this->getValue());
		soundEditor.currentModControllable->stutterConfig.pWriteParam = configValue;
		stutterer.setLivePWrite(configValue);
	}

	// === Automation interface (gold knob) ===

	ModelStackWithAutoParam* getModelStackWithParam(void* memory) override {
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			return modelStack->getPatchedAutoParamFromId(params::GLOBAL_SCATTER_PWRITE);
		}
		return modelStack->getUnpatchedAutoParamFromId(params::UNPATCHED_SCATTER_PWRITE);
	}

	// === CC Learning with dual context support ===

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			// Unpatched context (kit, audio clip)
			paramDescriptor.setToHaveParamOnly(params::UNPATCHED_SCATTER_PWRITE + params::UNPATCHED_START);
		}
		else {
			// Patched context (synth, MIDI)
			paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_PWRITE);
		}
		return paramDescriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	// === Mod matrix support (patched context only) ===

	MenuItem* selectButtonPress() override {
		// If shift held down, user wants to delete automation
		if (Buttons::isShiftButtonPressed()) {
			return Automation::selectButtonPress();
		}
		// In unpatched context (GlobalEffectable), no mod matrix available
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return nullptr;
		}
		// In patched context (Sound), open mod matrix source selection
		soundEditor.patchingParamSelected = params::GLOBAL_SCATTER_PWRITE;
		return &source_selection::regularMenu;
	}

	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) override {
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return nullptr;
		}
		soundEditor.patchingParamSelected = params::GLOBAL_SCATTER_PWRITE;
		source_selection::regularMenu.s = s;
		return &patch_cable_strength::regularMenu;
	}

	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) override {
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_PWRITE);
		return soundEditor.currentParamManager->getPatchCableSet()
		               ->isSourcePatchedToDestinationDescriptorVolumeInspecific(s, paramDescriptor)
		           ? 3
		           : 255;
	}

	uint8_t shouldDrawDotOnName() override {
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_PWRITE);
		return soundEditor.currentParamManager->getPatchCableSet()->isAnySourcePatchedToParamVolumeInspecific(
		           paramDescriptor)
		           ? 3
		           : 255;
	}

	[[nodiscard]] deluge::modulation::params::Kind getParamKind() {
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return deluge::modulation::params::Kind::UNPATCHED_SOUND;
		}
		return deluge::modulation::params::Kind::PATCHED;
	}

	bool usesAffectEntire() override { return true; }

	// === Display configuration ===
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	void getColumnLabel(StringBuf& label) override {
		if (currentMode() == ScatterMode::Pitch) {
			label.append("Scale");
		}
		else {
			label.append("pWrt");
		}
	}

	std::string_view getTitle() const override { return getName(); }

	[[nodiscard]] std::string_view getName() const override {
		if (currentMode() == ScatterMode::Pitch) {
			return "Scale";
		}
		return "pWrite";
	}

	void drawValue() override {
		auto mode = currentMode();
		int32_t val = this->getValue();

		if (mode == ScatterMode::Pitch) {
			// Map 0-50 to 0-11 scale index
			int32_t idx = (val * (kNumScales - 1)) / kMaxMenuValue;
			idx = std::clamp(idx, int32_t{0}, kNumScales - 1);
			display->setTextAsNumber(idx);
		}
		else {
			IntegerContinuous::drawValue();
		}
	}

	void getNotificationValue(StringBuf& valueBuf) override {
		auto mode = currentMode();
		int32_t val = this->getValue();

		if (mode == ScatterMode::Pitch) {
			int32_t idx = (val * (kNumScales - 1)) / kMaxMenuValue;
			idx = std::clamp(idx, int32_t{0}, kNumScales - 1);
			valueBuf.append(kScaleShort[idx]);
		}
		else {
			int32_t writePercent = (val * 100) / kMaxMenuValue;
			valueBuf.appendInt(writePercent);
			valueBuf.append("%");
		}
	}
};

} // namespace deluge::gui::menu_item::stutter
