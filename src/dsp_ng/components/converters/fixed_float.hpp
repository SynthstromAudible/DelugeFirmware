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
#include "dsp_ng/core/converter.hpp"

namespace deluge::dsp {
template <>
struct SampleConverter<fixed_point::Sample, floating_point::Sample> {
	float render(fixed_point::Sample sample) { return sample.to_float(); }
};

template <>
struct SampleConverter<floating_point::Sample, fixed_point::Sample> {
	fixed_point::Sample render(float sample) { return sample; }
};

template <>
struct SampleConverter<Argon<q31_t>, Argon<float>> {
	Argon<float> render(Argon<q31_t> sample) { return sample.ConvertTo<float, 31>(); }
};

template <>
struct SampleConverter<Argon<float>, Argon<q31_t>> {
	Argon<q31_t> render(Argon<float> sample) { return sample.ConvertTo<q31_t, 31>(); }
};
} // namespace deluge::dsp
