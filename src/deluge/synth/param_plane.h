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

#include "definitions_cxx.hpp"       // kNumPatchSources, kFirstLocalSource
#include "modulation/params/param.h" // params::LOCAL_LAST, kNumParams, FIRST_GLOBAL
#include "util/misc.h"               // util::to_underlying
#include <array>
#include <cstdint>

/// The per-scope modulation data plane: the patch source values fed into the Patcher, and the param final
/// values it writes out. Grouped into one POD so the synth core can own a single unit (the Phase-4 Voice
/// pool) rather than loose Voice/Sound fields. The Patcher reads/writes these through `std::span`s, so it
/// is agnostic to the sizes; only the construction site points its spans at a plane.
template <std::size_t NumSources, std::size_t NumParams>
struct ParamPlane {
	std::array<int32_t, NumSources> sourceValues{};    ///< current patch-source values (LFO / env / velocity / …)
	std::array<int32_t, NumParams> paramFinalValues{}; ///< Patcher output, indexed by (param - config.firstParam)
};

/// The per-voice plane: all the patch sources, the local param range.
using VoiceParams = ParamPlane<kNumPatchSources, deluge::modulation::params::LOCAL_LAST>;

/// The per-sound (global) plane: the global patch sources, the global param range.
using GlobalParams = ParamPlane<util::to_underlying(kFirstLocalSource),
                                deluge::modulation::params::kNumParams - deluge::modulation::params::FIRST_GLOBAL>;
