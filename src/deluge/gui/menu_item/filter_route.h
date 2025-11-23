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
#include "model/instrument/kit.h"
#include "model/mod_controllable/mod_controllable_audio.h"
#include "model/song/song.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item {
class FilterRouting final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue<::FilterRoute>(soundEditor.currentModControllable->filterRoute); }

	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<::FilterRoute>();

		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					// Note: we need to apply the same filtering as stated in the isRelevant() function
					if (soundDrum->lpfMode != FilterMode::OFF && soundDrum->hpfMode != FilterMode::OFF) {
						soundDrum->filterRoute = current_value;
					}
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentModControllable->filterRoute = current_value;
		}
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		return {l10n::getView(l10n::String::STRING_FOR_HPF_TO_LPF), l10n::getView(l10n::String::STRING_FOR_LPF_TO_HPF),
		        l10n::getView(l10n::String::STRING_FOR_PARALLEL)};
	}

	[[nodiscard]] int32_t getOccupiedSlots() const override { return 4; }
	[[nodiscard]] bool showColumnLabel() const override { return false; }
	[[nodiscard]] bool showNotification() const override { return false; }

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		OLED::main.drawHorizontalLine(kScreenTitleSeparatorY, 0, OLED_MAIN_WIDTH_PIXELS - 1);
		drawPixelsForOled();
	}
};
} // namespace deluge::gui::menu_item
