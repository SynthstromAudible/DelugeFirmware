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
#include <algorithm>

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

/// pWrite param for looper scatter modes, or Scale for Pitch mode
/// Non-Pitch modes: buffer write-back probability (0=freeze, 50=always write)
/// Pitch mode: scale selection (0-50 maps to 12 scales)
class ScatterModeParam final : public IntegerContinuous {
public:
	using IntegerContinuous::IntegerContinuous;

	static constexpr const char* kScaleNames[] = {
	    "Chromatic", "Major",   "Minor",  "MajPent", "MinPent", "Blues",
	    "Dorian",    "Mixolyd", "MajTri", "MinTri",  "Sus4",    "Dim",
	};
	static constexpr const char* kScaleShort[] = {
	    "Chr", "Maj", "Min", "Ma5", "Mi5", "Blu", "Dor", "Mix", "MAJ", "MIN", "Su4", "Dim",
	};
	static constexpr int32_t kNumScales = 12;

	bool isPitchMode() const {
		return soundEditor.currentModControllable->stutterConfig.scatterMode == ScatterMode::Pitch;
	}

	void readCurrentValue() override {
		if (isPitchMode()) {
			this->setValue(soundEditor.currentModControllable->stutterConfig.pitchScaleParam);
		}
		else {
			this->setValue(soundEditor.currentModControllable->stutterConfig.pWriteParam);
		}
	}

	void writeCurrentValue() override {
		uint8_t value = static_cast<uint8_t>(this->getValue());

		if (isPitchMode()) {
			if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
				Kit* kit = getCurrentKit();
				for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
					if (thisDrum->type == DrumType::SOUND) {
						auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
						if (soundDrum->stutterConfig.scatterMode == ScatterMode::Pitch) {
							soundDrum->stutterConfig.pitchScaleParam = value;
						}
					}
				}
			}
			else {
				soundEditor.currentModControllable->stutterConfig.pitchScaleParam = value;
			}
			stutterer.setLivePitchScale(value);
		}
		else {
			if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
				Kit* kit = getCurrentKit();
				for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
					if (thisDrum->type == DrumType::SOUND) {
						auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
						if (soundDrum->stutterConfig.isLooperMode()) {
							soundDrum->stutterConfig.pWriteParam = value;
						}
					}
				}
			}
			else {
				soundEditor.currentModControllable->stutterConfig.pWriteParam = value;
			}
			stutterer.setLivePWrite(value);
		}
	}

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode != ScatterMode::Classic && mode != ScatterMode::Burst;
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 50; }

	void getColumnLabel(StringBuf& label) override {
		if (isPitchMode()) {
			label.append("Scal");
		}
		else {
			label.append("pWrt");
		}
	}

	std::string_view getTitle() const override { return getName(); }

	[[nodiscard]] std::string_view getName() const override {
		if (isPitchMode()) {
			return "Scale";
		}
		return "pWrite";
	}

	void getNotificationValue(StringBuf& valueBuf) override {
		int32_t val = this->getValue();
		if (isPitchMode()) {
			int32_t idx = (val * (kNumScales - 1)) / 50;
			idx = std::clamp(idx, int32_t{0}, kNumScales - 1);
			valueBuf.append(kScaleShort[idx]);
		}
		else {
			int32_t writePercent = (val * 100) / 50;
			valueBuf.appendInt(writePercent);
			valueBuf.append("%");
		}
	}
};

} // namespace deluge::gui::menu_item::stutter
