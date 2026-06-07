/*
 * Copyright © 2014-2025 Synthstrom Audible Limited
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

/// libdeluge/types.h — shared POD types for the hardware boundary.
///
/// Everything here is C-ABI: fixed-width integers, bool, and plain structs only.
#ifndef LIBDELUGE_TYPES_H
#define LIBDELUGE_TYPES_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// Result/status code returned by fallible boundary calls.
typedef enum DelugeStatus {
	DELUGE_OK = 0,
	DELUGE_ERR = -1,         ///< unspecified failure
	DELUGE_ERR_PARAM = -2,   ///< invalid argument
	DELUGE_ERR_BUSY = -3,    ///< resource temporarily unavailable
	DELUGE_ERR_TIMEOUT = -4, ///< operation timed out
	DELUGE_ERR_IO = -5,      ///< hardware / transport I/O error
	DELUGE_ERR_NODEV = -6,   ///< no such device / not present
	DELUGE_ERR_UNSUPPORTED = -7,
} DelugeStatus;

/// One stereo frame of audio. Layout-compatible with the application's
/// `StereoSample` (`{ q31_t l; q31_t r; }`). Samples are signed Q31 fixed point.
typedef struct DelugeStereoSample {
	int32_t l;
	int32_t r;
} DelugeStereoSample;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_TYPES_H
