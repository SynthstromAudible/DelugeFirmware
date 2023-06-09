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

#ifndef MULTIWAVETABLERANGE_H_
#define MULTIWAVETABLERANGE_H_

#include "MultiRange.h"
#include "WaveTableHolder.h"

class MultiWaveTableRange final : public MultiRange {
public:
	MultiWaveTableRange();

	WaveTableHolder
	    waveTableHolder; // Has to be first variable, cos I do a sneaky optimization between both of MultiRange's children.
};

#endif /* MULTIWAVETABLERANGE_H_ */
