/*
 * Copyright © 2019-2023 Synthstrom Audible Limited
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

#include <ContextMenuOverwriteFile.h>
#include "SaveUI.h"

ContextMenuOverwriteFile contextMenuOverwriteFile;

ContextMenuOverwriteFile::ContextMenuOverwriteFile() {
#if HAVE_OLED
	title = "Overwrite?";
#endif
}

char const** ContextMenuOverwriteFile::getOptions() {
#if HAVE_OLED
	static char const* options[] = {"Ok"};
#else
	static char const* options[] = {"OVERWRITE"};
#endif
	return options;
}

bool ContextMenuOverwriteFile::acceptCurrentOption() {
	bool dealtWith = currentSaveUI->performSave(true);

	return dealtWith;
}
