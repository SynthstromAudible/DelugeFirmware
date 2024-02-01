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
// todo - make interpolation standalone functions which receive buffers so they can be compiled with arm and
// deduplicated
int16_t strength2 = oscPos >> 9;
int16_t strength1 = 32767 - strength2;

sampleRead[0] = (interpolationBuffer[0][0][1] * strength1) + (interpolationBuffer[0][0][0] * strength2);
if (numChannelsNow == 2) {
	sampleRead[1] = (interpolationBuffer[1][0][1] * strength1) + (interpolationBuffer[1][0][0] * strength2);
}
