/*
 * Copyright © 2026 Synthstrom Audible Limited
 *
 * This file is part of The Synthstrom Audible Deluge Firmware.
 * Licensed under the GNU General Public License v3 or later; see LICENSE.
 */

// The regions the params' enum positions used to decide their chains and curves by, named by each region's first
// param: what the specs characterise the param table against, and how the frozen Patcher walks a scope.

#pragma once

#include "modulation/params/param.h"

#include <cstdint>

namespace deluge::synth::test::regions {

namespace params = deluge::modulation::params;

inline constexpr int32_t kLocalLinear = params::LOCAL_MODULATOR_0_FEEDBACK; ///< after the square-curve params
inline constexpr int32_t kLocalBipolar = params::LOCAL_OSC_A_PHASE_WIDTH;   ///< the first add-chain param
inline constexpr int32_t kLocalExponential = params::LOCAL_LPF_FREQ;
inline constexpr int32_t kGlobalLinear = params::GLOBAL_DELAY_FEEDBACK;
inline constexpr int32_t kGlobalBipolar = params::GLOBAL_DELAY_RATE; ///< the sound scope has no bipolar params
inline constexpr int32_t kGlobalExponential = params::GLOBAL_DELAY_RATE;

} // namespace deluge::synth::test::regions
