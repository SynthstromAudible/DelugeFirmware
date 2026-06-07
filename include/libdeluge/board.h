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

/// libdeluge/board.h — capability descriptor.
///
/// Replaces the compile-time hardware knowledge currently baked into
/// `RZA1/cpu_specific.h` (`DELUGE_MODEL_*`, NUM_* grid sizes, CV/gate counts).
/// The BSP fills this in and hands it to `deluge_app_init`; the application
/// queries capabilities instead of `#if DELUGE_MODEL ...`.
#ifndef LIBDELUGE_BOARD_H
#define LIBDELUGE_BOARD_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Primary display kind present on the board.
typedef enum DelugeDisplayKind {
	DELUGE_DISPLAY_NONE = 0,
	DELUGE_DISPLAY_OLED = 1,         ///< graphical framebuffer display
	DELUGE_DISPLAY_SEVEN_SEGMENT = 2 ///< 4-digit numeric display
} DelugeDisplayKind;

/// Static description of one board's capabilities.
typedef struct DelugeBoard {
	const char* name; ///< e.g. "Synthstrom Deluge (144-pad, OLED)"

	/// Pad / button matrix.
	uint8_t pad_grid_width;  ///< main pad columns (e.g. 16)
	uint8_t pad_grid_height; ///< main pad rows (e.g. 8)
	uint8_t button_rows;
	uint8_t button_cols;
	uint8_t encoder_count;

	/// Outputs.
	uint8_t cv_channels;   ///< physical CV DAC channels (e.g. 2)
	uint8_t gate_channels; ///< gate outputs (e.g. 4)

	/// Display.
	DelugeDisplayKind display_kind;
	uint16_t display_width;  ///< pixels, OLED only
	uint16_t display_height; ///< pixels, OLED only

	/// Audio.
	uint32_t audio_sample_rate_hz; ///< nominal codec rate (e.g. 44100)
	uint8_t audio_in_channels;     ///< usually 2 (stereo)
	uint8_t audio_out_channels;    ///< usually 2 (stereo)
} DelugeBoard;

/// Return the board this BSP targets. Valid after `deluge_platform_init`.
/// [task]
const DelugeBoard* deluge_board(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_BOARD_H
