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

// ── DetentedEncoder ────────────────────────────────────────────────────────

void DetentedEncoder::applyEdges(int8_t edges) {
	// Two A-pin edges per quadrature cycle, one quadrature cycle per detent click.
	edgeAccumulator += edges;
	int8_t ticks = edgeAccumulator / 2;
	edgeAccumulator -= ticks * 2;
	pos.fetch_add(ticks, std::memory_order_relaxed);
}

int32_t DetentedEncoder::take() {
	return pos.exchange(0, std::memory_order_relaxed);
}

// -- ContinuousEncoder ------------------------------------------------------

int8_t ContinuousEncoder::take() {
	return pos.exchange(0, std::memory_order_relaxed);
}

double ContinuousEncoder::calcNextKnobSpeed(int8_t offset) {
	// - inertia and acceleration control how fast the knob speeds up in horizontal menus
	// - speedScale controls how we go from "raw speed" to speed used as offset multiplier
	// - min and max speed clamp the max effective speed
	//
	// current values have been tuned to be slow enough to feel easy to control, but fast
	// enough to go from 0 to 50 with one fast turn of the encoder. speedScale and min/max
	// could potentially be user-configurable in a small range.
	constexpr double acceleration = 0.1;
	constexpr double inertia = 1.0 - acceleration;
	constexpr double speed_scale = 0.15;
	constexpr double min_speed = 1.0;
	constexpr double max_speed = 3.0;
	constexpr double reset_speed_time_threshold = 0.3;

	// lastOffset and lastEncoderTime keep track of our direction and time
	static int8_t last_offset = 0;
	static double last_encoder_time = 0.0;
	const double time = getSystemTime();

	if (time - last_encoder_time >= reset_speed_time_threshold || offset != last_offset) {
		// too much time passed, or the knob direction changed, reset the speed
		currentKnobSpeed = 0.0;
	}
	else {
		// moving in the same direction, update speed
		currentKnobSpeed = currentKnobSpeed * inertia + 1.0 / (time - last_encoder_time) * acceleration;
	}
	last_encoder_time = time;
	last_offset = offset;
	return std::clamp((currentKnobSpeed * speed_scale), min_speed, max_speed);
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
	// The board owns the encoder GPIO and the per-encoder quadrature-edge interrupts: its ISR
	// accumulates signed A-pin edges and wakes EncoderTaskID on movement (see the BSP's encoder_io).
	// We drain those edge counts in interpretEncoders() (task context) and apply the detent /
	// gold-knob policy there. No encoder pin number or IRQ id crosses the libdeluge boundary.
	deluge_encoder_io_init(EncoderTaskID);
}

} // namespace deluge::hid::encoders
