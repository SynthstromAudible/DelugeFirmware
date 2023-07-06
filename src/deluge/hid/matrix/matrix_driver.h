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

#ifndef DISPLAYMANAGER_H
#define DISPLAYMANAGER_H

#include "definitions.h"
#include "RZA1/system/r_typedefs.h"
#include "gui/waveform/waveform_render_data.h"
#include "pad.h"

class AudioClip;

class MatrixDriver {
public:
	MatrixDriver();

	bool isPadPressed(int x, int y);

	int padAction(int x, int y, int velocity);
	void noPressesHappening(bool inCardRoutine);
	bool isUserDoingBootloaderOverwriteAction();

	bool padStates[displayWidth + sideBarWidth][displayHeight];
};

extern char* matrixDriverDisplayWritePos;

extern MatrixDriver matrixDriver;
#endif // DISPLAYMANAGER_H
