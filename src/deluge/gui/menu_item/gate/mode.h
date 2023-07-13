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
#include "processing/engines/cv_engine.h"

namespace deluge::gui::menu_item::gate {

#if HAVE_OLED
static char mode_title[] = "Gate outX mode";
#else
static char* mode_title = nullptr;
#endif

class Mode final : public Selection<3> {
#if HAVE_OLED
	static_vector<char const*, 3> options_ = {"V-trig", "S-trig"};
#else
	static_vector<char const*, 3> options_ = {"VTRI", "STRI"};
#endif

public:
	Mode() : Selection(mode_title) {
	}
	void readCurrentValue() override {
		this->value_ = cvEngine.gateChannels[soundEditor.currentSourceIndex].mode;
	}
	void writeCurrentValue() override {
		cvEngine.setGateType(soundEditor.currentSourceIndex, this->value_);
	}
	static_vector<char const*, capacity()> getOptions() override {
		return options_;
	}

	void updateOptions(int value) {
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
