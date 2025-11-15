/*
 * Copyright (c) 2024 Sean Ditny
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

#include "gui/context_menu/stem_export/done_stem_export.h"
#include "definitions_cxx.hpp"
#include "gui/l10n/l10n.h"
#include "hid/display/display.h"

extern "C" {
#include "fatfs/ff.h"
}

namespace deluge::gui::context_menu {

PLACE_SDRAM_BSS DoneStemExport doneStemExport{};

char const* DoneStemExport::getTitle() {
	using enum l10n::String;
	return l10n::get(STRING_FOR_DONE_EXPORT_STEMS);
}

std::span<char const*> DoneStemExport::getOptions() {
	using enum l10n::String;

	if (display->haveOLED()) {
		static char const* options[] = {l10n::get(STRING_FOR_OK)};
		return {options, 1};
	}
	else {
		static char const* options[] = {l10n::get(STRING_FOR_DONE_EXPORT_STEMS)};
		return {options, 1};
	}
}

bool DoneStemExport::acceptCurrentOption() {
	return false; // return false so you exit out of the context menu
}
} // namespace deluge::gui::context_menu
