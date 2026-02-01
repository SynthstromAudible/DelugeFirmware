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
#include "gui/menu_item/toggle.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/fx/stutterer.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

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
		// Only relevant for Classic and Burst modes
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode == ScatterMode::Classic || mode == ScatterMode::Burst;
	}
};

/// Unified mode-specific param for all scatter modes (stored in modeSpecificParam)
/// Each mode interprets the 0-50 value differently:
/// - Leaky: pWrite probability (0-50 → 0%-100%)
/// - Pitch: scale selection (0-50 → 0-11 scales)
/// - Pattern: pattern selection (0-50 → 0-7 patterns)
/// - Repeat: density control (0-50)
/// - Time: phrase multiplier (0-50)
/// - Shuffle: intensity (0-50)
class ScatterModeParam final : public IntegerContinuous {
public:
	using IntegerContinuous::IntegerContinuous;

	// Scale names for Pitch mode (scales + triads)
	static constexpr const char* kScaleNames[] = {
	    "Chromatic", "Major",   "Minor",  "MajPent", "MinPent", "Blues",
	    "Dorian",    "Mixolyd", "MajTri", "MinTri",  "Sus4",    "Dim",
	};
	static constexpr const char* kScaleShort[] = {
	    "Chr", "Maj", "Min", "Ma5", "Mi5", "Blu", "Dor", "Mix", "MAJ", "MIN", "Su4", "Dim",
	};
	static constexpr int32_t kNumScales = 12;

	// Pattern names for Pattern mode
	static constexpr const char* kPatternNames[] = {
	    "Seq", "Weave", "Skip", "Mirror", "Pairs", "Reverse", "Thirds", "Spiral",
	};
	static constexpr const char* kPatternShort[] = {
	    "Seq", "Wea", "Skp", "Mir", "Pai", "Rev", "3rd", "Spi",
	};
	static constexpr int32_t kNumPatterns = 8;

	/// Get current scatter mode
	ScatterMode currentMode() const { return soundEditor.currentModControllable->stutterConfig.scatterMode; }

	void readCurrentValue() override {
		// All modes use the unified modeSpecificParam
		this->setValue(soundEditor.currentModControllable->stutterConfig.modeSpecificParam);
	}

	void writeCurrentValue() override {
		uint8_t value = static_cast<uint8_t>(this->getValue());

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					// Only update drums in the same mode
					if (soundDrum->stutterConfig.scatterMode == currentMode()) {
						soundDrum->stutterConfig.modeSpecificParam = value;
					}
				}
			}
		}
		else {
			soundEditor.currentModControllable->stutterConfig.modeSpecificParam = value;
		}

		// Update global stutterer for live changes
		stutterer.setLiveModeParam(value);
	}

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Only relevant for modes that use a slider param (not Classic/Burst which use Toggle)
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode != ScatterMode::Classic && mode != ScatterMode::Burst;
	}

	// === Display configuration ===
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override {
		// All modes use 0-50 range
		return 50;
	}

	void getColumnLabel(StringBuf& label) override {
		switch (currentMode()) {
		case ScatterMode::Leaky:
			label.append("pWrit");
			break;
		case ScatterMode::Repeat:
			label.append("Reps");
			break;
		case ScatterMode::Time:
			label.append("Reps");
			break;
		case ScatterMode::Shuffle:
			label.append("Dry");
			break;
		case ScatterMode::Pitch:
			label.append("Scale");
			break;
		case ScatterMode::Pattern:
			label.append("Reps");
			break;
		default:
			label.append("---");
			break;
		}
	}

	std::string_view getTitle() const override { return getName(); }

	// Override getName to return dynamic name based on mode
	[[nodiscard]] std::string_view getName() const override {
		switch (currentMode()) {
		case ScatterMode::Leaky:
			return l10n::getView(l10n::String::STRING_FOR_SCATTER_PWRITE);
		case ScatterMode::Pitch:
			return "Scale";
		case ScatterMode::Pattern:
			return "Repeats";
		case ScatterMode::Repeat:
			return "Repeats";
		case ScatterMode::Time:
			return "Repeats";
		case ScatterMode::Shuffle:
			return "Dry Prob";
		default:
			return l10n::getView(l10n::String::STRING_FOR_SCATTER_MODE);
		}
	}

	/// Get repeat count from value (power of 2: 1,2,4,8,16,32)
	static int32_t getRepeatCountFromValue(int32_t val) {
		if (val <= 8) {
			return 1;
		}
		if (val <= 16) {
			return 2;
		}
		if (val <= 25) {
			return 4;
		}
		if (val <= 33) {
			return 8;
		}
		if (val <= 42) {
			return 16;
		}
		return 32;
	}

	// Override to show descriptive values for each mode
	void drawValue() override {
		auto mode = currentMode();
		int32_t val = this->getValue();

		if (mode == ScatterMode::Pitch) {
			// Map 0-50 to 0-11 scale index
			int32_t idx = (val * (kNumScales - 1)) / 50;
			idx = std::clamp(idx, int32_t{0}, kNumScales - 1);
			display->setTextAsNumber(idx);
		}
		else if (mode == ScatterMode::Repeat || mode == ScatterMode::Time || mode == ScatterMode::Pattern) {
			// Show power-of-2 repeat count
			display->setTextAsNumber(getRepeatCountFromValue(val));
		}
		else {
			IntegerContinuous::drawValue();
		}
	}

	// Override notification to show descriptive values
	void getNotificationValue(StringBuf& valueBuf) override {
		auto mode = currentMode();
		int32_t val = this->getValue();

		if (mode == ScatterMode::Pitch) {
			int32_t idx = (val * (kNumScales - 1)) / 50;
			idx = std::clamp(idx, int32_t{0}, kNumScales - 1);
			valueBuf.append(kScaleShort[idx]);
		}
		else if (mode == ScatterMode::Repeat || mode == ScatterMode::Time || mode == ScatterMode::Pattern) {
			// Show "Nx" format for repeat count
			valueBuf.appendInt(getRepeatCountFromValue(val));
			valueBuf.append("x");
		}
		else if (mode == ScatterMode::Shuffle) {
			// Show percentage for dry probability
			int32_t percent = (val * 100) / 50;
			valueBuf.appendInt(percent);
			valueBuf.append("%");
		}
		else {
			IntegerContinuous::getNotificationValue(valueBuf);
		}
	}
};

} // namespace deluge::gui::menu_item::stutter
