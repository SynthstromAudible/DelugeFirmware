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

#include "synth/matrix/matrix_block.h"
#include "synth/matrix/program.h"

#include <cstdint>
#include <span>

namespace deluge::synth::matrix {

/// @brief One scope's pass: what it reads and the param plane it writes.
struct ScopePass {
	uint8_t scope{};                  ///< 0 the voice, 1 the sound
	std::span<const int32_t> sources; ///< the scope's source values, indexed by PatchSource
	std::span<int32_t> final_values;  ///< the scope's params' final values, indexed from its first param
};

/// @brief Runs a scope's pass of a compiled matrix program over the numeric core @p B.
///
/// The depth results are one array for every pass: a pass that skips a depth destination reads the last value any
/// pass wrote there.
template <class B>
class Evaluator {
public:
	/// The block's patching: each destination one of whose sources is in @p sources_changed.
	static void run(const ScopePass& pass, const MatrixBlock& block, uint32_t sources_changed);
	/// A note's first patching: every param, from every source.
	static void run_initial(const ScopePass& pass, const MatrixBlock& block);
	/// Param @p p, which has no cables, from its preset alone.
	static void run_uncabled_param(const ScopePass& pass, const MatrixBlock& block, int32_t p);
};

} // namespace deluge::synth::matrix
