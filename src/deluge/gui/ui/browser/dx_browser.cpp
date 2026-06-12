
/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "dx_browser.h"
#include "definitions_cxx.hpp"
#include "gui/menu_item/dx/cartridge.h"
#include "gui/ui/sound_editor.h"
#include "hid/display/oled.h"

using namespace deluge::gui;

DxSyxBrowser::DxSyxBrowser() {
	fileIcon = deluge::hid::display::OLED::waveIcon;
	title = "DX7 syx files";
	shouldWrapFolderContents = false;
}

static char const* allowedFileExtensionsSyx[] = {"SYX", NULL};
bool DxSyxBrowser::opened() {

	bool success = Browser::opened();
	if (!success)
		return false;

	allowedFileExtensions = allowedFileExtensionsSyx;

	allowFoldersSharingNameWithFile = true;
	outputTypeToLoad = OutputType::NONE;
	qwertyVisible = false;

	fileIndexSelected = 0;

	Error error = StorageManager::initSD();
	if (error != Error::NONE)
		goto sdError;

	currentDir = "DX7";

	// TODO: fill in last used name!
	error = arrivedInNewFolder(1, "", "DX7");
	if (error != Error::NONE)
		goto sdError;

	return true;
sdError:
	display->displayError(error);
	return false;
}

// TODO: this is identical to SampleBrowser, move to parent class?
Error DxSyxBrowser::getCurrentFilePath(std::string* path) {
	(*path) = currentDir;
	int oldLength = path->size();
	if (oldLength) {
		(*path).resize(oldLength);
		(*path).append("/");
	}

	FileItem* currentFileItem = getCurrentFileItem();

	(*path).append(currentFileItem->filename);

	return Error::NONE;
}

void DxSyxBrowser::enterKeyPress() {
	FileItem* currentFileItem = getCurrentFileItem();
	if (!currentFileItem) {
		return;
	}

	if (currentFileItem->isFolder) {
		// [SIC]
		char const* filenameChars =
		    currentFileItem->filename
		        .c_str(); // Extremely weirdly, if we try to just put this inside the parentheses in the next line,
		                  // it returns an empty string (&nothing). Surely this is a compiler error??

		Error error = goIntoFolder(filenameChars);
		if (error != Error::NONE) {
			display->displayError(error);
			close(); // Don't use goBackToSoundEditor() because that would do a left-scroll
			return;
		}
	}
	else {
		// TODO: c.f. slotbrowser, we might just be able to pass a file pointer to the FAT loader
		std::string path;
		getCurrentFilePath(&path);
		close();

		if (!path.empty()) {
			if (menu_item::dxCartridge.tryLoad(path.c_str())) {
				soundEditor.enterSubmenu(&menu_item::dxCartridge);
			}
		}

		// dx7ui.openFile(path.get());
	}
}

DxSyxBrowser dxBrowser{};
