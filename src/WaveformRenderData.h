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

#ifndef WAVEFORMRENDERDATA_H_
#define WAVEFORMRENDERDATA_H_

#include "definitions.h"
#include "r_typedefs.h"

#define COL_STATUS_INVESTIGATED 1
#define COL_STATUS_INVESTIGATED_BUT_BEYOND_WAVEFORM 2

struct WaveformRenderData {
	int64_t xScroll;
	int64_t xZoom;
	int32_t maxPerCol[displayWidth];
	int32_t minPerCol[displayWidth];
	uint8_t colStatus[displayWidth];
};

#endif // WAVEFORMRENDERDATA_H_
