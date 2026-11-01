/*
 * Copyright © 2026 Synthstrom Audible Limited
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

/// @file
/// @brief The mod-matrix workload: one held note's patch, 14 cables (two of them depth cables) over 8 voices and the
/// instrument. The cables are listed in the order PatchCableSet::setupPatching leaves them (depth destinations first,
/// then param destinations by ascending param id, the instrument's last), so the matrix fixture can take them as
/// they are.

#include "definitions_cxx.hpp"
#include "modulation/params/param.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace deluge::bench::mod_matrix {

namespace params = deluge::modulation::params;

/// The voices one block patches, besides the instrument.
inline constexpr std::size_t kVoices = 8;

struct WorkloadCable {
	uint8_t source;  ///< PatchSource, as its underlying value.
	int32_t param;   ///< The destination param; for a depth cable, the param of the cable it modulates.
	int8_t range_of; ///< The workload index of the cable whose depth this cable modulates; -1 for a param cable.
	bool unipolar;   ///< The cable's polarity.
	int32_t amount;  ///< Q1.30, as PatchCable::param holds it.
};

struct WorkloadDestination {
	int32_t param;
	int32_t preset_q31; ///< The patched param's preset value, as PatchedParamSet holds it.
};

[[nodiscard]] constexpr uint8_t src(PatchSource s) {
	return std::to_underlying(s);
}
[[nodiscard]] constexpr uint32_t source_bit(uint8_t s) {
	return uint32_t{1} << s;
}

inline constexpr std::array<WorkloadCable, 14> kCables{{
    {src(PatchSource::AFTERTOUCH), params::LOCAL_PITCH_ADJUST, 9, false, 0x2000'0000}, // depth of cable 9
    {src(PatchSource::Y), params::LOCAL_LPF_FREQ, 7, false, 0x1800'0000},              // depth of cable 7
    {src(PatchSource::AFTERTOUCH), params::LOCAL_OSC_A_VOLUME, -1, false, 0x2000'0000},
    {src(PatchSource::VELOCITY), params::LOCAL_VOLUME, -1, false, 0x3000'0000},
    {src(PatchSource::Y), params::LOCAL_LPF_RESONANCE, -1, false, 0x1000'0000},
    {src(PatchSource::RANDOM), params::LOCAL_OSC_A_PHASE_WIDTH, -1, false, 0x0800'0000},
    {src(PatchSource::LFO_GLOBAL_1), params::LOCAL_PAN, -1, false, 0x2000'0000},
    {src(PatchSource::ENVELOPE_1), params::LOCAL_LPF_FREQ, -1, false, 0x3000'0000},
    {src(PatchSource::NOTE), params::LOCAL_LPF_FREQ, -1, false, 0x1000'0000},
    {src(PatchSource::LFO_LOCAL_1), params::LOCAL_PITCH_ADJUST, -1, false, 0x0400'0000},
    {src(PatchSource::X), params::LOCAL_PITCH_ADJUST, -1, false, 0x2000'0000},
    {src(PatchSource::ENVELOPE_2), params::LOCAL_OSC_B_PITCH_ADJUST, -1, false, -0x1000'0000},
    {src(PatchSource::SIDECHAIN), params::GLOBAL_VOLUME_POST_FX, -1, false, -0x2000'0000},
    {src(PatchSource::LFO_GLOBAL_2), params::GLOBAL_DELAY_RATE, -1, false, 0x1000'0000},
}};

inline constexpr std::array<WorkloadDestination, 10> kDestinations{{
    {params::LOCAL_OSC_A_VOLUME, 0},
    {params::LOCAL_VOLUME, 0x1000'0000},
    {params::LOCAL_LPF_RESONANCE, 0},
    {params::LOCAL_OSC_A_PHASE_WIDTH, 0},
    {params::LOCAL_PAN, 0},
    {params::LOCAL_LPF_FREQ, 0x0800'0000},
    {params::LOCAL_PITCH_ADJUST, 0},
    {params::LOCAL_OSC_B_PITCH_ADJUST, 0},
    {params::GLOBAL_VOLUME_POST_FX, 0},
    {params::GLOBAL_DELAY_RATE, 0},
}};

/// Sources a held note leaves alone, at fixed q31 values (aftertouch is stored unipolar: 0 to INT32_MAX).
inline constexpr std::array<std::pair<uint8_t, int32_t>, 6> kStaticSources{{
    {src(PatchSource::VELOCITY), 0x3000'0000},
    {src(PatchSource::NOTE), 0x0800'0000},
    {src(PatchSource::X), 0},
    {src(PatchSource::Y), 0x1000'0000},
    {src(PatchSource::AFTERTOUCH), 0x2000'0000},
    {src(PatchSource::RANDOM), 0x0ABC'DEF0},
}};

/// Sources that move every block of a held note. The first three are the instrument's (global) sources.
inline constexpr std::array<uint8_t, 6> kMovingSources{
    src(PatchSource::LFO_GLOBAL_1), src(PatchSource::LFO_GLOBAL_2), src(PatchSource::SIDECHAIN),
    src(PatchSource::ENVELOPE_1),   src(PatchSource::ENVELOPE_2),   src(PatchSource::LFO_LOCAL_1),
};
inline constexpr std::size_t kNumGlobalMoving = 3;

[[nodiscard]] constexpr int32_t moving_value_q31(uint32_t block, uint32_t voice, uint32_t source) {
	return static_cast<int32_t>((block * 0x0100'0193U) + (voice * 0x9E37'79B9U) + (source * 0x7F4A'7C15U));
}

} // namespace deluge::bench::mod_matrix
