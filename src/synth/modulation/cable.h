/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "synth/modulation/patch_source.h"

#include <cstddef>
#include <cstdint>

namespace deluge::synth {

/// @brief The most cables one clip's patching holds.
inline constexpr std::size_t kMaxCables = 32;

/// @brief Whether a cable's modulation swings both ways around zero or only up from it; numbered as the runtime's
/// Polarity.
enum class CablePolarity : uint8_t { UNIPOLAR = 0, BIPOLAR = 1 };

/// @brief Where a cable goes, as the runtime's descriptor word: bits 0-7 the param, then one byte per source from the
/// one nearest the param, 0xFF for none. A cable to a param is `p | 0xFFFFFF00`; a cable to the depth of the cable
/// from `s` to `p` is `p | s << 8 | 0xFFFF0000`; 0xFFFFFFFF is no target. Destinations sort by this word.
struct CableTarget {
	uint32_t word{0xFFFF'FFFFu};

	[[nodiscard]] static constexpr CableTarget param(uint8_t p) { return {p | 0xFFFF'FF00u}; }
	[[nodiscard]] static constexpr CableTarget depth_of(uint8_t p, PatchSource s) {
		return {p | (static_cast<uint32_t>(s) << 8) | 0xFFFF'0000u};
	}
	[[nodiscard]] constexpr bool is_null() const { return word == 0xFFFF'FFFFu; }
	/// True for a cable to a param (and for no target).
	[[nodiscard]] constexpr bool is_param() const { return (word & 0xFF00u) == 0xFF00u; }
	[[nodiscard]] constexpr uint8_t param_index() const { return word & 0xFFu; }
	/// The source nearest the param; NOT_AVAILABLE for a cable to a param.
	[[nodiscard]] constexpr PatchSource top_source() const { return static_cast<PatchSource>((word >> 8) & 0xFFu); }
	/// The source furthest from the param: the source of the cable whose depth this one modulates.
	[[nodiscard]] constexpr PatchSource bottom_source() const {
		return static_cast<PatchSource>((word & 0x00FF'0000u) == 0x00FF'0000u ? (word >> 8) & 0xFFu
		                                                                      : (word >> 16) & 0xFFu);
	}
	/// The target of the cable whose depth this one modulates.
	[[nodiscard]] constexpr CableTarget modulated() const {
		return {(word & 0x00FF'0000u) == 0x00FF'0000u ? word | 0x0000'FF00u : word | 0x00FF'0000u};
	}
	[[nodiscard]] constexpr bool operator==(const CableTarget&) const = default;
};

/// @brief One cable of a clip's patching: its source, where it goes and its polarity. Its depth is a host param keyed
/// by the cable's depth id, not part of the cable.
struct CableRecord {
	PatchSource source{PatchSource::NONE};
	CableTarget target{};
	CablePolarity polarity{CablePolarity::BIPOLAR};
	[[nodiscard]] constexpr bool operator==(const CableRecord&) const = default;
};

} // namespace deluge::synth
