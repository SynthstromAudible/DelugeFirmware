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

#include "gui/context_menu/context_menu_sample_browser_kit.h"
#include "gui/ui/browser/sample_browser.h"
#include "util/functions.h"
#include "hid/display/numeric_driver.h"
#include "gui/ui/slicer.h"
#include "storage/file_item.h"

ContextMenuSampleBrowserKit contextMenuFileBrowserKit;

ContextMenuSampleBrowserKit::ContextMenuSampleBrowserKit() {
#if HAVE_OLED
	title = "Sample(s)";
#endif
}

char const** ContextMenuSampleBrowserKit::getOptions() {
#if HAVE_OLED
	static char const* options[] = {"Load all", "Slice"};
#else
	static char const* options[] = {"ALL", "Slice"};
#endif
	return options;
}

int ContextMenuSampleBrowserKit::getNumOptions() {
	return 2;
}

bool ContextMenuSampleBrowserKit::isCurrentOptionAvailable() {
	switch (currentOption) {
	case 0: // "ALL" option - to import whole folder. Works whether they're currently on a file or a folder.
		return true;
	default: // Slicer option - only works if currently on a file, not a folder.
		return (!sampleBrowser.getCurrentFileItem()->isFolder);
	}
}

bool ContextMenuSampleBrowserKit::acceptCurrentOption() {
	switch (currentOption) {
	case 0: // Import whole folder
		return sampleBrowser.importFolderAsKit();
	default: // Slicer
		numericDriver.setNextTransitionDirection(1);
		openUI(&slicer);
		return true;
	}
}

int ContextMenuSampleBrowserKit::padAction(int x, int y, int on) {
	return sampleBrowser.padAction(x, y, on);
}

bool ContextMenuSampleBrowserKit::canSeeViewUnderneath() {
	return sampleBrowser.canSeeViewUnderneath();
}
