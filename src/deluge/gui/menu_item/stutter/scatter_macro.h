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

#include "definitions_cxx.hpp"
#include "gui/menu_item/automation/automation.h"
#include "gui/menu_item/integer.h"
#include "gui/menu_item/menu_item_with_cc_learning.h"
#include "gui/menu_item/patch_cable_strength/regular.h"
#include "gui/menu_item/source_selection/regular.h"
#include "gui/menu_item/value_scaling.h"
#include "gui/ui/sound_editor.h"
#include "model/fx/stutterer.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include <hid/buttons.h>
#include <hid/display/display.h>

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::stutter {

/// Scatter macro parameter - dual patched/unpatched param for macro control
/// Uses GLOBAL_SCATTER_MACRO when in Sound context, UNPATCHED_SCATTER_MACRO for GlobalEffectable (kits, audio clips)
///
/// Secret menu: Push+twist encoder to adjust gammaPhase (multiplier for all zone phase offsets)
class ScatterMacro final : public IntegerContinuous, public MenuItemWithCCLearning, public Automation {
public:
	using IntegerContinuous::IntegerContinuous;
	/// Compatibility constructor matching patched_param::Integer signature (param ID is always GLOBAL_SCATTER_MACRO)
	ScatterMacro(l10n::String name, l10n::String title, int32_t /*paramId*/) : IntegerContinuous(name, title) {}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Not relevant for Classic or Burst (gated stutter) modes
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode != ScatterMode::Classic && mode != ScatterMode::Burst;
	}

	// === Value read/write with dual context support ===

	void readCurrentValue() override {
		q31_t value;
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			value = soundEditor.currentParamManager->getPatchedParamSet()->getValue(params::GLOBAL_SCATTER_MACRO);
		}
		else {
			value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_SCATTER_MACRO);
		}
		// Bipolar storage, displayed as 0-50 (like TableShaperMix)
		this->setValue(computeCurrentValueForStandardMenuItem(value));
	}

	void writeCurrentValue() override {
		q31_t value = computeFinalValueForStandardMenuItem(this->getValue());
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStackMemory);
		modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);
	}

	// === Automation interface (gold knob) ===

	ModelStackWithAutoParam* getModelStackWithParam(void* memory) override {
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			return modelStack->getPatchedAutoParamFromId(params::GLOBAL_SCATTER_MACRO);
		}
		return modelStack->getUnpatchedAutoParamFromId(params::UNPATCHED_SCATTER_MACRO);
	}

	// === CC Learning with dual context support ===

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			// Unpatched context (kit, audio clip)
			paramDescriptor.setToHaveParamOnly(params::UNPATCHED_SCATTER_MACRO + params::UNPATCHED_START);
		}
		else {
			// Patched context (synth, MIDI)
			paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_MACRO);
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
		soundEditor.patchingParamSelected = params::GLOBAL_SCATTER_MACRO;
		return &source_selection::regularMenu;
	}

	/// Handle patching source shortcut press (e.g., LFO1, LFO2, envelope shortcuts)
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) override {
		// In unpatched context, no patching available
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return nullptr;
		}
		// In patched context, open patch cable strength menu for this source
		soundEditor.patchingParamSelected = params::GLOBAL_SCATTER_MACRO;
		source_selection::regularMenu.s = s;
		return &patch_cable_strength::regularMenu;
	}

	/// Blink shortcut if this source is patched to scatter macro
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) override {
		// In unpatched context, no patching - don't blink
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		// In patched context, check if source is patched to this param
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_MACRO);
		return soundEditor.currentParamManager->getPatchCableSet()
		               ->isSourcePatchedToDestinationDescriptorVolumeInspecific(s, paramDescriptor)
		           ? 3
		           : 255;
	}

	/// Show dot on name if any source is patched to scatter macro
	uint8_t shouldDrawDotOnName() override {
		// In unpatched context, no patching - no dot
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		// In patched context, check if any source is patched
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_SCATTER_MACRO);
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

	// === Encoder action with secret menu ===

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust gammaPhase (multiplier for all zone phase offsets)
			Buttons::selectButtonPressUsedUp = true;
			float& gamma = soundEditor.currentModControllable->stutterConfig.gammaPhase;
			gamma = std::max(0.0f, gamma + static_cast<float>(offset) * 0.1f);
			// Show current value on display
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "gamma:%d", static_cast<int32_t>(gamma * 10.0f));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			IntegerContinuous::selectEncoderAction(offset);
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	// === Display configuration ===

	[[nodiscard]] int32_t getMinValue() const override { return kMinMenuValue; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	void getColumnLabel(StringBuf& label) override { label.append("Macro"); }

private:
	mutable bool suppressNotification_ = false;
};

} // namespace deluge::gui::menu_item::stutter
