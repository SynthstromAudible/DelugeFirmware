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

/// Slider-based mode param for scatter modes (pWrite for Leaky, scale for Pitch, placeholders for others)
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

	/// Get current scatter mode
	ScatterMode currentMode() const { return soundEditor.currentModControllable->stutterConfig.scatterMode; }

	void readCurrentValue() override {
		auto mode = currentMode();
		switch (mode) {
		case ScatterMode::Leaky:
			// pWrite: read leakyWriteProb as 0-50 value
			this->setValue(
			    static_cast<int32_t>(soundEditor.currentModControllable->stutterConfig.leakyWriteProb * 50.0f));
			break;
		case ScatterMode::Pitch:
			// Scale: read pitchScale as 0-7
			this->setValue(soundEditor.currentModControllable->stutterConfig.pitchScale);
			break;
		default:
			// Placeholder for other modes - read as 25 (middle)
			this->setValue(25);
			break;
		}
	}

	void writeCurrentValue() override {
		auto mode = currentMode();
		switch (mode) {
		case ScatterMode::Leaky: {
			// pWrite: write leakyWriteProb
			float prob = static_cast<float>(this->getValue()) / 50.0f;
			if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
				Kit* kit = getCurrentKit();
				for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
					if (thisDrum->type == DrumType::SOUND) {
						auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
						if (soundDrum->stutterConfig.scatterMode == ScatterMode::Leaky) {
							soundDrum->stutterConfig.leakyWriteProb = prob;
						}
					}
				}
			}
			else {
				soundEditor.currentModControllable->stutterConfig.leakyWriteProb = prob;
			}
			// Also update global stutterer for live changes
			stutterer.setLiveLeakyWriteProb(prob);
			break;
		}
		case ScatterMode::Pitch: {
			// Scale: write pitchScale
			uint8_t scale = static_cast<uint8_t>(this->getValue());
			if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
				Kit* kit = getCurrentKit();
				for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
					if (thisDrum->type == DrumType::SOUND) {
						auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
						if (soundDrum->stutterConfig.scatterMode == ScatterMode::Pitch) {
							soundDrum->stutterConfig.pitchScale = scale;
						}
					}
				}
			}
			else {
				soundEditor.currentModControllable->stutterConfig.pitchScale = scale;
			}
			// Also update global stutterer for live changes
			stutterer.setLivePitchScale(scale);
			break;
		}
		default:
			// Placeholder for other modes - no-op for now
			break;
		}
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
		// Pitch mode uses 0-7 for scale, others use 0-50
		return (currentMode() == ScatterMode::Pitch) ? (kNumScales - 1) : 50;
	}

	void getColumnLabel(StringBuf& label) override {
		switch (currentMode()) {
		case ScatterMode::Leaky:
			label.append("pWrite");
			break;
		case ScatterMode::Repeat:
			label.append("Rept"); // Placeholder
			break;
		case ScatterMode::Time:
			label.append("Time"); // Placeholder
			break;
		case ScatterMode::Shuffle:
			label.append("Shuf"); // Placeholder
			break;
		case ScatterMode::Pitch:
			label.append("Scale");
			break;
		case ScatterMode::Pattern:
			label.append("Patn"); // Placeholder
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
		default:
			// For placeholder modes, just use mode name
			return l10n::getView(l10n::String::STRING_FOR_SCATTER_MODE);
		}
	}

	// Override to show scale name instead of number for Pitch mode
	void drawValue() override {
		if (currentMode() == ScatterMode::Pitch) {
			int32_t val = this->getValue();
			int32_t idx = (val < 0) ? 0 : (val >= kNumScales) ? (kNumScales - 1) : val;
			display->setTextAsNumber(idx); // Could show name but 7seg is limited
		}
		else {
			IntegerContinuous::drawValue();
		}
	}

	// Override notification to show scale name for Pitch mode
	void getNotificationValue(StringBuf& valueBuf) override {
		if (currentMode() == ScatterMode::Pitch) {
			int32_t val = this->getValue();
			int32_t idx = (val < 0) ? 0 : (val >= kNumScales) ? (kNumScales - 1) : val;
			valueBuf.append(kScaleShort[idx]);
		}
		else {
			IntegerContinuous::getNotificationValue(valueBuf);
		}
	}
};

} // namespace deluge::gui::menu_item::stutter
