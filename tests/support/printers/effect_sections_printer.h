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

#include "effects/state.h"
#include "effects/words.h"

#include <ostream>

/// @file
/// @brief Prints one section of an owner's effect settings (its filter, saturation, mod FX, stutter, compressor or
///        sidechain) in a failed CppSpec comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::effects {

/// @brief Prints a filter section by its words, e.g. `{lpf 24dB, hpf HPLadder, route H2L}`.
/// @param os The stream to print to.
/// @param s The section.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const FilterState& s) {
	return os << "{lpf " << to_string(s.lpf_mode) << ", hpf " << to_string(s.hpf_mode) << ", route "
	          << to_string(s.route) << "}";
}

/// @brief Prints a saturation section, e.g. `{clipping 3}`.
/// @param os The stream to print to.
/// @param s The section.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SaturationState& s) {
	return os << "{clipping " << static_cast<int>(s.clipping_amount) << "}";
}

/// @brief Prints a mod FX section by its type's word, e.g. `{type chorus}`.
/// @param os The stream to print to.
/// @param s The section.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ModFxState& s) {
	return os << "{type " << to_string(s.type) << "}";
}

/// @brief Prints a stutter section's flags, e.g. `{song 1, quantized 1, reversed 0, pingPong 0}`.
/// @param os The stream to print to.
/// @param s The section.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const StutterState& s) {
	return os << "{song " << s.use_song_stutter << ", quantized " << s.quantized << ", reversed " << s.reversed
	          << ", pingPong " << s.ping_pong << "}";
}

/// @brief Prints a compressor section's raw knob values, e.g. `{attack 83886080, release 83886080, …}`.
/// @param os The stream to print to.
/// @param s The section.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const CompressorState& s) {
	return os << "{attack " << s.attack << ", release " << s.release << ", threshold " << s.threshold << ", ratio "
	          << s.ratio << ", hpf " << s.sidechain_hpf << ", blend " << s.blend << "}";
}

/// @brief Prints a sidechain section, its sync type and level as their numbers, e.g.
///        `{attack 0, release 0, syncType 0, syncLevel 0}`.
/// @param os The stream to print to.
/// @param s The section.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SidechainState& s) {
	return os << "{attack " << s.attack << ", release " << s.release << ", syncType " << static_cast<int>(s.sync_type)
	          << ", syncLevel " << static_cast<int>(s.sync_level) << "}";
}

} // namespace deluge::effects
