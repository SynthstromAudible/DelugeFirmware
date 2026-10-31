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

#include "synth/matrix/descriptors.h"
#include "synth/modulation/cable.h"

#include <array>
#include <cstddef>
#include <cstdint>

/// @file
/// @brief The compiled matrix program: one instrument's cables, compiled for the evaluator into structure-of-arrays
/// tables per scope (voice and sound). Immutable once compiled; it lives in one exact-size block the instrument owns.

namespace deluge::synth::matrix {

/// A cable whose depth no depth cable controls: it scales by "1" (2^29).
inline constexpr uint8_t kNoRange = 0xFF;

/// One destination: a run of cables combined by its chain and shaped by its curve.
struct Destination {
	uint8_t first_cable{}; ///< into the scope's cable tables
	uint8_t num_cables{};
	ChainKind chain{};
	CurveKind curve{};
	uint8_t param{};          ///< the param (param destinations); the depth result's index (depth destinations)
	bool wave_index_double{}; ///< a wavetable index: the cables' sum doubles before the preset
	uint32_t sources{};       ///< the sources its cables read, one bit each
};

/// One scope's tables, pointing into the program's block.
struct ScopeView {
	const uint8_t* slot_source{};        ///< per polarity slot: the source it converts
	const Conversion* slot_conversion{}; ///< per polarity slot: how
	const uint8_t* cable_slot{};         ///< per cable: its polarity slot
	const uint8_t* cable_depth{};        ///< per cable: its index into the per-block depths (routing order)
	const uint8_t* cable_range{};        ///< per cable: the depth result scaling it, or kNoRange
	const DepthMode* cable_mode{};       ///< per cable: its depth's shape
	const Destination* destinations{};   ///< depth destinations first, in routing order; then param destinations
	uint8_t num_slots{};
	uint8_t num_cables{}; ///< the cable tables hold this rounded up to 4; the extra entries are neutral
	uint8_t num_depth_destinations{};
	uint8_t num_destinations{};
	uint32_t sources_patched{}; ///< the sources any of its cables read
	uint64_t cabled{};          ///< bit (p - first_param) for each param with cables
	uint8_t first_param{};
	uint8_t end_param{};
};

/// One instrument's compiled cables.
struct MatrixProgram {
	std::array<ScopeView, 2> scopes{}; ///< [0] the voice scope, [1] the sound scope
	std::size_t bytes{};               ///< the block it lives in, header included
};

} // namespace deluge::synth::matrix
