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
#include "gui/l10n/l10n.h"
#include "gui/menu_item/cv/submenu.h"
#include "gui/menu_item/selection.h"
#include "gui/ui/sound_editor.h"

extern void setCvNumberForTitle(int32_t m);
extern deluge::gui::menu_item::cv::Submenu cvSubmenu;

namespace deluge::gui::menu_item::cv {
class Selection final : public menu_item::Selection {
public:
	using menu_item::Selection::Selection;

	void beginSession(MenuItem* navigatedBackwardFrom) override {
		if (navigatedBackwardFrom == nullptr) {
			this->setValue(0);
		}
		else {
			this->setValue(soundEditor.currentSourceIndex);
		}
		menu_item::Selection::beginSession(navigatedBackwardFrom);
	}

	MenuItem* selectButtonPress() override {
		soundEditor.currentSourceIndex = this->getValue();
		setCvNumberForTitle(this->getValue());
		return &cvSubmenu;
	}

	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		static auto cv1 = l10n::getView(STRING_FOR_CV_OUTPUT_1);
		static auto cv2 = l10n::getView(STRING_FOR_CV_OUTPUT_2);

		return {cv1, cv2};
	}
};
} // namespace deluge::gui::menu_item::cv
