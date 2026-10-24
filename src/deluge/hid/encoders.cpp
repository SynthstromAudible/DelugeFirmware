/*
 * Copyright © 2020-2023 Synthstrom Audible Limited
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

#include "hid/encoders.h"
#include "OSLikeStuff/scheduler_api.h"
#include "libdeluge/encoder_io.h"
#include <algorithm>

namespace deluge::hid::encoders {

TaskID EncoderTaskID = -1;

double ContinuousEncoder::calcNextKnobSpeed(int8_t offset) {
	return acceleration_.advance(offset, getSystemTime(), kGoldEncoderAcceleration);
}

// ── Named encoder globals ──────────────────────────────────────────────────

DetentedEncoder scrollY;
DetentedEncoder scrollX;
DetentedEncoder tempo;
DetentedEncoder select;
ContinuousEncoder mod0;
ContinuousEncoder mod1;

DetentedEncoder& functionEncoderAt(size_t i) {
	static DetentedEncoder* const table[] = {&scrollY, &scrollX, &tempo, &select};
	return *table[i];
}

ContinuousEncoder& modEncoderAt(size_t i) {
	static ContinuousEncoder* const table[] = {&mod0, &mod1};
	return *table[i];
}

void init() {
	// The board owns the encoder GPIO, the per-encoder A-edge interrupts and the quadrature decoding: it accumulates
	// confirmed signed edges and wakes EncoderTaskID on movement (see the BSP's encoder_io). We drain those edge counts
	// in interpretEncoders() (task context) and apply the detent / gold-knob policy there. No encoder pin number or IRQ
	// id crosses the libdeluge boundary.
	deluge_encoder_io_init(EncoderTaskID);
}

void poll_encoder_pins() {
	// Catches slow B-only transitions, which don't raise the A-edge interrupt
	deluge_encoder_poll_pins();
}

} // namespace deluge::hid::encoders
