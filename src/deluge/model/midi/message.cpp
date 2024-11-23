/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "message.h"

size_t bytesPerStatusMessage(uint8_t statusByte) {
	if (statusByte == 0xF0    // System exclusive - dynamic length
	    || statusByte == 0xF4 // Undefined
	    || statusByte == 0xF5 // Undefined
	    || statusByte == 0xF6 // Tune request
	    || statusByte == 0xF7 // End of exclusive
	    || statusByte == 0xF8 // Timing clock
	    || statusByte == 0xF9 // Undefined
	    || statusByte == 0xFA // Start
	    || statusByte == 0xFB // Continue
	    || statusByte == 0xFC // Stop
	    || statusByte == 0xFD // Undefined
	    || statusByte == 0xFE // Active sensing
	    || statusByte == 0xFF // Reset
	) {
		return 1;
	}
	else if (statusByte == 0xF1         // Timecode
	         || statusByte == 0xF3      // Song select
	         || statusByte >> 4 == 0x0C // Program change
	         || statusByte >> 4 == 0x0D // Channel aftertouch
	) {
		return 2;
	}
	else {
		return 3;
	}
}
