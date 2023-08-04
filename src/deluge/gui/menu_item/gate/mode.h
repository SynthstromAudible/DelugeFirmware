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
#include "gui/menu_item/selection/typed_selection.h"
#include "gui/ui/sound_editor.h"
#include "processing/engines/cv_engine.h"
#include "util/misc.h"

namespace deluge::gui::menu_item::gate {

static std::string mode_title = HAVE_OLED ? "Gate outX mode" : "";

class Mode final : public TypedSelection<GateType, 3> {

	static_vector<std::string, capacity()> options_ = {
	    HAVE_OLED ? "V-trig" : "VTRI",
	    HAVE_OLED ? "S-trig" : "STRI",
	};

public:
	Mode() : TypedSelection(HAVE_OLED ? mode_title : "") {}
	void readCurrentValue() override { this->value_ = cvEngine.gateChannels[soundEditor.currentSourceIndex].mode; }
	void writeCurrentValue() override { cvEngine.setGateType(soundEditor.currentSourceIndex, this->value_); }
	static_vector<std::string, capacity()> getOptions() override { return options_; }

	void updateOptions(int32_t value) {
		switch (value) {
		case WHICH_GATE_OUTPUT_IS_CLOCK:
			options_[2] = "Clock";
			break;

		case WHICH_GATE_OUTPUT_IS_RUN:
			options_[2] = HAVE_OLED ? "\"Run\" signal" : "Run";
			break;

		default:
			// Remove the extra entry if it's present
			if (options_.size() > 2) {
				options_.pop_back();
			}
			break;
		}
	}
};

} // namespace deluge::gui::menu_item::gate
