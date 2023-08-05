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
#include "gui/menu_item/selection/selection.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::shortcuts {
class Version final : public Selection<NUM_SHORTCUTS_VERSIONS> {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->value_ = soundEditor.shortcutsVersion; }
	void writeCurrentValue() override { soundEditor.setShortcutsVersion(this->value_); }
	static_vector<std::string, capacity()> getOptions() override {
		using enum l10n::Strings;
		return {
		    l10n::get(STRING_FOR_SHORTCUTS_VERSION_1),
		    l10n::get(STRING_FOR_SHORTCUTS_VERSION_3),
		};
	}
};
} // namespace deluge::gui::menu_item::shortcuts
