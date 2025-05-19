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
#include "util/misc.h"

namespace deluge::gui::menu_item::voice {
class Priority final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(*soundEditor.currentPriority); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<VoicePriority>();
		*soundEditor.currentPriority = current_value;
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->voicePriority = current_value;
				}
			}
		}
		// Or, the normal case of just one sound (or audio clip)
		else {
			*soundEditor.currentPriority = current_value;
		}
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		return {
		    l10n::getView(l10n::String::STRING_FOR_LOW),
		    l10n::getView(optType == OptType::SHORT ? l10n::String::STRING_FOR_MEDIUM_SHORT
		                                            : l10n::String::STRING_FOR_MEDIUM),
		    l10n::getView(l10n::String::STRING_FOR_HIGH),
		};
	}
};
} // namespace deluge::gui::menu_item::voice
