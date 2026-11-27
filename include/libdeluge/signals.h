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

/// libdeluge/signals.h — named board digital I/O lines.
///
/// The Deluge has a handful of board-level GPIO signals (status LEDs, audio-path
/// enables, jack/source detects) that are not part of the PIC control surface or
/// the CV/gate jacks. The application refers to them by logical name; the BSP
/// owns the actual port/pin mapping, so RZ/A1L pin numbers never cross the
/// boundary. Replaces direct `setOutputState`/`readInput(PIN.port, PIN.pin)` use
/// and the `constexpr Pin` table in definitions_cxx.hpp.
#ifndef LIBDELUGE_SIGNALS_H
#define LIBDELUGE_SIGNALS_H

#include "abi.h"
#include "types.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/// @brief A named board digital line. Boards implement the subset they have.
///
/// @note Fixed underlying type (`: uint8_t`): this enum crosses the C-ABI FFI boundary, so its
///       width must be pinned explicitly rather than left to the compiler's default
///       (arm-none-eabi defaults to `-fshort-enums`, laying it out as 1 byte, while a host/bindgen
///       build without that flag would otherwise emit a wider `int`-sized enum) — an explicit
///       underlying type is authoritative in C and C++ regardless of `-fshort-enums`, so every
///       build target agrees.
typedef enum DelugeSignal : uint8_t {
	// Outputs
	DELUGE_SIGNAL_SYNC_LED = 0,    ///< external-clock "synced" indicator LED
	DELUGE_SIGNAL_BATTERY_LED = 1, ///< low-battery indicator LED (open-drain)
	DELUGE_SIGNAL_SPEAKER_ENABLE = 2,
	DELUGE_SIGNAL_CODEC_ENABLE = 3, ///< audio codec power/enable

	// Inputs
	DELUGE_SIGNAL_HEADPHONE_DETECT = 64,
	DELUGE_SIGNAL_LINE_IN_DETECT = 65,
	DELUGE_SIGNAL_MIC_DETECT = 66,
	DELUGE_SIGNAL_LINE_OUT_DETECT_L = 67,
	DELUGE_SIGNAL_LINE_OUT_DETECT_R = 68,
} DelugeSignal;

/// Drive an output `signal` high/low (`on`). The board handles any inversion
/// (e.g. open-drain LEDs) internally; `on` is the logical "asserted" state. [task]
void deluge_signal_write(DelugeSignal signal, bool on) DELUGE_NOEXCEPT;

/// Read the current level of an input `signal`. [task]
bool deluge_signal_read(DelugeSignal signal) DELUGE_NOEXCEPT;

// ---------------------------------------------------------------------------
// Battery voltage sense. The board owns the ADC channel and conversion; the app
// runs its own smoothing/region logic over the raw readings.
// ---------------------------------------------------------------------------

/// If a battery-voltage conversion has completed, store its raw reading in
/// `*out` and return true; otherwise return false. [task]
bool deluge_battery_read_raw(uint16_t* out) DELUGE_NOEXCEPT;

/// Arm the next battery-voltage conversion. [task]
void deluge_battery_start_conversion(void) DELUGE_NOEXCEPT;

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_SIGNALS_H
