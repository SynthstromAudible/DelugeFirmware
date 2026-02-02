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

/// pWrite param for all looper scatter modes (buffer write-back probability)
class ScatterPWrite final : public IntegerContinuous {
public:
	using IntegerContinuous::IntegerContinuous;

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->stutterConfig.pWriteParam); }

	void writeCurrentValue() override {
		uint8_t value = static_cast<uint8_t>(this->getValue());
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

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		auto mode = soundEditor.currentModControllable->stutterConfig.scatterMode;
		return mode != ScatterMode::Classic && mode != ScatterMode::Burst;
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 50; }

	void getColumnLabel(StringBuf& label) override { label.append("pWrt"); }
	std::string_view getTitle() const override { return "pWrite"; }
	[[nodiscard]] std::string_view getName() const override { return "pWrite"; }

	void getNotificationValue(StringBuf& valueBuf) override {
		int32_t writePercent = (this->getValue() * 100) / 50;
		valueBuf.appendInt(writePercent);
		valueBuf.append("%");
	}
};

/// Scale selection for Pitch mode only
class PitchScale final : public IntegerContinuous {
public:
	using IntegerContinuous::IntegerContinuous;

	static constexpr const char* kScaleShort[] = {
	    "Chr", "Maj", "Min", "Ma5", "Mi5", "Blu", "Dor", "Mix", "MAJ", "MIN", "Su4", "Dim", "+1",
	    "+2",  "+3",  "+4",  "+5",  "+6",  "+7",  "+8",  "+9",  "+10", "+11", "+12", "+13",
	};
	static constexpr int32_t kNumScales = 25;

	void readCurrentValue() override {
		this->setValue(soundEditor.currentModControllable->stutterConfig.pitchScaleParam);
	}

	void writeCurrentValue() override {
		uint8_t value = static_cast<uint8_t>(this->getValue());
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

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.currentModControllable->stutterConfig.scatterMode == ScatterMode::Pitch;
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 50; }

	void getColumnLabel(StringBuf& label) override { label.append("Scal"); }
	std::string_view getTitle() const override { return "Scale"; }
	[[nodiscard]] std::string_view getName() const override { return "Scale"; }

	void getNotificationValue(StringBuf& valueBuf) override {
		int32_t idx = (this->getValue() * (kNumScales - 1)) / 50;
		idx = std::clamp(idx, int32_t{0}, kNumScales - 1);
		valueBuf.append(kScaleShort[idx]);
	}
};

} // namespace deluge::gui::menu_item::stutter
