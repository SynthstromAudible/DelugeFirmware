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
#include "gui/menu_item/integer.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/fx/stutterer.h"
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::stutter {

/// Density control for scatter modes - controls output dry/wet probability
/// CCW (0) = all dry output (hear input, no grains)
/// CW (50) = normal grain playback (hash decides)
class ScatterDensity final : public IntegerContinuous {
public:
	using IntegerContinuous::IntegerContinuous;

	/// Get current scatter mode
	ScatterMode currentMode() const { return soundEditor.currentModControllable->stutterConfig.scatterMode; }

	void readCurrentValue() override { this->setValue(soundEditor.currentModControllable->stutterConfig.densityParam); }

	void writeCurrentValue() override {
		uint8_t value = static_cast<uint8_t>(this->getValue());

		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {
			Kit* kit = getCurrentKit();
			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					// Only update drums in looper modes
					if (soundDrum->stutterConfig.isLooperMode()) {
						soundDrum->stutterConfig.densityParam = value;
					}
				}
			}
		}
		else {
			soundEditor.currentModControllable->stutterConfig.densityParam = value;
		}

		// Update global stutterer for live changes
		stutterer.setLiveDensity(value);
	}

	bool usesAffectEntire() override { return true; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		// Only relevant for looper modes (not Classic/Burst)
		return soundEditor.currentModControllable->stutterConfig.isLooperMode();
	}

	// === Display configuration ===
	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 50; }

	void getColumnLabel(StringBuf& label) override { label.append("Dens"); }

	std::string_view getTitle() const override { return getName(); }

	[[nodiscard]] std::string_view getName() const override { return "Density"; }

	// Override notification to show percentage
	void getNotificationValue(StringBuf& valueBuf) override {
		int32_t val = this->getValue();
		int32_t percent = (val * 100) / 50;
		valueBuf.appendInt(percent);
		valueBuf.append("%");
	}
};

} // namespace deluge::gui::menu_item::stutter
