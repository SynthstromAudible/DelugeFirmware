/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "gui/context_menu/sample_browser/kit.h"
#include "gui/l10n.h"
#include "definitions_cxx.hpp"
#include "gui/ui/browser/sample_browser.h"
#include "util/functions.h"
#include "hid/display.h"
#include "gui/ui/slicer.h"
#include "storage/file_item.h"

namespace deluge::gui::context_menu::sample_browser {
Kit kit{};

char const* Kit::getTitle() {
	using enum l10n::Strings;
	return l10n::get(STRING_FOR_SAMPLES);
}

Sized<char const**> Kit::getOptions() {
	using enum l10n::Strings;
	static char const* options[] = {
	    l10n::get(STRING_FOR_LOAD_ALL), //<
	    l10n::get(STRING_FOR_SLICE)     //<
	};
	return {options, 2};
}

bool Kit::isCurrentOptionAvailable() {
	switch (currentOption) {
	case 0: // "ALL" option - to import whole folder. Works whether they're currently on a file or a folder.
		return true;
	default: // Slicer option - only works if currently on a file, not a folder.
		return (!sampleBrowser.getCurrentFileItem()->isFolder);
	}
}

bool Kit::acceptCurrentOption() {
	switch (currentOption) {
	case 0: // Import whole folder
		return sampleBrowser.importFolderAsKit();
	default: // Slicer
		display.setNextTransitionDirection(1);
		openUI(&slicer);
		return true;
	}
}

ActionResult Kit::padAction(int x, int y, int on) {
	return sampleBrowser.padAction(x, y, on);
}

bool Kit::canSeeViewUnderneath() {
	return sampleBrowser.canSeeViewUnderneath();
}
} // namespace deluge::gui::context_menu::sample_browser
