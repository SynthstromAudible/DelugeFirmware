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
#include "libdeluge/encoder_io.h"
#include <algorithm>
#include <libdeluge/scheduler.h>

namespace deluge::hid::encoders {

// ── DetentedEncoder ────────────────────────────────────────────────────────

void DetentedEncoder::applyEdges(int8_t edges) {
	// Four edges per click, reported at the nearest click: see the class comment.
	edgeAccumulator += edges;
	int32_t ticks = 0;
	while (edgeAccumulator > 2) {
		edgeAccumulator -= 4;
		++ticks;
	}
	while (edgeAccumulator < -2) {
		edgeAccumulator += 4;
		--ticks;
	}
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

} // namespace deluge::hid::encoders
