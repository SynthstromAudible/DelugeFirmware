/*
 * Copyright © 2024 Synthstrom Audible Limited
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
#include "gui/ui/sound_editor.h"
#include "model/settings/runtime_feature_settings.h"

namespace deluge::gui::menu_item::harmonizer {
class Probability final : public Integer {
public:
	using Integer::Integer;

	void readCurrentValue() override { this->setValue(soundEditor.currentHarmonizerSettings->probability); }

	void writeCurrentValue() override {
		soundEditor.currentHarmonizerSettings->probability = static_cast<uint8_t>(this->getValue());
	}

	[[nodiscard]] int32_t getMinValue() const override { return 0; }
	[[nodiscard]] int32_t getMaxValue() const override { return 100; }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.currentHarmonizerSettings != nullptr && soundEditor.editingCVOrMIDIClip()
		       && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::MidiHarmonizer);
	}
};
} // namespace deluge::gui::menu_item::harmonizer
