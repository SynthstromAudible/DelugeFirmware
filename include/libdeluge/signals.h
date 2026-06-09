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

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// A named board digital line. Boards implement the subset they have.
typedef enum DelugeSignal {
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
void deluge_signal_write(DelugeSignal signal, bool on);

/// Read the current level of an input `signal`. [task]
bool deluge_signal_read(DelugeSignal signal);

// ---------------------------------------------------------------------------
// MIDI/gate output timer. A board one-shot that fires midiAndGateTimerGoneOff()
// a precise number of audio samples in the future, so MIDI clock and gate edges
// land sample-accurately rather than at the audio-block boundary. The app owns
// *when* (it computes the sample delay); the board owns the timer hardware and
// the samples→ticks conversion, so neither the timer number nor its clock rate
// crosses the boundary.
// ---------------------------------------------------------------------------

/// True if the output timer is already armed (an output ISR is pending). The app
/// checks this before arming, or before emitting output inline, so it never
/// races the scheduled ISR. [task] [isr]
bool deluge_midi_gate_timer_pending(void);

/// Arm the output timer to fire midiAndGateTimerGoneOff() `samples_from_now`
/// audio samples in the future. Call only when !deluge_midi_gate_timer_pending().
/// [task]
void deluge_midi_gate_timer_arm(uint32_t samples_from_now);

// ---------------------------------------------------------------------------
// Runtime-provided callbacks (inverse direction: the app implements, the
// BSP/HAL call up). Declared here so the timer HAL depends only on
// <libdeluge/...>, never on the app's deluge.h. See system.h for the rationale.
// ---------------------------------------------------------------------------

/// Fired from the MIDI/gate output timer ISR: the scheduled gate/clock output
/// time has arrived and the app should emit pending events. [isr]
void midiAndGateTimerGoneOff(void);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_SIGNALS_H
