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

/// libdeluge/cv_gate.h — analog CV/gate outputs and trigger-clock input.
///
/// CV is written as a raw DAC code in the board's native resolution; the
/// application's note->voltage mapping (volts/octave, calibration) stays in the
/// app. Backed by the RSPI DAC + GPIO today; `deluge-bsp::cv_gate` and
/// `::trigger_clock` on the Rust side. The trigger-clock callback fires on each
/// rising edge of the external clock input.
#ifndef LIBDELUGE_CV_GATE_H
#define LIBDELUGE_CV_GATE_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Set the DAC output for CV `channel` (0..cv_channels-1) to a raw 16-bit code.
/// [task] [audio]
void deluge_cv_set(uint8_t channel, uint16_t dac_code);

/// Set gate output `channel` (0..gate_channels-1) high or low. [task] [audio]
void deluge_gate_set(uint8_t channel, bool high);

/// Callback invoked on each rising edge of the external trigger-clock input.
/// `timestamp` is in the units of clock.h's sample/tick counter. [isr]
typedef void (*DelugeTriggerClockHandler)(uint64_t timestamp);

/// Register (or clear, with NULL) the trigger-clock-in handler. [task]
void deluge_trigger_clock_set_handler(DelugeTriggerClockHandler handler);

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_CV_GATE_H
