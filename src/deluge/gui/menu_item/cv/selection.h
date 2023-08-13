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
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/selection.h"
#include "gui/menu_item/submenu.h"
#include "gui/ui/sound_editor.h"
#include "transpose.h"
#include "volts.h"
#include <ranges>

extern void setCvNumberForTitle(int32_t m);
extern deluge::gui::menu_item::cv::Submenu cvSubmenu;

namespace deluge::gui::menu_item::cv {
class Selection final : public menu_item::Selection<2> {
public:
	using menu_item::Selection<capacity()>::Selection;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		if (navigatedBackwardFrom == nullptr) {
			this->setValue(0);
		}
		else {
			this->setValue(soundEditor.currentSourceIndex);
		}
		menu_item::Selection<2>::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() override {
		soundEditor.currentSourceIndex = this->getValue();
		setCvNumberForTitle(this->getValue());
		return &cvSubmenu;
	}

	static_vector<std::string_view, capacity()> getOptions() override {
		using enum l10n::String;

		// // Just a test
		// auto strings =                               //<
		//     std::views::iota(1_u32, capacity())      //<
		//     | std::views::transform([](size_t idx) { //<
		// 	      return fmt::vformat(l10n::get(STRING_FOR_CV_OUTPUT_N), fmt::make_format_args(idx));
		//       })
		//     | std::views::common;

		// return {strings.begin(), strings.begin() + capacity()};

		return {
		    fmt::vformat(l10n::getView(STRING_FOR_CV_OUTPUT_N), fmt::make_format_args(1)),
		    fmt::vformat(l10n::getView(STRING_FOR_CV_OUTPUT_N), fmt::make_format_args(2)),
		};
	}
};
} // namespace deluge::gui::menu_item::cv
