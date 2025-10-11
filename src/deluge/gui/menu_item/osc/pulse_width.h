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

	void renderInHorizontalMenu(int32_t startX, int32_t width, int32_t startY, int32_t height) override {
		oled_canvas::Canvas& image = OLED::main;

		const float valueNormalized = getValue() / 50.0f;

		constexpr int32_t xPadding = 4;
		width -= xPadding * 2 + 1;

		int32_t xStart = startX + xPadding;
		int32_t xEnd = xStart + width;
		int32_t yStart = startY + kHorizontalMenuSlotYOffset;
		int32_t yEnd = startY + height - 5;

		int32_t pwMinX = xStart + 2;
		int32_t pwMaxX = xStart + width / 2;
		int32_t pwWidth = pwMaxX - pwMinX;
		int32_t pwX = pwMaxX - pwWidth * valueNormalized;

		image.drawVerticalLine(xStart, yStart, yEnd);
		image.drawHorizontalLine(yStart, xStart, pwX);
		image.drawVerticalLine(pwX, yStart, yEnd);
		image.drawHorizontalLine(yEnd, pwX, xEnd);
	}
};

} // namespace deluge::gui::menu_item::osc
