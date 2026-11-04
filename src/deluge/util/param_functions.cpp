/*
 * Copyright © 2014-2026 Synthstrom Audible Limited
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

#include "util/param_functions.h"
#include "dsp-next/core/fixed_point.h"
#include "dsp-next/fixed/curves/param_curve.h"
#include "modulation/params/param.h"
#include <cstdint>

namespace params = deluge::modulation::params;
namespace curves = deluge::dsp::fixed;

int32_t getFinalParameterValueExpWithDumbEnvelopeHack(int32_t paramNeutralValue, int32_t patchedValue, int32_t p) {
	// The neutral read at Q31 (its units are the param's); the patched value is the chain's Q29 combination
	const auto neutral = deluge::dsp::Q<31>::from_raw(paramNeutralValue);
	auto combination = deluge::dsp::Q<29>::from_raw(patchedValue);
	if (params::LOCAL_ENV_0_DECAY <= p && p <= params::LOCAL_ENV_3_RELEASE) {
		// The neutral times the release rate: the high word, in the neutral's units
		return mul(neutral, curves::release_rate(combination)).raw();
	}
	if (params::LOCAL_ENV_0_ATTACK <= p && p <= params::LOCAL_ENV_3_ATTACK) {
		// An attack is the exponential of the negated combination; saturating, so the most negative patching is the
		// fastest attack
		combination = neg_sat(combination);
	}
	return curves::curve_exponential(neutral, combination).raw();
}
