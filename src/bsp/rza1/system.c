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

/// RZ/A1L implementation of <libdeluge/system.h>.
///
/// Part of bsp-rza1-legacy. Only the timer-quiesce path is implemented so far;
/// deluge_platform_init/reset/log are added as the boot wiring in main.c moves
/// behind the boundary.

#include "libdeluge/system.h"
#include "RZA1/cpu_specific.h"

#include "RZA1/mtu/mtu.h"
#include "definitions.h"

void deluge_system_quiesce(void) {
	// Disable all MTU system timers, as the chainload teardown previously did
	// inline. Behaviour is unchanged; only the layer moved.
	disableTimer(TIMER_MIDI_GATE_OUTPUT);
	disableTimer(TIMER_SYSTEM_SLOW);
	disableTimer(TIMER_SYSTEM_FAST);
	disableTimer(TIMER_SYSTEM_SUPERFAST);
}
