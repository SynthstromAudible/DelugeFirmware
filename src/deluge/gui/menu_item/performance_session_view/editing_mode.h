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
#include "gui/menu_item/toggle.h"
#include "gui/views/performance_session_view.h"

namespace deluge::gui::menu_item::performance_session_view {
class EditingMode final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override {
		int32_t currentValue;
		if (!performanceSessionView.defaultEditingMode) {
			currentValue = 0;
		}
		else if (!performanceSessionView.editingParam) {
			currentValue = 1;
		}
		else {
			currentValue = 2;
		}
		this->setValue(currentValue);
	}
	void writeCurrentValue() override {
		int32_t currentValue = this->getValue();
		if (currentValue == 0) {
			performanceSessionView.defaultEditingMode = false;
			performanceSessionView.editingParam = false;
		}
		else if (currentValue == 1) {
			performanceSessionView.defaultEditingMode = true;
			performanceSessionView.editingParam = false;
		}
		else {
			performanceSessionView.defaultEditingMode = true;
			performanceSessionView.editingParam = true;
		}
		uiNeedsRendering(&performanceSessionView);
	}
	std::vector<std::string_view> getOptions() override {
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_DISABLED),
		    l10n::getView(STRING_FOR_PERFORM_EDIT_VALUE),
		    l10n::getView(STRING_FOR_PERFORM_EDIT_PARAM),
		};
	}
};

} // namespace deluge::gui::menu_item::performance_session_view
