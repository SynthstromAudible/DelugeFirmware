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

#include "instruments/deluge/sound_state.h"

#include <cstddef>
#include <format>
#include <ostream>

/// @file
/// @brief Prints a source's key-split range and its DX7 patch in a failed CppSpec comparison, alone or as container
///        elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::instrument {

/// @brief Prints a range field by field, e.g.
///        `{top_note 60, file "SAMPLES/A.WAV", start 0, end 9999999, loop_start 0, loop_end 0, transpose 0, cents 0}`.
/// @param os The stream to print to.
/// @param r The range.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const RangeState& r) {
	return os << std::format("{{top_note {}, file \"{}\", start {}, end {}, loop_start {}, loop_end {}, transpose {}, "
	                         "cents {}}}",
	                         r.top_note, r.file_path, r.start_pos, r.end_pos, r.loop_start_pos, r.loop_end_pos,
	                         r.transpose, r.cents);
}

/// @brief Prints a DX7 patch as its 156 voice bytes, its engine mode and its random detune.
/// @param os The stream to print to.
/// @param p The patch.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DxPatchState& p) {
	os << "{params [";
	for (std::size_t i = 0; i < p.params.size(); ++i) {
		os << (i == 0 ? "" : " ") << int{p.params[i]};
	}
	return os << std::format("], engine_mode {}, random_detune {}}}", p.engine_mode, p.random_detune);
}

} // namespace deluge::instrument
