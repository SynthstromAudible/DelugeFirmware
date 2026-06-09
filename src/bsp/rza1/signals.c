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

/// RZ/A1L implementation of <libdeluge/signals.h>.
///
/// Part of bsp-rza1-legacy and, unlike control_surface.cpp, fully decoupled from
/// application headers — it needs only the GPIO HAL and owns the board pin map.
/// The {port, pin} numbers below mirror the `constexpr Pin` table still in
/// definitions_cxx.hpp; this becomes the single source of truth once the
/// application's remaining direct GPIO use (chiefly deluge.cpp) is migrated.
///
///   SYNC_LED {6,7}  BATTERY_LED {1,1}  SPEAKER_ENABLE {4,1}  CODEC {6,12}
///   HEADPHONE_DETECT {6,5}  LINE_IN_DETECT {6,6}  MIC_DETECT {7,9}
///   LINE_OUT_DETECT_L {6,3}  LINE_OUT_DETECT_R {6,4}

#include "libdeluge/signals.h"

#include "RZA1/cpu_specific.h"     // TIMER_MIDI_GATE_OUTPUT
#include "RZA1/gpio/gpio.h"        // setOutputState, readInput
#include "RZA1/intc/devdrv_intc.h" // R_INTC_Enable, INTC_ID_TGIA
#include "RZA1/mtu/mtu.h"          // isTimerEnabled, enableTimer, TGRA

void deluge_signal_write(DelugeSignal signal, bool on) {
	switch (signal) {
	case DELUGE_SIGNAL_SYNC_LED:
		setOutputState(6, 7, on);
		break;
	default:
		// Remaining output signals are wired up as their callers migrate off
		// direct GPIO. Note BATTERY_LED is open-drain (logical-on drives 0), so
		// its polarity must be inverted here when it is added.
		break;
	}
}

bool deluge_signal_read(DelugeSignal signal) {
	switch (signal) {
	case DELUGE_SIGNAL_HEADPHONE_DETECT:
		return readInput(6, 5) != 0;
	case DELUGE_SIGNAL_LINE_IN_DETECT:
		return readInput(6, 6) != 0;
	case DELUGE_SIGNAL_MIC_DETECT:
		return readInput(7, 9) != 0;
	case DELUGE_SIGNAL_LINE_OUT_DETECT_L:
		return readInput(6, 3) != 0;
	case DELUGE_SIGNAL_LINE_OUT_DETECT_R:
		return readInput(6, 4) != 0;
	default:
		return false;
	}
}

bool deluge_midi_gate_timer_pending(void) {
	return isTimerEnabled(TIMER_MIDI_GATE_OUTPUT);
}

void deluge_midi_gate_timer_arm(uint32_t samples_from_now) {
	R_INTC_Enable(INTC_ID_TGIA[TIMER_MIDI_GATE_OUTPUT]);
	// samples → timer ticks: *766245 >> 16 (i.e. samples_from_now * 515616 / kSampleRate),
	// the P0/prescaler tick rate of MTU2 channel TIMER_MIDI_GATE_OUTPUT on this board.
	*TGRA[TIMER_MIDI_GATE_OUTPUT] = (uint16_t)((samples_from_now * 766245) >> 16);
	enableTimer(TIMER_MIDI_GATE_OUTPUT);
}
