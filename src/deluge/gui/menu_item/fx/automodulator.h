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
#include "gui/menu_item/zone_based.h"
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
#include <cmath>
#include <cstdio>

namespace params = deluge::modulation::params;

namespace deluge::gui::menu_item::fx {

// Resolution and zone constants for automodulator
constexpr int32_t kAutomodZoneResolution = 1024; // 0-1023
constexpr int32_t kAutomodNumZones = 8;

// ============================================================================
// Page 2 Menu Items: Freq, Rate, Manual, Depth
// ============================================================================

/// Sync rate table entry - ordered by frequency (slow to fast, interspersed triplets)
struct SyncRateEntry {
	uint8_t syncLevel;      // SyncLevel enum value (1-9)
	uint8_t slowShift;      // Additional right-shift for ultra-slow rates (0=normal)
	bool triplet;           // true = triplet timing
	const char* label;      // Full display label (e.g., "1/4", "1/4T") for OLED
	const char* shortLabel; // Short label (max 4 chars) for 7-seg display
};

/// Sync rates ordered by frequency (slow to fast)
/// Labels are honest (actual LFO cycle length matches display)
/// Triplets are interspersed at their actual frequency position
/// Max speed: 1/64, Min speed: 8/1
constexpr SyncRateEntry kSyncRates[] = {
    {1, 2, false, "8/1", "8/1"},  // 8 whole notes per cycle
    {1, 1, false, "4/1", "4/1"},  // 4 whole notes per cycle
    {1, 0, false, "2/1", "2/1"},  // 2 whole notes per cycle
    {2, 0, false, "1/1", "1/1"},  // 1 whole note per cycle
    {2, 0, true, "1/1T", "1/1T"}, // Triplet
    {3, 0, false, "1/2", "1/2"},  // Half note
    {3, 0, true, "1/2T", "1/2T"}, // Triplet
    {4, 0, false, "1/4", "1/4"},  // Quarter note
    {4, 0, true, "1/4T", "1/4T"}, // Triplet
    {5, 0, false, "1/8", "1/8"},  // 8th note
    {5, 0, true, "1/8T", "1/8T"}, // Triplet
    {6, 0, false, "1/16", "16"},  // 16th note
    {6, 0, true, "1/16T", "16T"}, // Triplet
    {7, 0, false, "1/32", "32"},  // 32nd note
    {7, 0, true, "1/32T", "32T"}, // Triplet
    {8, 0, false, "1/64", "64"},  // 64th note (max speed)
    {8, 0, true, "1/64T", "64T"}, // Triplet
};
constexpr int32_t kNumSyncRates = sizeof(kSyncRates) / sizeof(kSyncRates[0]);

// Helpers for dual patched/unpatched automod param access (Sound vs GlobalEffectable contexts)
inline q31_t getAutomodParamValue(params::ParamType patched, params::ParamType unpatched) {
	if (soundEditor.currentParamManager->containsAnyMainParamCollections()) {
		return soundEditor.currentParamManager->getPatchedParamSet()->getValue(patched);
	}
	return soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(unpatched);
}

inline ModelStackWithAutoParam* getAutomodModelStack(void* memory, params::ParamType patched,
                                                     params::ParamType unpatched) {
	ModelStackWithThreeMainThings* modelStack = soundEditor.getCurrentModelStack(memory);
	if (soundEditor.currentParamManager->containsAnyMainParamCollections()) {
		return modelStack->getPatchedAutoParamFromId(patched);
	}
	return modelStack->getUnpatchedAutoParamFromId(unpatched);
}

/// Automod Freq: Bipolar filter frequency offset with dual patched/unpatched support
/// Uses mod matrix in Sound context, learnable in GlobalEffectable context
/// Bipolar range: -64 to +63, displays with explicit +/- sign
class AutomodFreq final : public patched_param::Integer {
public:
	static constexpr int32_t kFreqMenuHalfRange = 64;

	using patched_param::Integer::Integer;

	/// Override selectEncoderAction to enforce our bipolar range
	void selectEncoderAction(int32_t offset) override {
		int32_t newValue = this->getValue() + offset;
		// Clamp to bipolar range
		if (newValue > kFreqMenuHalfRange - 1) {
			newValue = kFreqMenuHalfRange - 1;
		}
		else if (newValue < -kFreqMenuHalfRange) {
			newValue = -kFreqMenuHalfRange;
		}
		this->setValue(newValue);

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

	// Bipolar display: show +/- explicitly
	void drawPixelsForOled() override {
		DEF_STACK_STRING_BUF(valStr, 8);
		formatBipolarValue(valStr);
		deluge::hid::display::OLED::main.drawStringCentered(valStr.data(), 0, 20, kTextSpacingX, kTextSpacingY,
		                                                    OLED_MAIN_WIDTH_PIXELS);
	}

	void drawValue() override {
		DEF_STACK_STRING_BUF(valStr, 8);
		formatBipolarValue(valStr);
		display->setText(valStr.data());
	}

	// Use default knob rendering for horizontal menu (from Number base class)
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

protected:
	[[nodiscard]] int32_t getMinValue() const override { return -kFreqMenuHalfRange; }
	[[nodiscard]] int32_t getMaxValue() const override { return kFreqMenuHalfRange - 1; }

	void readCurrentValue() override {
		// Read from q31 param, scale to menu range (-64 to +63)
		// >> 25 maps q31 [-2^31, 2^31-1] to [-64, +63]
		this->setValue(getAutomodParamValue(params::GLOBAL_AUTOMOD_FREQ, params::UNPATCHED_AUTOMOD_FREQ) >> 25);
	}

	// writeCurrentValue uses base class (DSP reads from paramFinalValues)

	ModelStackWithAutoParam* getModelStack(void* memory) override {
		return getAutomodModelStack(memory, params::GLOBAL_AUTOMOD_FREQ, params::UNPATCHED_AUTOMOD_FREQ);
	}

	int32_t getFinalValue() override {
		int32_t value = this->getValue();
		if (value >= kFreqMenuHalfRange - 1) {
			return std::numeric_limits<int32_t>::max();
		}
		else if (value <= -kFreqMenuHalfRange) {
			return std::numeric_limits<int32_t>::min();
		}
		else {
			// Scale -64..+63 to q31 range (<< 25 is inverse of >> 25)
			return value << 25;
		}
	}

private:
	void formatBipolarValue(StringBuf& buf) {
		int32_t val = this->getValue();
		if (val > 0) {
			buf.append("+");
		}
		buf.appendInt(val);
	}
};

/// Automod Manual: Bipolar LFO offset with dual patched/unpatched support
/// - When LFO is running: adds to LFO output
/// - When rate is "stop": used directly as LFO value (manual control)
/// Bipolar range: -64 to +63, displays with explicit +/- sign
class AutomodManual final : public patched_param::Integer {
public:
	static constexpr int32_t kManualMenuHalfRange = 64;

	using patched_param::Integer::Integer;

	void selectEncoderAction(int32_t offset) override {
		int32_t newValue = this->getValue() + offset;
		// Clamp to bipolar range
		if (newValue > kManualMenuHalfRange - 1) {
			newValue = kManualMenuHalfRange - 1;
		}
		else if (newValue < -kManualMenuHalfRange) {
			newValue = -kManualMenuHalfRange;
		}
		this->setValue(newValue);

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

	void drawPixelsForOled() override {
		DEF_STACK_STRING_BUF(valStr, 8);
		formatBipolarValue(valStr);
		deluge::hid::display::OLED::main.drawStringCentered(valStr.data(), 0, 20, kTextSpacingX, kTextSpacingY,
		                                                    OLED_MAIN_WIDTH_PIXELS);
	}

	void drawValue() override {
		DEF_STACK_STRING_BUF(valStr, 8);
		formatBipolarValue(valStr);
		display->setText(valStr.data());
	}

	// Use default knob rendering for horizontal menu (from Number base class)
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

protected:
	[[nodiscard]] int32_t getMinValue() const override { return -kManualMenuHalfRange; }
	[[nodiscard]] int32_t getMaxValue() const override { return kManualMenuHalfRange - 1; }

	void readCurrentValue() override {
		// Read from q31 param, scale to menu range (-64 to +63)
		// >> 25 maps q31 [-2^31, 2^31-1] to [-64, +63]
		this->setValue(getAutomodParamValue(params::GLOBAL_AUTOMOD_MANUAL, params::UNPATCHED_AUTOMOD_MANUAL) >> 25);
	}

	// writeCurrentValue uses base class (DSP reads from paramFinalValues)

	ModelStackWithAutoParam* getModelStack(void* memory) override {
		return getAutomodModelStack(memory, params::GLOBAL_AUTOMOD_MANUAL, params::UNPATCHED_AUTOMOD_MANUAL);
	}

	int32_t getFinalValue() override {
		int32_t value = this->getValue();
		if (value >= kManualMenuHalfRange - 1) {
			return std::numeric_limits<int32_t>::max();
		}
		else if (value <= -kManualMenuHalfRange) {
			return std::numeric_limits<int32_t>::min();
		}
		else {
			// Scale -64..+63 to q31 range (<< 25 is inverse of >> 25)
			return value << 25;
		}
	}

private:
	void formatBipolarValue(StringBuf& buf) {
		int32_t val = this->getValue();
		if (val > 0) {
			buf.append("+");
		}
		buf.appendInt(val);
	}
};

/// Automod Rate: Pure LFO rate control (direct rate only, no "Free" mode)
/// - Encoder press toggles synced/unsynced mode
/// - Synced: shows "1/1", "1/2", "1/4", etc. (value 1 = index 0)
/// - Unsynced: shows "10ms", "26ms", etc.
/// Note: LFO modes (Stop/Once/Retrig/Free) are controlled via push toggle on Depth knob
class AutomodRate final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->automod.rate); }

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		soundEditor.currentModControllable->automod.rate = static_cast<uint16_t>(this->getValue());
	}

	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override {
		if (soundEditor.currentModControllable->automod.rateSynced) {
			return kNumSyncRates; // 1-17 = sync rates (1/1 to 1/256T)
		}
		return 128; // Unsynced: 1-128 = ms range
	}
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	void selectEncoderAction(int32_t offset) override {
		// Check for encoder press (toggle sync mode)
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC) && offset == 0) {
			// Pure press with no turn - toggle sync
			Buttons::selectButtonPressUsedUp = true;
			toggleSyncWithPopup();
			return;
		}
		// Normal turn
		Integer::selectEncoderAction(offset);
	}

	MenuItem* selectButtonPress() override {
		// Toggle sync mode on press
		toggleSyncWithPopup();
		return NO_NAVIGATION;
	}

private:
	void toggleSyncWithPopup() {
		auto& automod = soundEditor.currentModControllable->automod;
		automod.rateSynced = !automod.rateSynced;
		// Clamp value to new range if needed
		if (automod.rate < static_cast<uint16_t>(getMinValue())) {
			automod.rate = static_cast<uint16_t>(getMinValue());
		}
		if (automod.rate > static_cast<uint16_t>(getMaxValue())) {
			automod.rate = static_cast<uint16_t>(getMaxValue());
		}
		readCurrentValue();
		// Show popup indicating sync state
		display->displayPopup(automod.rateSynced ? "SYNC" : "SYNC OFF");
		if (display->haveOLED()) {
			renderUIsForOled();
		}
		else {
			drawValue();
		}
	}

public:
	void drawPixelsForOled() override {
		DEF_STACK_STRING_BUF(rateStr, 16);
		getRateString(rateStr);
		deluge::hid::display::OLED::main.drawStringCentered(rateStr.data(), 0, 20, kTextSpacingX, kTextSpacingY,
		                                                    OLED_MAIN_WIDTH_PIXELS);
	}

	void drawValue() override {
		DEF_STACK_STRING_BUF(rateStr, 8);
		getRateString(rateStr);
		display->setText(rateStr.data());
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		DEF_STACK_STRING_BUF(rateStr, 8);
		getRateString(rateStr, true); // Use short labels for horizontal menu
		deluge::hid::display::OLED::main.drawStringCentered(rateStr.data(), slot.start_x,
		                                                    slot.start_y + kHorizontalMenuSlotYOffset, kTextSpacingX,
		                                                    kTextSpacingY, slot.width);
	}

	void getColumnLabel(StringBuf& label) override { label.append("Rate"); }

private:
	void getRateString(StringBuf& buf, bool useShortLabel = false) {
		auto& automod = soundEditor.currentModControllable->automod;
		if (automod.rateSynced) {
			// Synced mode: show subdivision (rate 1 = index 0)
			int32_t idx = automod.rate - 1;
			if (idx >= 0 && idx < kNumSyncRates) {
				buf.append(useShortLabel ? kSyncRates[idx].shortLabel : kSyncRates[idx].label);
			}
			else {
				buf.append("?");
			}
		}
		else {
			// Unsynced mode: log scale from 0.01Hz to 20Hz
			// Formula: hz = 0.01 * 2000^((rate-1)/127)
			float hz = 0.01f * powf(2000.0f, static_cast<float>(automod.rate - 1) / 127.0f);
			if (hz < 0.1f) {
				// Show 2 decimal places for very slow rates (e.g., "0.01Hz")
				int32_t hundredths = static_cast<int32_t>(hz * 100.0f + 0.5f);
				buf.append("0.");
				if (hundredths < 10) {
					buf.append("0");
				}
				buf.appendInt(hundredths);
			}
			else if (hz < 1.0f) {
				// Show 1 decimal place for sub-Hz rates (e.g., "0.5Hz")
				int32_t tenths = static_cast<int32_t>(hz * 10.0f + 0.5f);
				buf.append("0.");
				buf.appendInt(tenths);
			}
			else {
				// Show whole Hz for faster rates (e.g., "20Hz")
				buf.appendInt(static_cast<int32_t>(hz + 0.5f));
			}
			if (!useShortLabel) {
				buf.append("Hz");
			}
		}
	}
};

/// Automod Mix: Wet/dry blend, 0=OFF (bypass)
/// Shows "OFF" at value 0, otherwise shows percentage
/// Secret menu: Push+twist to adjust gammaPhase (multiplier for all zone phase offsets)
class AutomodMix final : public IntegerWithOff {
public:
	using IntegerWithOff::IntegerWithOff;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->automod.mix); }

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		uint8_t newMix = static_cast<uint8_t>(this->getValue());
		auto* mca = soundEditor.currentModControllable;

		// Reset state when turning effect on
		if (mca->automod.mix == 0 && newMix > 0) {
			mca->automod.resetState();
		}
		mca->automod.mix = newMix;
	}

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret menu: adjust gammaPhase (multiplier for all zone phase offsets)
			Buttons::selectButtonPressUsedUp = true;
			float& gamma = soundEditor.currentModControllable->automod.gammaPhase;
			gamma = std::max(0.0f, gamma + static_cast<float>(offset));
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
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	void getColumnLabel(StringBuf& label) override { label.append("Mix"); }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		if (this->getValue() == 0) {
			deluge::hid::display::OLED::main.drawStringCentered("OFF", slot.start_x,
			                                                    slot.start_y + kHorizontalMenuSlotYOffset,
			                                                    kTextSpacingX, kTextSpacingY, slot.width);
			return;
		}
		IntegerWithOff::renderInHorizontalMenu(slot);
	}

private:
	mutable bool suppressNotification_ = false;
};

// ============================================================================
// Page 1 Menu Items: Type, Flavor, Mod, Mix
// ============================================================================

/// Automod Depth: GLOBAL patched param for overall modulation depth with mod matrix support
/// Works in both Sound (patched) and GlobalEffectable (unpatched) contexts
class AutomodDepth final : public IntegerContinuous, public MenuItemWithCCLearning, public Automation {
public:
	using IntegerContinuous::IntegerContinuous;
	/// Compatibility constructor matching patched_param::Integer signature
	AutomodDepth(l10n::String name, l10n::String title, int32_t /*paramId*/) : IntegerContinuous(name, title) {}

	// === Value read/write with dual context support ===

	void readCurrentValue() override {
		q31_t value;
		if (soundEditor.currentParamManager->hasPatchedParamSet()) {
			value = soundEditor.currentParamManager->getPatchedParamSet()->getValue(params::GLOBAL_AUTOMOD_DEPTH);
		}
		else {
			value = soundEditor.currentParamManager->getUnpatchedParamSet()->getValue(params::UNPATCHED_AUTOMOD_DEPTH);
		}
		// Bipolar: -50 = 0% depth, 0 = 100% depth, +50 = 200% depth
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
			return modelStack->getPatchedAutoParamFromId(params::GLOBAL_AUTOMOD_DEPTH);
		}
		return modelStack->getUnpatchedAutoParamFromId(params::UNPATCHED_AUTOMOD_DEPTH);
	}

	// === CC Learning with dual context support ===

	ParamDescriptor getLearningThing() override {
		ParamDescriptor paramDescriptor;
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			// Unpatched context (kit, audio clip)
			paramDescriptor.setToHaveParamOnly(params::UNPATCHED_AUTOMOD_DEPTH + params::UNPATCHED_START);
		}
		else {
			// Patched context (synth, MIDI)
			paramDescriptor.setToHaveParamOnly(params::GLOBAL_AUTOMOD_DEPTH);
		}
		return paramDescriptor;
	}

	void unlearnAction() final { MenuItemWithCCLearning::unlearnAction(); }
	bool allowsLearnMode() final { return MenuItemWithCCLearning::allowsLearnMode(); }
	void learnKnob(MIDICable* cable, int32_t whichKnob, int32_t modKnobMode, int32_t midiChannel) final {
		MenuItemWithCCLearning::learnKnob(cable, whichKnob, modKnobMode, midiChannel);
	}

	// === LFO mode toggle (push to cycle: Stop → Once → Retrig → Free) ===

	MenuItem* selectButtonPress() override {
		// If shift held down, user wants to delete automation
		if (Buttons::isShiftButtonPressed()) {
			return Automation::selectButtonPress();
		}
		// Cycle through LFO modes
		cycleLfoMode();
		return NO_NAVIGATION;
	}

private:
	void cycleLfoMode() {
		auto& automod = soundEditor.currentModControllable->automod;
		// Cycle: STOP → ONCE → RETRIG → FREE → STOP
		switch (automod.lfoMode) {
		case deluge::dsp::AutomodLfoMode::STOP:
			automod.lfoMode = deluge::dsp::AutomodLfoMode::ONCE;
			display->displayPopup("ONCE");
			break;
		case deluge::dsp::AutomodLfoMode::ONCE:
			automod.lfoMode = deluge::dsp::AutomodLfoMode::RETRIG;
			display->displayPopup("RETRIG");
			break;
		case deluge::dsp::AutomodLfoMode::RETRIG:
			automod.lfoMode = deluge::dsp::AutomodLfoMode::FREE;
			display->displayPopup("FREE");
			break;
		case deluge::dsp::AutomodLfoMode::FREE:
		default:
			automod.lfoMode = deluge::dsp::AutomodLfoMode::STOP;
			display->displayPopup("STOP");
			break;
		}
		if (display->haveOLED()) {
			renderUIsForOled();
		}
	}

public:
	/// Handle patching source shortcut press (e.g., LFO1, LFO2, envelope shortcuts)
	MenuItem* patchingSourceShortcutPress(PatchSource s, bool previousPressStillActive = false) override {
		// In unpatched context, no patching available
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return nullptr;
		}
		// In patched context, open patch cable strength menu for this source
		soundEditor.patchingParamSelected = params::GLOBAL_AUTOMOD_DEPTH;
		source_selection::regularMenu.s = s;
		return &patch_cable_strength::regularMenu;
	}

	/// Blink shortcut if this source is patched to automod depth
	uint8_t shouldBlinkPatchingSourceShortcut(PatchSource s, uint8_t* colour) override {
		// In unpatched context, no patching - don't blink
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		// In patched context, check if source is patched to this param
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_AUTOMOD_DEPTH);
		return soundEditor.currentParamManager->getPatchCableSet()
		               ->isSourcePatchedToDestinationDescriptorVolumeInspecific(s, paramDescriptor)
		           ? 3
		           : 255;
	}

	/// Show dot on name if any source is patched to automod depth
	uint8_t shouldDrawDotOnName() override {
		// In unpatched context, no patching - no dot
		if (!soundEditor.currentParamManager->hasPatchedParamSet()) {
			return 255;
		}
		// In patched context, check if any source is patched
		ParamDescriptor paramDescriptor{};
		paramDescriptor.setToHaveParamOnly(params::GLOBAL_AUTOMOD_DEPTH);
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

	[[nodiscard]] int32_t getMinValue() const override { return kMinMenuValue; }
	[[nodiscard]] int32_t getMaxValue() const override { return kMaxMenuValue; }
	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return KNOB; }

	void getColumnLabel(StringBuf& label) override { label.append("Depth"); }
};

/// Automod Type: Zone-based DSP topology selector (8 zones)
/// Controls mix of filter/comb/tremolo stages
/// Uses ZoneBasedMenuItem with auto-wrap: turning past boundaries wraps and adjusts phase offset
/// Secret menu: Push+twist to manually adjust typePhaseOffset
class AutomodType final : public ZoneBasedMenuItem<kAutomodNumZones, kAutomodZoneResolution> {
public:
	using ZoneBasedMenuItem::ZoneBasedMenuItem;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->automod.type); }

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		soundEditor.currentModControllable->automod.type = static_cast<uint16_t>(this->getValue());
	}

	// Enable auto-wrap: turning past boundaries wraps and increments/decrements phase offset
	[[nodiscard]] bool supportsAutoWrap() const override { return true; }

	[[nodiscard]] float getPhaseOffset() const override {
		return soundEditor.currentModControllable->automod.typePhaseOffset;
	}

	void setPhaseOffset(float offset) override { soundEditor.currentModControllable->automod.typePhaseOffset = offset; }

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override { return getTypeName(zoneIndex); }

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret: push+twist manually adjusts typePhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->automod.typePhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(offset) * 1.0f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "T:%d", static_cast<int32_t>(std::floor(effectivePhaseOffset())));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			// Delegate to base class for auto-wrap handling
			ZoneBasedMenuItem::selectEncoderAction(offset);
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
		float effOffset = effectivePhaseOffset();
		if (effOffset != 0.0f) {
			cacheCoordDisplay(effOffset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			ZoneBasedMenuItem::renderInHorizontalMenu(slot);
		}
	}

protected:
	void drawPixelsForOled() override {
		float effOffset = effectivePhaseOffset();
		if (effOffset != 0.0f) {
			cacheCoordDisplay(effOffset, this->getValue());
			drawZoneForOled(this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			ZoneBasedMenuItem::drawPixelsForOled();
		}
	}

private:
	mutable bool suppressNotification_ = false;

	// Effective phase offset includes per-knob offset + gamma contribution (matching scatter pattern)
	[[nodiscard]] float effectivePhaseOffset() const {
		auto& automod = soundEditor.currentModControllable->automod;
		return automod.typePhaseOffset + static_cast<float>(kAutomodZoneResolution) * automod.gammaPhase;
	}

	// Shared coordinate display buffer
	static inline char coordBuffer_[12] = {};
	static void cacheCoordDisplay(float phaseOffset, int32_t value) {
		int32_t p = static_cast<int32_t>(std::floor(phaseOffset));
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

/// Automod Flavor: Zone-based filter character control (8 zones)
/// Controls LP/BP/HP mix via phi triangles
/// Uses ZoneBasedMenuItem with auto-wrap: turning past boundaries wraps and adjusts phase offset
/// Secret menu: Push+twist to manually adjust flavorPhaseOffset
class AutomodFlavor final : public ZoneBasedMenuItem<kAutomodNumZones, kAutomodZoneResolution> {
public:
	using ZoneBasedMenuItem::ZoneBasedMenuItem;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->automod.flavor); }

	bool usesAffectEntire() override { return true; }

	void writeCurrentValue() override {
		soundEditor.currentModControllable->automod.flavor = static_cast<uint16_t>(this->getValue());
	}

	// Enable auto-wrap: turning past boundaries wraps and increments/decrements phase offset
	[[nodiscard]] bool supportsAutoWrap() const override { return true; }

	[[nodiscard]] float getPhaseOffset() const override {
		return soundEditor.currentModControllable->automod.flavorPhaseOffset;
	}

	void setPhaseOffset(float offset) override {
		soundEditor.currentModControllable->automod.flavorPhaseOffset = offset;
	}

	[[nodiscard]] const char* getZoneName(int32_t zoneIndex) const override { return getFlavorName(zoneIndex); }

	void selectEncoderAction(int32_t offset) override {
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)) {
			// Secret: push+twist manually adjusts flavorPhaseOffset
			Buttons::selectButtonPressUsedUp = true;
			float& phase = soundEditor.currentModControllable->automod.flavorPhaseOffset;
			phase = std::max(0.0f, phase + static_cast<float>(offset) * 1.0f);
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "F:%d", static_cast<int32_t>(std::floor(effectivePhaseOffset())));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			// Delegate to base class for auto-wrap handling
			ZoneBasedMenuItem::selectEncoderAction(offset);
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
		float effOffset = effectivePhaseOffset();
		if (effOffset != 0.0f) {
			cacheCoordDisplay(effOffset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			ZoneBasedMenuItem::renderInHorizontalMenu(slot);
		}
	}

protected:
	void drawPixelsForOled() override {
		float effOffset = effectivePhaseOffset();
		if (effOffset != 0.0f) {
			cacheCoordDisplay(effOffset, this->getValue());
			drawZoneForOled(this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			ZoneBasedMenuItem::drawPixelsForOled();
		}
	}

private:
	mutable bool suppressNotification_ = false;

	// Effective phase offset includes per-knob offset + gamma contribution (matching scatter pattern)
	[[nodiscard]] float effectivePhaseOffset() const {
		auto& automod = soundEditor.currentModControllable->automod;
		return automod.flavorPhaseOffset + static_cast<float>(kAutomodZoneResolution) * automod.gammaPhase;
	}

	// Shared coordinate display buffer
	static inline char coordBuffer_[12] = {};
	static void cacheCoordDisplay(float phaseOffset, int32_t value) {
		int32_t p = static_cast<int32_t>(std::floor(phaseOffset));
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
			snprintf(buffer, sizeof(buffer), "M:%d", static_cast<int32_t>(std::floor(effectivePhaseOffset())));
			display->displayPopup(buffer);
			renderUIsForOled();
			suppressNotification_ = true;
		}
		else {
			// Auto-wrap: turning past boundaries wraps and adjusts phase offset
			int32_t scaledOffset = velocity_.getScaledOffset(offset);
			int32_t newValue = this->getValue() + scaledOffset;
			float& phaseOffset = soundEditor.currentModControllable->automod.modPhaseOffset;

			if (newValue >= kAutomodZoneResolution) {
				// Wrap past max: go to start and increment phase offset
				this->setValue(newValue - kAutomodZoneResolution);
				phaseOffset += 1.0f;
			}
			else if (newValue < 0) {
				// Wrap past min: go to end and decrement phase offset (floor at 0)
				if (phaseOffset >= 1.0f) {
					this->setValue(newValue + kAutomodZoneResolution);
					phaseOffset -= 1.0f;
				}
				else {
					// At phaseOffset=0, clamp at min (can't go negative)
					this->setValue(0);
				}
			}
			else {
				this->setValue(newValue);
			}
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
		float effOffset = effectivePhaseOffset();
		// Phi triangle mode: show coordinate display
		if (effOffset != 0.0f) {
			cacheCoordDisplay(effOffset, this->getValue());
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones, getCoordName);
		}
		else {
			renderZoneInHorizontalMenu(slot, this->getValue(), kAutomodZoneResolution, kAutomodNumZones,
			                           [](int32_t z) { return getModName(z); });
		}
	}

protected:
	void drawPixelsForOled() override {
		float effOffset = effectivePhaseOffset();
		// Phi triangle mode: show coordinate display
		if (effOffset != 0.0f) {
			cacheCoordDisplay(effOffset, this->getValue());
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

	// Effective phase offset includes per-knob offset + gamma contribution (matching scatter pattern)
	[[nodiscard]] float effectivePhaseOffset() const {
		auto& automod = soundEditor.currentModControllable->automod;
		return automod.modPhaseOffset + static_cast<float>(kAutomodZoneResolution) * automod.gammaPhase;
	}

	// Shared coordinate display buffer
	static inline char coordBuffer_[12] = {};
	static void cacheCoordDisplay(float phaseOffset, int32_t value) {
		int32_t p = static_cast<int32_t>(std::floor(phaseOffset));
		int32_t z = value >> 7; // 0-1023 → 0-7 (zone index)
		snprintf(coordBuffer_, sizeof(coordBuffer_), "%d:%d", p, z);
	}
	static const char* getCoordName([[maybe_unused]] int32_t zoneIndex) { return coordBuffer_; }

	static const char* getModName(int32_t zoneIndex) {
		// States/motion names (complements Type=chakra colors, Flavor=weather)
		switch (zoneIndex) {
		case 0:
			return "Rest";
		case 1:
			return "Calm";
		case 2:
			return "Dream";
		case 3:
			return "Wake";
		case 4:
			return "Rise";
		case 5:
			return "Soar";
		case 6:
			return "Peak";
		case 7:
			return "Void";
		default:
			return "?";
		}
	}
};

} // namespace deluge::gui::menu_item::fx
