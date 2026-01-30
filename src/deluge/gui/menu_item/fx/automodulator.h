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
#include "gui/menu_item/velocity_encoder.h"
#include "gui/ui/sound_editor.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "hid/display/oled.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/model_stack.h"
#include "modulation/params/param.h"
#include "modulation/params/param_set.h"
#include "modulation/patch/patch_cable_set.h"
#include <algorithm>
#include <cstdio>

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::fx {

// Resolution and zone constants for automodulator
constexpr int32_t kAutomodZoneResolution = 1024; // 0-1023
constexpr int32_t kAutomodNumZones = 8;

/// Automod Macro: GLOBAL patched param for overall modulation depth with mod matrix support
/// Works in both Sound (patched) and GlobalEffectable (unpatched) contexts
/// Secret menu: Push+twist to adjust gammaPhase (multiplier for all zone phase offsets)
class AutomodMacro final : public IntegerContinuous, public MenuItemWithCCLearning, public Automation {
public:
	using IntegerContinuous::IntegerContinuous;
	/// Compatibility constructor matching patched_param::Integer signature
	AutomodMacro(l10n::String name, l10n::String title, int32_t /*paramId*/) : IntegerContinuous(name, title) {}

	// === Value read/write with dual context support ===

	void readCurrentValue() override {
		q31_t value;
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			value = soundEditor.currentParamManager->getPatchedParamSet()->getValue(params::GLOBAL_AUTOMOD_MACRO);
		}
		else {
			value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_AUTOMOD_MACRO);
		}
		// Use standard half-precision scaling (unipolar 0-1 param)
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(value));
	}

	void writeCurrentValue() override {
		q31_t value = computeFinalValueForHalfPrecisionMenuItem(this->getValue());
		char modelStackMemory[MODEL_STACK_MAX_SIZE];
		ModelStackWithAutoParam* modelStackWithParam = getModelStackWithParam(modelStackMemory);
		modelStackWithParam->autoParam->setCurrentValueInResponseToUserInput(value, modelStackWithParam);
	}

	// === Automation interface (gold knob) ===

	ModelStackWithAutoParam* getModelStackWithParam(void* memory) override {
		ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			return modelStack->getPatchedAutoParamFromId(params::GLOBAL_AUTOMOD_MACRO);
		}
		return modelStack->getUnpatchedAutoParamFromId(params::UNPATCHED_AUTOMOD_MACRO);
	}

	// === CC Learning with dual context support ===

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			// Unpatched context (kit, audio clip)
			paramDescriptor.setToHaveParamOnly(params::UNPATCHED_AUTOMOD_MACRO + params::UNPATCHED_START);
		}
		else {
			// Patched context (synth, MIDI)
			paramDescriptor.setToHaveParamOnly(params::GLOBAL_AUTOMOD_MACRO);
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
		soundEditor.patchingParamSelected = params::GLOBAL_AUTOMOD_MACRO;
		return &source_selection::regularMenu;
	}

	/// Handle patching source shortcut press (e.g., LFO1, LFO2, envelope shortcuts)
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) override {
		// In unpatched context, no patching available
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return nullptr;
		}
		// In patched context, open patch cable strength menu for this source
		soundEditor.patchingParamSelected = params::GLOBAL_AUTOMOD_MACRO;
		source_selection::regularMenu.s = s;
		return &patch_cable_strength::regularMenu;
	}

	/// Blink shortcut if this source is patched to automod macro
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) override {
		// In unpatched context, no patching - don't blink
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		// In patched context, check if source is patched to this param
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_AUTOMOD_MACRO);
		return soundEditor.currentParamManager->getPatchCableSet()
		               ->isSourcePatchedToDestinationDescriptorVolumeInspecific(s, paramDescriptor)
		           ? 3
		           : 255;
	}

	/// Show dot on name if any source is patched to automod macro
	uint8_t shouldDrawDotOnName() override {
		// In unpatched context, no patching - no dot
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		// In patched context, check if any source is patched
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_AUTOMOD_MACRO);
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
			float& gamma = soundEditor.currentModControllable->automod.gammaPhase;
			gamma = std::max(0.0f, gamma + static_cast<float>(offset) * 0.1f);
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

/// Automod Type: 0=OFF, 1-1023 = DSP topology blend (8 zones)
/// Controls mix of filter/comb/tremolo stages
/// Secret menu: Push+twist to adjust typePhaseOffset
class AutomodType final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->automod.type); }

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		uint16_t newType = static_cast<uint16_t>(this->getValue());
		auto* mca = soundEditor.currentModControllable;

		// Reset state when turning effect on
		if (mca->automod.type == 0 && newType > 0) {
			mca->automod.resetState();
		}
		mca->automod.type = newType;
	}

	[[nodiscard]] int32_t getMaxValue() const override { return kAutomodZoneResolution - 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret: push+twist adjusts typePhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->automod.typePhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 1.0f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "T:%d", static_cast<int32_t>(phase));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			int32_t newValue = this->getValue() + velocity_.getScaledOffset(offset);
			newValue = std::clamp<int32_t>(newValue, 0, kAutomodZoneResolution - 1);
			this->setValue(newValue);
			writeCurrentValue();
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			else {
				drawValue();
			}
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		if (this->getValue() == 0) {
			deluge::hid::display::OLED::main.drawStringCentered("OFF", slot.start_x,
			                                                    slot.start_y + kHorizontalMenuSlotYOffset,
			                                                    kTextSpacingX, kTextSpacingY, slot.width);
			return;
		}
		float phaseOffset = soundEditor.currentModControllable->automod.typePhaseOffset;
		if (phaseOffset != 0.0f) {
			cacheCoordDisplay(phaseOffset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones,
			                           [](int32_t z) { return getTypeName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		if (this->getValue() == 0) {
			deluge::hid::display::OLED::main.drawStringCentered("OFF", 0, 20, kTextSpacingX, kTextSpacingY,
			                                                    OLED_MAIN_WIDTH_PIXELS);
			return;
		}
		float phaseOffset = soundEditor.currentModControllable->automod.typePhaseOffset;
		if (phaseOffset != 0.0f) {
			cacheCoordDisplay(phaseOffset, this->getValue());
			drawZoneForOled(this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kAutomodZoneResolution, kAutomodNumZones,
			                [](int32_t z) { return getTypeName(z); });
		}
	}

private:
	mutable VelocityEncoder velocity_;
	mutable bool suppressNotification_ = false;

	// Shared coordinate display buffer
	static inline char coordBuffer_[12] = {};
	static void cacheCoordDisplay(float phaseOffset, int32_t value) {
		int32_t p = static_cast<int32_t>(phaseOffset);
		int32_t z = value >> 7; // 0-1023 → 0-7 (zone index)
		snprintf(coordBuffer_, sizeof(coordBuffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coordBuffer_; }

	static const char* getTypeName(int32_t zoneIndex) {
		// Abstract color names (matching scatter Z1)
		switch (zoneIndex) {
		case 0:
			return "Rose";
		case 1:
			return "Blue";
		case 2:
			return "Indigo";
		case 3:
			return "Green";
		case 4:
			return "Lotus";
		case 5:
			return "White";
		case 6:
			return "Grey";
		case 7:
			return "Black";
		default:
			return "?";
		}
	}
};

/// Automod Flavor: 0-1023 (8 zones) for filter character control
/// Controls LP/BP/HP mix via phi triangles
/// Secret menu: Push+twist to adjust flavorPhaseOffset
class AutomodFlavor final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->automod.flavor); }

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		soundEditor.currentModControllable->automod.flavor = static_cast<uint16_t>(this->getValue());
	}

	[[nodiscard]] int32_t getMaxValue() const override { return kAutomodZoneResolution - 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret: push+twist adjusts flavorPhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->automod.flavorPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 1.0f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "F:%d", static_cast<int32_t>(phase));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			int32_t newValue = this->getValue() + velocity_.getScaledOffset(offset);
			newValue = std::clamp<int32_t>(newValue, 0, kAutomodZoneResolution - 1);
			this->setValue(newValue);
			writeCurrentValue();
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			else {
				drawValue();
			}
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		float phaseOffset = soundEditor.currentModControllable->automod.flavorPhaseOffset;
		if (phaseOffset != 0.0f) {
			cacheCoordDisplay(phaseOffset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones,
			                           [](int32_t z) { return getFlavorName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		float phaseOffset = soundEditor.currentModControllable->automod.flavorPhaseOffset;
		if (phaseOffset != 0.0f) {
			cacheCoordDisplay(phaseOffset, this->getValue());
			drawZoneForOled(this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kAutomodZoneResolution, kAutomodNumZones,
			                [](int32_t z) { return getFlavorName(z); });
		}
	}

private:
	mutable VelocityEncoder velocity_;
	mutable bool suppressNotification_ = false;

	// Shared coordinate display buffer
	static inline char coordBuffer_[12] = {};
	static void cacheCoordDisplay(float phaseOffset, int32_t value) {
		int32_t p = static_cast<int32_t>(phaseOffset);
		int32_t z = value >> 7; // 0-1023 → 0-7 (zone index)
		snprintf(coordBuffer_, sizeof(coordBuffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coordBuffer_; }

	static const char* getFlavorName(int32_t zoneIndex) {
		// Abstract weather/nature names for routing character
		switch (zoneIndex) {
		case 0:
			return "Frost";
		case 1:
			return "Dew";
		case 2:
			return "Fog";
		case 3:
			return "Cloud";
		case 4:
			return "Rain";
		case 5:
			return "Storm";
		case 6:
			return "Dark";
		case 7:
			return "Night";
		default:
			return "?";
		}
	}
};

/// Automod Mod: 0-1023 (8 zones) for rate/phase control
/// Controls LFO rate and stereo phase offset via phi triangle
/// Bipolar triangle: positive = free Hz, negative = tempo-synced subdivision
/// Secret menu: Push+twist to adjust modPhaseOffset
class AutomodMod final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->automod.mod); }

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		soundEditor.currentModControllable->automod.mod = static_cast<uint16_t>(this->getValue());
	}

	[[nodiscard]] int32_t getMaxValue() const override { return kAutomodZoneResolution - 1; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret: push+twist adjusts modPhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->automod.modPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(velocity_.getScaledOffset(offset)) * 1.0f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "M:%d", static_cast<int32_t>(phase));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			int32_t newValue = this->getValue() + velocity_.getScaledOffset(offset);
			newValue = std::clamp<int32_t>(newValue, 0, kAutomodZoneResolution - 1);
			this->setValue(newValue);
			writeCurrentValue();
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			else {
				drawValue();
			}
		}
	}

	[[nodiscard]] bool showNotification() const override {
		if (suppressNotification_) {
			suppressNotification_ = false;
			return false;
		}
		return true;
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		auto& automod = soundEditor.currentModControllable->automod;
		// Phi triangle mode: show coordinate display
		if (automod.modPhaseOffset != 0.0f) {
			cacheCoordDisplay(automod.modPhaseOffset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones,
			                           [](int32_t z) { return getModName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		auto& automod = soundEditor.currentModControllable->automod;
		// Phi triangle mode: show coordinate display
		if (automod.modPhaseOffset != 0.0f) {
			cacheCoordDisplay(automod.modPhaseOffset, this->getValue());
			drawZoneForOled(this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			drawZoneForOled(this->getValue(), kAutomodZoneResolution, kAutomodNumZones,
			                [](int32_t z) { return getModName(z); });
		}
	}

private:
	mutable VelocityEncoder velocity_;
	mutable bool suppressNotification_ = false;

	// Shared coordinate display buffer
	static inline char coordBuffer_[12] = {};
	static void cacheCoordDisplay(float phaseOffset, int32_t value) {
		int32_t p = static_cast<int32_t>(phaseOffset);
		int32_t z = value >> 7; // 0-1023 → 0-7 (zone index)
		snprintf(coordBuffer_, sizeof(coordBuffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coordBuffer_; }

	static const char* getModName(int32_t zoneIndex) {
		switch (zoneIndex) {
		case 0:
			return "0.1Hz";
		case 1:
			return "0.2Hz";
		case 2:
			return "0.4Hz";
		case 3:
			return "0.8Hz";
		case 4:
			return "1.5Hz";
		case 5:
			return "3Hz";
		case 6:
			return "6Hz";
		case 7:
			return "10Hz";
		default:
			return "?";
		}
	}
};

} // namespace deluge::gui::menu_item::fx
