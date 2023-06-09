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

#ifndef CLEARSONGUI_H
#define CLEARSONGUI_H

#include "ContextMenu.h"

class ContextMenuClearSong final : public ContextMenuForLoading {
public:
	ContextMenuClearSong();
	void focusRegained();
	bool canSeeViewUnderneath() { return true; }

	char const** getOptions();
	bool acceptCurrentOption();
};

extern ContextMenuClearSong contextMenuClearSong;

#endif // CLEARSONGUI_H
