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
#include "deluge/modulation/params/param.h"
#include "gui/menu_item/unpatched_param.h"
#include "gui/ui/sound_editor.h"

namespace deluge::gui::menu_item::voice {
class Portamento final : public UnpatchedParam {
public:
	using UnpatchedParam::UnpatchedParam;

	Portamento(l10n::String newName) : UnpatchedParam(newName, deluge::modulation::params::UNPATCHED_PORTAMENTO) {}

	void getColumnLabel(StringBuf& label, bool forSmallFont) override {
		label.append(deluge::l10n::get(forSmallFont ? l10n::String::STRING_FOR_PORTAMENTO
		                                            : l10n::String::STRING_FOR_PORTAMENTO_SHORT));
	}
};

} // namespace deluge::gui::menu_item::voice
