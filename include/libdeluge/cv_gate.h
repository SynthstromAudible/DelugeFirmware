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

#include "abi.h"
#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/// Initialise the CV DAC (linearity calibration). `display_shares_spi` is true
/// when the board's display shares the CV DAC's SPI bus. CV and gate output goes
/// through `libdeluge/timed_output.h`. [task]
void deluge_cv_init(bool display_shares_spi) DELUGE_NOEXCEPT;

/// Configure the gate output pins as outputs. [task]
void deluge_gate_init(void) DELUGE_NOEXCEPT;

/// Callback invoked on each rising edge of the external trigger-clock input.
/// `timestamp` is the edge's capture stamp (see `deluge_audio_stamp_to_render_offset`), widened to 64 bits. [isr]
typedef void (*DelugeTriggerClockHandler)(uint64_t timestamp);

/// Register (or clear, with NULL) the trigger-clock-in handler. [task]
void deluge_trigger_clock_set_handler(DelugeTriggerClockHandler handler) DELUGE_NOEXCEPT;

#ifdef DELUGE_HOST
/// Host harness only: deliver one trigger-clock rising edge, stamped @p stamp, to the installed handler.
/// Defined only by the Rust BSP's `host_app` build; never in a device image. [task]
void deluge_trigger_clock_host_edge(uint64_t stamp) DELUGE_NOEXCEPT;
#endif

#ifdef __cplusplus
}
#endif

#endif // LIBDELUGE_CV_GATE_H
