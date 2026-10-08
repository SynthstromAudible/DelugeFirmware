/*
 * Copyright © 2015-2023 Synthstrom Audible Limited
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

#include "dsp/delay/delay_buffer.h"
#include "memory/memory_allocator_interface.h"
#include <span>

using deluge::dsp::fixed::DelayTape;

Error DelayBuffer::init(uint32_t rate, uint32_t failIfThisSize, bool includeExtraSpace) {
	const auto [size, clamped] = DelayTape::idealSizeForRate(rate);
	if (static_cast<uint32_t>(size) == failIfThisSize) {
		return Error::UNSPECIFIED;
	}

	const size_t samples =
	    static_cast<size_t>(size) + (includeExtraSpace ? static_cast<size_t>(DelayTape::kWriteLag) : 0);
	auto* storage = static_cast<StereoSample*>(allocLowSpeed(samples * sizeof(StereoSample)));
	if (storage == nullptr) {
		tape_ = {};
		return Error::INSUFFICIENT_RAM;
	}

	tape_ = DelayTape{std::span{storage, samples}, static_cast<size_t>(size), rate};
	if (clamped) {
		tape_.makeNativeRatePrecise();
	}
	return Error::NONE;
}

void DelayBuffer::discard() {
	if (tape_.isActive()) {
		delugeDealloc(tape_.begin());
		tape_ = {};
	}
}
