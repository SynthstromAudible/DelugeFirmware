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
class Tightness final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override {
		this->setValue(static_cast<int32_t>(soundEditor.currentHarmonizerSettings->tightness));
	}

	void writeCurrentValue() override {
		soundEditor.currentHarmonizerSettings->tightness = static_cast<HarmonizerTightness>(this->getValue());
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_HARMONIZER_TARGET_CHORD_TONES),
		    l10n::getView(STRING_FOR_SCALE),
		    l10n::getView(STRING_FOR_HARMONIZER_LOOSE),
		    l10n::getView(STRING_FOR_HARMONIZER_EXTENSIONS),
		};
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return soundEditor.currentHarmonizerSettings != nullptr && soundEditor.editingCVOrMIDIClip()
		       && runtimeFeatureSettings.isOn(RuntimeFeatureSettingType::MidiHarmonizer);
	}
};
} // namespace deluge::gui::menu_item::harmonizer
