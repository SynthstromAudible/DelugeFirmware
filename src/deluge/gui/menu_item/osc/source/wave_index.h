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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/source/patched_param.h"
#include "hid/buttons.h"
#include "hid/display/display.h"
#include "processing/sound/sound.h"
#include <algorithm>
#include <cstdio>

namespace deluge::gui::menu_item::osc::source {
class WaveIndex final : public menu_item::source::PatchedParam, public FormattedTitle {
public:
	WaveIndex(l10n::String name, l10n::String title_format_str, int32_t newP, uint8_t source_id)
	    : PatchedParam(name, newP, source_id), FormattedTitle(title_format_str, source_id + 1) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		const auto sound = static_cast<Sound*>(modControllable);
		auto& source = sound->sources[source_id_];
		if (sound->getSynthMode() == SynthMode::FM) {
			return false;
		}
		if (source.oscType == OscType::PHI_MORPH) {
			return true;
		}
		return source.oscType == OscType::WAVETABLE && source.hasAtLeastOneAudioFileLoaded();
	}

	void selectEncoderAction(int32_t offset) override {
		// Push+twist: adjust gamma (shared phase multiplier) for PHI_MORPH
		if (Buttons::isButtonPressed(hid::button::SELECT_ENC)
		    && soundEditor.currentSound->sources[source_id_].oscType == OscType::PHI_MORPH) {
			Buttons::selectButtonPressUsedUp = true;
			float& gamma = soundEditor.currentSound->sources[source_id_].phiMorphGamma;
			gamma = std::max(0.0f, gamma + static_cast<float>(offset));
			char buffer[16];
			snprintf(buffer, sizeof(buffer), "G:%d", static_cast<int32_t>(gamma));
			display->displayPopup(buffer);
			if (display->haveOLED()) {
				renderUIsForOled();
			}
			return;
		}
		PatchedParam::selectEncoderAction(offset);
	}

	[[nodiscard]] RenderingStyle getRenderingStyle() const override { return SLIDER; }
};
} // namespace deluge::gui::menu_item::osc::source
