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

#include "engine-api/types/param.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::instrument {

/// @brief What a patch cable carries: the modulation sources of the Deluge instrument, numbered as the runtime numbers
/// them. NONE also stands for a source a file names that the instrument does not know.
enum class PatchSource : uint8_t {
	LFO_GLOBAL_1,
	LFO_GLOBAL_2,
	SIDECHAIN,
	ENVELOPE_0,
	ENVELOPE_1,
	ENVELOPE_2,
	ENVELOPE_3,
	LFO_LOCAL_1,
	LFO_LOCAL_2,
	X,
	Y,
	AFTERTOUCH,
	VELOCITY,
	NOTE,
	RANDOM,
	NONE,
};

namespace detail {
inline constexpr std::array<std::string_view, 16> kSourceNames{
    "lfo1", "lfo3", "compressor", "envelope1",  "envelope2", "envelope3", "envelope4", "lfo2",
    "lfo4", "x",    "y",          "aftertouch", "velocity",  "note",      "random",    "none"};
} // namespace detail

/// @brief The name a file gives @p source.
[[nodiscard]] constexpr std::string_view to_string(PatchSource source) {
	return detail::kSourceNames[static_cast<size_t>(source)];
}

/// @brief The source a file's @p name names; NONE when it names none the instrument knows.
[[nodiscard]] constexpr PatchSource parse_patch_source(std::string_view name) {
	for (size_t s = 0; s < detail::kSourceNames.size(); ++s) {
		if (detail::kSourceNames[s] == name) {
			return static_cast<PatchSource>(s);
		}
	}
	return PatchSource::NONE;
}

/// @brief Whether a cable's modulation swings both ways around zero or only up from it.
enum class Polarity : uint8_t { UNIPOLAR, BIPOLAR };

/// @brief The name a file gives @p polarity.
[[nodiscard]] constexpr std::string_view to_string(Polarity polarity) {
	return polarity == Polarity::UNIPOLAR ? "unipolar" : "bipolar";
}

/// @brief The polarity a file's @p name names; BIPOLAR for any other name.
[[nodiscard]] constexpr Polarity parse_polarity(std::string_view name) {
	return name == "unipolar" ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
}

/// @brief The bit every cable depth id has and no param id has.
///
/// A depth id is `kCableDepthFlag | p | (s1+1) << 8 | (s2+1) << 14 | (s3+1) << 20`: `p` (8 bits) the destination
/// param's id, `s1` the source of the cable nearest the param, `s2` and `s3` the sources of cables controlling the
/// depth below them; each source field is 6 bits holding the source's value + 1, 0 when absent. Pinned forever by
/// tests/engines/deluge/cables_spec.cpp.
inline constexpr uint32_t kCableDepthFlag = 0x4000'0000;

/// @brief The destination of a cable whose destination name the instrument does not know. The runtime keeps such a
/// cable and leaves it out of patching.
inline constexpr engine::ParamId kUnknownDestination{55};
/// @brief The pre-3.2 `"range"` destination a file's range cable has before its adjusted cable is known.
inline constexpr engine::ParamId kRangePlaceholder{89};

/// @brief A cable's ends: its source and its destination (a param, or another cable's depth).
struct CableRoute {
	PatchSource source{PatchSource::NONE};
	engine::ParamId destination{};
	[[nodiscard]] constexpr bool operator==(const CableRoute&) const = default;
};

namespace detail {
inline constexpr uint32_t kSourceShift[3] = {8, 14, 20};
[[nodiscard]] constexpr uint32_t source_field(uint32_t id, int level) {
	return (id >> kSourceShift[level]) & 0x3Fu;
}
[[nodiscard]] constexpr int depth_level(uint32_t id) {
	int level = 0;
	while (level < 3 && source_field(id, level) != 0) {
		++level;
	}
	return level;
}
} // namespace detail

/// @brief Whether @p id is a cable's depth.
[[nodiscard]] constexpr bool is_cable_depth(engine::ParamId id) {
	return (id.value & kCableDepthFlag) != 0;
}

/// @brief The depth id of the cable from @p source to @p destination; nullopt when the destination already carries
/// three sources or is neither a cable depth nor a param id below 256.
[[nodiscard]] constexpr std::optional<engine::ParamId> cable_depth_id(PatchSource source, engine::ParamId destination) {
	const uint32_t field = static_cast<uint32_t>(source) + 1;
	if (!is_cable_depth(destination)) {
		if (destination.value > 0xFFu) {
			return std::nullopt;
		}
		return engine::ParamId{kCableDepthFlag | destination.value | field << detail::kSourceShift[0]};
	}
	const int level = detail::depth_level(destination.value);
	if (level == 3) {
		return std::nullopt;
	}
	return engine::ParamId{destination.value | field << detail::kSourceShift[level]};
}

/// @brief The source and destination of the cable whose depth is @p depth; nullopt when @p depth is not a cable's.
[[nodiscard]] constexpr std::optional<CableRoute> cable_route(engine::ParamId depth) {
	if (!is_cable_depth(depth)) {
		return std::nullopt;
	}
	const int level = detail::depth_level(depth.value);
	if (level == 0) {
		return std::nullopt;
	}
	const uint32_t field = detail::source_field(depth.value, level - 1);
	const uint32_t without = depth.value & ~(0x3Fu << detail::kSourceShift[level - 1]);
	const engine::ParamId destination{level == 1 ? without & 0xFFu : without};
	return CableRoute{static_cast<PatchSource>(field - 1), destination};
}

} // namespace deluge::instrument
