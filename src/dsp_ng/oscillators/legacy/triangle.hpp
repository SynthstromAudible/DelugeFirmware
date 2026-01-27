/**
 * Copyright (c) 2025 Katherine Whitlock
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

#pragma once
#include "deluge/util/fixedpoint.h"
#include "dsp_ng/core/generator.hpp"
#include "dsp_ng/core/units.hpp"
#include "table_oscillator.hpp"
#include <cstdint>

namespace deluge::dsp::oscillator {

class Triangle : public TableOscillator {
	static std::span<int16_t> getTriangleTable(uint32_t phase_increment);

public:
	Triangle() = default;
	void setFrequency(Frequency frequency) {
		TableOscillator::setFrequency(frequency);
		setTable(getTriangleTable(getPhaseIncrement()));
	}

	void setPhaseIncrement(uint32_t phase_increment) {
		TableOscillator::setPhaseIncrement(phase_increment);
		setTable(getTriangleTable(phase_increment));
	}
};
} // namespace deluge::dsp::oscillator
