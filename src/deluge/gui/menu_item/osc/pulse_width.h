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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/source/patched_param.h"
#include "modulation/params/param_set.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::osc {
class PulseWidth final : public source::PatchedParam, public FormattedTitle {
public:
	PulseWidth(l10n::String name, l10n::String title_format_str, int32_t newP, uint8_t source_id)
	    : PatchedParam(name, newP, source_id), FormattedTitle(title_format_str, source_id + 1) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	int32_t getFinalValue() override { return computeFinalValueForHalfPrecisionMenuItem(this->getValue()); }

	void readCurrentValue() override {
		this->setValue(computeCurrentValueForHalfPrecisionMenuItem(
		    soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP())));
	}

	bool isRelevant(ModControllableAudio* modControllable, int32_t whichThing) override {
		const auto sound = static_cast<Sound*>(modControllable);
		if (sound->getSynthMode() == SynthMode::FM) {
			return false;
		}

		const OscType oscType = sound->sources[source_id_].oscType;
		if (oscType == OscType::WAVETABLE) {
			auto& source = sound->sources[source_id_];
			return source.hasAtLeastOneAudioFileLoaded();
		}

		return oscType != OscType::SAMPLE && oscType != OscType::INPUT_L && oscType != OscType::INPUT_R
		       && oscType != OscType::INPUT_STEREO;
	}

	void renderInHorizontalMenu(const SlotPosition& slot) override {
		oled_canvas::Canvas& image = OLED::main;

		const float norm = getValue() / 50.0f;

		constexpr int32_t x_padding = 4;
		const uint8_t width = slot.width - x_padding * 2;

		int32_t start_x = slot.start_x + x_padding;
		int32_t end_x = start_x + width - 1;
		int32_t start_y = slot.start_y + kHorizontalMenuSlotYOffset;
		int32_t end_y = slot.start_y + slot.height - 6;

		int32_t pw_min_x = start_x + 2;
		int32_t pw_max_x = start_x + width / 2;
		int32_t pw_width = pw_max_x - pw_min_x;
		int32_t pw_x = pw_max_x - pw_width * norm;

		image.drawVerticalLine(start_x, start_y, end_y);
		image.drawHorizontalLine(start_y, start_x, pw_x);
		image.drawVerticalLine(pw_x, start_y, end_y);
		image.drawHorizontalLine(end_y, pw_x, end_x);
	}
};

} // namespace deluge::gui::menu_item::osc
