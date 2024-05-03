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

#include "definitions_cxx.hpp"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::voice {
class VoiceCount : public Integer {
public:
	using Integer::Integer;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->maxVoiceCount); }
	void writeCurrentValue() override { soundEditor.currentSound->maxVoiceCount = this->getValue(); }
	[[nodiscard]] int32_t getMinValue() const override { return 1; }
	[[nodiscard]] int32_t getMaxValue() const override { return 16; }
};

extern VoiceCount polyphonicVoiceCountMenu;

class PolyphonyType final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentSound->polyphonic); }
	void writeCurrentValue() override {
		auto current_value = this->getValue<PolyphonyMode>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKit()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->polyphonic = current_value;
				}
			}
		}

		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->polyphonic = current_value;
		}
	}

	deluge::vector<std::string_view> getOptions() override {
		deluge::vector<std::string_view> options = {
		    l10n::getView(l10n::String::STRING_FOR_AUTO),
		    l10n::getView(l10n::String::STRING_FOR_POLYPHONIC),
		    l10n::getView(l10n::String::STRING_FOR_MONOPHONIC),
		    l10n::getView(l10n::String::STRING_FOR_LEGATO),
		};

		if (soundEditor.editingKit()) {
			options.push_back(l10n::getView(l10n::String::STRING_FOR_CHOKE));
		}
		return options;
	}
	MenuItem* selectButtonPress() override {
		if (this->getValue<PolyphonyMode>() == PolyphonyMode::POLY) {
			return &polyphonicVoiceCountMenu;
		}
		return Selection::selectButtonPress();
	}

	bool usesAffectEntire() override { return true; }
};
} // namespace deluge::gui::menu_item::voice
