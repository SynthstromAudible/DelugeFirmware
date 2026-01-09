/*
 * Copyright © 2024 Synthstrom Audible Limited
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
#include "util/containers.h"

namespace deluge::gui::menu_item::clip {

class ClipTypeSelection final : public Selection {
public:
	using Selection::Selection;

	void readCurrentValue() override;
	void writeCurrentValue() override;
	deluge::vector<std::string_view> getOptions(OptType optType = OptType::FULL) override;

	// Allow entering the selection menu to see options
	bool shouldEnterSubmenu() override { return true; }

private:
	// Available clip types for Synth/MIDI/CV tracks
	static constexpr l10n::String clipTypeOptions[] = {
	    l10n::String::STRING_FOR_PIANO_ROLL,
	    l10n::String::STRING_FOR_STEP_SEQ,
	    l10n::String::STRING_FOR_PULSE_SEQ,
	};
};

} // namespace deluge::gui::menu_item::clip
