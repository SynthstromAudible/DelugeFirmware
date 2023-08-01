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
class PulseWidth final : public menu_item::source::PatchedParam, public FormattedTitle {
public:
	using menu_item::source::PatchedParam::PatchedParam;
	PulseWidth(const string& name, const string& title_format_str, int32_t newP)
	    : source::PatchedParam(name, newP), FormattedTitle(title_format_str) {}

	[[nodiscard]] const string& getTitle() const override { return FormattedTitle::title(); }

	int32_t getFinalValue() override { return (uint32_t)this->value_ * (85899345 >> 1); }

	void readCurrentValue() override {
		this->value_ =
		    ((int64_t)soundEditor.currentParamManager->getPatchedParamSet()->getValue(getP()) * 100 + 2147483648) >> 32;
	}

	bool isRelevant(Sound* sound, int32_t whichThing) override {
		if (sound->getSynthMode() == SynthMode::FM) {
			return false;
		}
		OscType oscType = sound->sources[whichThing].oscType;
		return (oscType != OscType::SAMPLE && oscType != OscType::INPUT_L && oscType != OscType::INPUT_R
		        && oscType != OscType::INPUT_STEREO);
	}
};

} // namespace deluge::gui::menu_item::osc
