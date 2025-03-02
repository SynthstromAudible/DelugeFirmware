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
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::arpeggiator {
class ArpMpeVelocity final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.currentArpSettings->mpeVelocity); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<ArpMpeModSource>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				// Note: we need to apply the same filtering as stated in the isRelevant() function
				if (thisDrum->type != DrumType::GATE) {
					thisDrum->arpSettings.mpeVelocity = current_value;
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentArpSettings->mpeVelocity = current_value;
		}
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		return !soundEditor.editingGateDrumRow();
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_DISABLED),                //<
		    l10n::getView(STRING_FOR_PATCH_SOURCE_AFTERTOUCH), //<
		    l10n::getView(STRING_FOR_PATCH_SOURCE_Y),          //<
		};
	}
};
} // namespace deluge::gui::menu_item::arpeggiator
