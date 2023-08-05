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
#include "fmt/core.h"
#include "gui/l10n/l10n.h"
#include "gui/menu_item/selection/selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::modulator {
class Destination final : public Selection<2> {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->value_ = soundEditor.currentSound->modulator1ToModulator0; }
	void writeCurrentValue() override { soundEditor.currentSound->modulator1ToModulator0 = this->value_; }
	static_vector<std::string, capacity()> getOptions() override {
		using enum l10n::Strings;
		return {
		    l10n::get(STRING_FOR_CARRIERS),
		    fmt::vformat(l10n::get(STRING_FOR_MODULATOR_N), fmt::make_format_args(1)),
		};
	}
	bool isRelevant(Sound* sound, int32_t whichThing) override {
		return (whichThing == 1 && sound->synthMode == SynthMode::FM);
	}
};

} // namespace deluge::gui::menu_item::modulator
