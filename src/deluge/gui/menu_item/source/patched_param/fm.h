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
#include "gui/menu_item/formatted_title.h"
#include "gui/menu_item/source/patched_param.h"
#include "processing/sound/sound.h"

namespace deluge::gui::menu_item::source::patched_param {
class FM final : public source::PatchedParam, public FormattedTitle {
public:
	FM(const string& name, const string& title_format_str, int newP)
	    : source::PatchedParam(name, newP), FormattedTitle(title_format_str) {}

	[[nodiscard]] const string& getTitle() const override { return FormattedTitle::title(); }

	bool isRelevant(Sound* sound, int whichThing) override { return (sound->getSynthMode() == SynthMode::FM); }
};
} // namespace deluge::gui::menu_item::source::patched_param
