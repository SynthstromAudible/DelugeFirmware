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
#include "gui/ui/sound_editor.h"
#include "model/drum/drum.h"
#include "model/instrument/kit.h"
#include "model/song/song.h"
#include "processing/sound/sound.h"
#include "processing/sound/sound_drum.h"

namespace deluge::gui::menu_item::lfo {

class Type final : public Selection, FormattedTitle {
public:
	Type(l10n::String name, l10n::String title, uint8_t lfoId)
	    : Selection(name, title), FormattedTitle(title, lfoId + 1), lfoId_(lfoId) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		using enum l10n::String;
		bool shortOpt = optType == OptType::SHORT;
		return {
		    l10n::getView(STRING_FOR_SINE),
		    l10n::getView(STRING_FOR_TRIANGLE),
		    l10n::getView(STRING_FOR_SQUARE),
		    l10n::getView(STRING_FOR_SAW),
		    l10n::getView(STRING_FOR_SAMPLE_AND_HOLD),
		    l10n::getView(shortOpt ? STRING_FOR_RANDOM_WALK_SHORT : STRING_FOR_RANDOM_WALK),
		    l10n::getView(STRING_FOR_WARBLE),
		};
	}

	void readCurrentValue() override { this->setValue(soundEditor.currentSound->lfoConfig[lfoId_].waveType); }
	bool usesAffectEntire() override { return true; }
	void writeCurrentValue() override {
		auto current_value = this->getValue<LFOType>();
		// If affect-entire button held, do whole kit
		if (currentUIMode == UI_MODE_HOLDING_AFFECT_ENTIRE_IN_SOUND_EDITOR && soundEditor.editingKitRow()) {

			Kit* kit = getCurrentKit();

			for (Drum* thisDrum = kit->firstDrum; thisDrum != nullptr; thisDrum = thisDrum->next) {
				if (thisDrum->type == DrumType::SOUND) {
					auto* soundDrum = static_cast<SoundDrum*>(thisDrum);
					soundDrum->lfoConfig[lfoId_].waveType = current_value;
					// This fires unnecessarily for LFO2 assignments as well, but that's ok. It's not
					// entirely clear if we really need this for the LFO1, even: maybe the clock-driven resyncs
					// would be enough?
					soundDrum->resyncGlobalLFOs();
				}
			}
		}
		// Or, the normal case of just one sound
		else {
			soundEditor.currentSound->lfoConfig[lfoId_].waveType = current_value;
			// This fires unnecessarily for LFO2 assignments as well, but that's ok. It's not
			// entirely clear if we really need this for the LFO1, even: maybe the clock-driven resyncs
			// would be enough?
			soundEditor.currentSound->resyncGlobalLFOs();
		}
	}

	void configureRenderingOptions(const HorizontalMenuRenderingOptions& options) override {
		options.show_label = false;
	}

	void renderInHorizontalMenu(const HorizontalMenuSlotPosition& slot) override {
		oled_canvas::Canvas& image = OLED::main;

		const Icon& icon = [&] {
			switch (soundEditor.currentSound->lfoConfig[lfoId_].waveType) {
			case LFOType::SINE:
				return OLED::sineIcon;
			case LFOType::TRIANGLE:
				return OLED::triangleIcon;
			case LFOType::SQUARE:
				return OLED::squareIcon;
			case LFOType::SAW:
				return OLED::sawIcon;
			case LFOType::SAMPLE_AND_HOLD:
				return OLED::sampleHoldIcon;
			case LFOType::RANDOM_WALK:
				return OLED::randomWalkIcon;
			case LFOType::WARBLER:
				return OLED::warblerIcon;
			}
			return OLED::sineIcon;
		}();

		image.drawIconCentered(icon, slot.start_x, slot.width, slot.start_y + kHorizontalMenuSlotYOffset + 2);
	}

private:
	uint8_t lfoId_;
};

} // namespace deluge::gui::menu_item::lfo
