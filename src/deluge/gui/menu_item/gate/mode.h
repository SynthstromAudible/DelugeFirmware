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

class Mode final : public Selection {
#if HAVE_OLED
	char const* options_[3] = {"V-trig", "S-trig", nullptr};
#else
	char const* options_[3] = {"VTRI", "STRI", nullptr};
#endif
	size_t options_size_ = 2;

public:
	Mode() : Selection(mode_title) {}
	void readCurrentValue() override {
		soundEditor.currentValue = cvEngine.gateChannels[soundEditor.currentSourceIndex].mode;
	}
	void writeCurrentValue() override {
		cvEngine.setGateType(soundEditor.currentSourceIndex, soundEditor.currentValue);
	}
	Sized<char const**> getOptions() override { return {options_, options_size_}; }

	void updateOptions(int value) {
		switch (value) {
		case WHICH_GATE_OUTPUT_IS_CLOCK:
			options_[2] = "Clock";
			options_size_ = 3;
			break;

		case WHICH_GATE_OUTPUT_IS_RUN:
			options_[2] = HAVE_OLED ? "\"Run\" signal" : "Run";
			options_size_ = 3;
			break;

		default:
			options_[2] = nullptr;
			options_size_ = 2;
			break;
		}
	}
};

} // namespace deluge::gui::menu_item::gate
