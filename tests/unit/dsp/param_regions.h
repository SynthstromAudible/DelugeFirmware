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
/// The end of the per-voice params the regions cover, which the frozen Patcher walks: the DX7's come after them.
inline constexpr int32_t kVoiceRegionsEnd = params::LOCAL_DX7_LEVEL;

/// @brief The param whose region and rules @p p is characterised by: the DX7's params, which the regions predate,
/// patch as their oscillator A counterparts (its level as the volume, its amp mod as the pulse width, its pitch adjust
/// as the pitch); every other param as itself.
/// @param p A patched param's runtime index.
/// @return The param to characterise @p p by.
constexpr int32_t region_param(int32_t p) {
	switch (p) {
	case params::LOCAL_DX7_LEVEL:
		return params::LOCAL_OSC_A_VOLUME;
	case params::LOCAL_DX7_AMP_MOD:
		return params::LOCAL_OSC_A_PHASE_WIDTH;
	case params::LOCAL_DX7_PITCH_ADJUST:
		return params::LOCAL_OSC_A_PITCH_ADJUST;
	default:
		return p;
	}
}

} // namespace deluge::synth::test::regions
