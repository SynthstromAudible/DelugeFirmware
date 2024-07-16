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
#include "gui/menu_item/decimal.h"
#include "gui/menu_item/formatted_title.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/cv_engine.h"

namespace deluge::gui::menu_item::cv {
class Transpose final : public Decimal, public FormattedTitle {
public:
	Transpose(l10n::String name, l10n::String title_format_str) : Decimal(name), FormattedTitle(title_format_str) {}

	[[nodiscard]] std::string_view getTitle() const override { return FormattedTitle::title(); }

	[[nodiscard]] int32_t getMinValue() const override { return -9600; }
	[[nodiscard]] int32_t getMaxValue() const override { return 9600; }
	[[nodiscard]] int32_t getNumDecimalPlaces() const override { return 2; }

	void readCurrentValue() override {
		this->setValue(computeCurrentValueForTranspose(cvEngine.cvChannels[soundEditor.currentSourceIndex].transpose,
		                                               cvEngine.cvChannels[soundEditor.currentSourceIndex].cents));
	}

	void writeCurrentValue() override {
		int32_t transpose, cents;
		computeFinalValuesForTranspose(this->getValue(), &transpose, &cents);
		cvEngine.setCVTranspose(soundEditor.currentSourceIndex, transpose, cents);
	}
};
} // namespace deluge::gui::menu_item::cv
