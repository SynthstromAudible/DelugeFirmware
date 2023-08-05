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
#include "gui/l10n/strings.h"
#include "gui/menu_item/gate/mode.h"
#include "gui/menu_item/selection/selection.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/display.hpp"
#include "mode.h"
#include "off_time.h"
#include "util/container/static_vector.hpp"

extern deluge::gui::menu_item::gate::OffTime gateOffTimeMenu;
extern deluge::gui::menu_item::gate::Mode gateModeMenu;
namespace deluge::gui::menu_item::gate {

class Selection final : public menu_item::Selection<5> {
public:
	using menu_item::Selection<capacity()>::Selection;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		if (navigatedBackwardFrom == nullptr) {
			this->value_ = 0;
		}
		else {
			this->value_ = soundEditor.currentSourceIndex;
		}
		menu_item::Selection<capacity()>::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() override {
		if (this->value_ == NUM_GATE_CHANNELS) {
			return &gateOffTimeMenu;
		}
		soundEditor.currentSourceIndex = this->value_;

		if (display.type == DisplayType::OLED) {
			gate::mode_title[8] = '1' + this->value_;
		}

		gateModeMenu.updateOptions(this->value_);
		return &gateModeMenu;
	}

	static_vector<std::string, capacity()> getOptions() override {
		using enum l10n::Strings;
		auto gate_output_fmt_string = l10n::get(STRING_FOR_GATE_OUTPUT_N);
		return {
		    fmt::vformat(gate_output_fmt_string, fmt::make_format_args(1)),
		    fmt::vformat(gate_output_fmt_string, fmt::make_format_args(2)),
		    fmt::vformat(gate_output_fmt_string, fmt::make_format_args(3)),
		    fmt::vformat(gate_output_fmt_string, fmt::make_format_args(4)),
		    l10n::get(STRING_FOR_MINIMUM_OFF_TIME),
		};
	}
};
} // namespace deluge::gui::menu_item::gate
