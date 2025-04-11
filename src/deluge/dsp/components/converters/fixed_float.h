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
#include "dsp/core/converter.h"

namespace deluge::dsp::converters {
struct FixedFloatConverter : Converter<fixed_point::Sample, floating_point::Sample>,
                             Converter<Argon<q31_t>, Argon<float>> {
	floating_point::Sample render(fixed_point::Sample sample) final { return sample.to_float(); }
	fixed_point::Sample render(floating_point::Sample sample) final { return sample; }
	Argon<float> render(Argon<q31_t> sample) final { return sample.ConvertTo<floating_point::Sample, 31>(); }
	Argon<q31_t> render(Argon<float> sample) final { return sample.ConvertTo<q31_t, 31>(); }
};
} // namespace deluge::dsp::converters
