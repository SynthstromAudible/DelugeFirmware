/*
 * Copyright © 2024-2025 Owlet Records
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
 *
 * --- Additional terms under GNU GPL version 3 section 7 ---
 * This file requires preservation of the above copyright notice and author attribution
 * in all copies or substantial portions of this file.
 */
#pragma once
#include "gui/menu_item/automation/automation.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/fx/stutterer.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include "processing/sound/sound_drum.h"

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::stutter {

/// Density control for scatter modes - dual patched/unpatched param for modulation
/// Uses GLOBAL_SCATTER_DENSITY when in Sound context, UNPATCHED_SCATTER_DENSITY for GlobalEffectable
/// Controls output dry/wet probability:
/// CCW (0) = all dry output (hear input, no grains)
/// CW (50) = normal grain playback (hash decides)
class ScatterDensity final : public IntegerContinuous, public MenuItemWithCCLearning, public Automation {
public:
	using IntegerContinuous::IntegerContinuous;
	/// Compatibility constructor matching patched_param::Integer signature
	ScatterDensity(l10n::String name, l10n::String title, int32_t /*paramId*/) : IntegerContinuous(name, title) {}

	/// Get current scatter mode
	ScatterMode currentMode() const { return soundEditor.currentModControllable->stutterConfig.scatterMode; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Only relevant for looper modes (not Classic/Burst)
		return soundEditor.currentModControllable->stutterConfig.isLooperMode();
	}

	// === Value read/write with dual context support ===

	void readCurrentValue() override {
		q31_t value;
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			value = soundEditor.currentParamManager->getPatchedParamSet()->getValue(params::GLOBAL_SCATTER_DENSITY);
		}
		else {
			value =
			    soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_SCATTER_DENSITY);
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
		soundEditor.currentModControllable->stutterConfig.densityParam = configValue;
		stutterer.setLiveDensity(configValue);
	}

	// === Automation interface (gold knob) ===

	ModelStackWithAutoParam* getModelStackWithParam(void* memory) override {
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			return modelStack->getPatchedAutoParamFromId(params::GLOBAL_SCATTER_DENSITY);
		}
		return modelStack->getUnpatchedAutoParamFromId(params::UNPATCHED_SCATTER_DENSITY);
	}

	// === CC Learning with dual context support ===

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			// Unpatched context (kit, audio clip)
			paramDescriptor.setToHaveParamOnly(params::UNPATCHED_SCATTER_DENSITY + params::UNPATCHED_START);
		}
		else {
			// Patched context (synth, MIDI)
			paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_DENSITY);
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
		soundEditor.patchingParamSelected = params::GLOBAL_SCATTER_DENSITY;
		return &source_selection::regularMenu;
	}

	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) override {
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return nullptr;
		}
		soundEditor.patchingParamSelected = params::GLOBAL_SCATTER_DENSITY;
		source_selection::regularMenu.s = s;
		return &patch_cable_strength::regularMenu;
	}

	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) override {
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_DENSITY);
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
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_DENSITY);
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

	void getColumnLabel(StringBuf& label) override { label.append("Dens"); }

	std::string_view getTitle() const override { return getName(); }

	[[nodiscard]] std::string_view getName() const override { return "Density"; }

	void getNotificationValue(StringBuf& valueBuf) override {
		int32_t val = this->getValue();
		int32_t percent = (val * 100) / kMaxMenuValue;
		valueBuf.appendInt(percent);
		valueBuf.append("%");
	}
};

} // namespace deluge::gui::menu_item::stutter
