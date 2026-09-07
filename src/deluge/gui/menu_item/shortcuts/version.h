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
#include "gui/menu_item/selection.h"
#include "gui/ui/shell_guest_services.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::shortcuts {
class Version final : public Selection {
public:
	using Selection::Selection;
	void readCurrentValue() override { this->setValue(soundEditor.shortcutsVersion); }
	void writeCurrentValue() override {
		soundEditor.setShortcutsVersion(this->getValue());
		// The ported menu keeps its own copy of this setting beside the two `constexpr` layers it
		// selects between, and nothing else would tell it the user just changed layouts -- so it
		// would keep resolving the old one until the next boot. See
		// `deluge::gui::ui::applyShellGuestShortcutsVersion`.
		deluge::gui::ui::applyShellGuestShortcutsVersion();
	}
	deluge::vector<std::string_view> getOptions(OptType optType) override {
		(void)optType;
		using enum l10n::String;
		return {
		    l10n::getView(STRING_FOR_SHORTCUTS_VERSION_1),
		    l10n::getView(STRING_FOR_SHORTCUTS_VERSION_3),
		};
	}
};
} // namespace deluge::gui::menu_item::shortcuts
