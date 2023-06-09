/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#ifndef SaveSongUI_h
#define SaveSongUI_h

#include "SaveUI.h"

class ContextMenu;

class SaveSongUI final : public SaveUI {
public:
	SaveSongUI();
	bool opened();
	void focusRegained();
	//void selectEncoderAction(int8_t offset);
	bool performSave(bool mayOverwrite = false);

#if DELUGE_MODEL == DELUGE_MODEL_40_PAD
	int padAction(int x, int y, int velocity);
#endif

	bool collectingSamples;

protected:
	//int arrivedInNewFolder(int direction);
};

extern SaveSongUI saveSongUI;

#endif
