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
#include "gui/l10n/strings.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "io/midi/harmonizer_settings.h"
#include "model/settings/runtime_feature_settings.h"

namespace deluge::gui::menu_item::harmonizer {
class Interval final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		this->setValue(static_cast<int32_t>(soundEditor.currentHarmonizerSettings->interval));
	}

	void writeCurrentValue() override {
		soundEditor.currentHarmonizerSettings->interval = static_cast<DiatonicInterval>(this->getValue());
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_HARMONIZER_INT_OFF),       l10n::getView(STRING_FOR_HARMONIZER_INT_3RD_ABOVE),
		    l10n::getView(STRING_FOR_HARMONIZER_INT_3RD_BELOW), l10n::getView(STRING_FOR_HARMONIZER_INT_6TH_ABOVE),
		    l10n::getView(STRING_FOR_HARMONIZER_INT_6TH_BELOW), l10n::getView(STRING_FOR_HARMONIZER_INT_OCTAVE),
		};
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.currentHarmonizerSettings != nullptr && soundEditor.editingCVOrMIDIClip()
		       && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::MidiHarmonizer);
	}
};
} // namespace deluge::gui::menu_item::harmonizer
