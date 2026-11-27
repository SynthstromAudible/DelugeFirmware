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

#include "instruments/classic/params.h"
#include "plugin/types/param.h"
#include "synth/modulation/cable.h"
#include "synth/modulation/patch_source.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::instruments::classic {

/// @brief What a patch cable carries: the synth's modulation sources.
using PatchSource = synth::PatchSource;

namespace detail {
inline constexpr std::array<std::string_view, 16> kSourceNames{
    "lfo1", "lfo3", "compressor", "envelope1",  "envelope2", "envelope3", "envelope4", "lfo2",
    "lfo4", "x",    "y",          "aftertouch", "velocity",  "note",      "random",    "none"};
} // namespace detail

/// @brief The name a file gives @p source.
/// @param source The patch source.
/// @return The source's file name.
[[nodiscard]] constexpr std::string_view to_string(PatchSource source) {
	return detail::kSourceNames[static_cast<size_t>(source)];
}

/// @brief The source a file's @p name names; NONE when it names none the instrument knows.
/// @param name A source name from a file.
/// @return The named source, or NONE.
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
/// @param polarity The polarity.
/// @return The polarity's file name.
[[nodiscard]] constexpr std::string_view to_string(Polarity polarity) {
	return polarity == Polarity::UNIPOLAR ? "unipolar" : "bipolar";
}

/// @brief The polarity a file's @p name names; BIPOLAR for any other name.
/// @param name A polarity name from a file.
/// @return The named polarity.
[[nodiscard]] constexpr Polarity parse_polarity(std::string_view name) {
	return name == "unipolar" ? Polarity::UNIPOLAR : Polarity::BIPOLAR;
}

/// @brief Whether a cable from @p source has a polarity: X and Y cannot be converted, so they ignore the setting.
/// @param source The cable's source.
/// @return False for X and Y, true for every other source.
[[nodiscard]] constexpr bool has_polarity(PatchSource source) {
	return source != PatchSource::X && source != PatchSource::Y;
}

/// @brief The polarity a new cable from @p source must take, or none where it takes the device's default.
///
/// Aftertouch is stored unipolar (bipolar would leave the default patch near silent); MPE X and Y and the sidechain
/// are bipolar.
/// @param source The cable's source.
/// @return The source's fixed polarity, or nullopt for the device's default.
[[nodiscard]] constexpr std::optional<Polarity> fixed_polarity(PatchSource source) {
	if (source == PatchSource::AFTERTOUCH) {
		return Polarity::UNIPOLAR;
	}
	if (source == PatchSource::X || source == PatchSource::Y || source == PatchSource::SIDECHAIN) {
		return Polarity::BIPOLAR;
	}
	return std::nullopt;
}

/// @brief @p polarity as the patch-cable menu's short column shows it.
/// @param polarity The polarity.
/// @return "UPLR" or "BPLR".
[[nodiscard]] constexpr std::string_view to_short_string(Polarity polarity) {
	return polarity == Polarity::BIPOLAR ? "BPLR" : "UPLR";
}

/// @brief The bit every cable depth id has and no param id has: plugin::kCableDepthFlag, which the controls library
/// reads too.
///
/// A depth id is `kCableDepthFlag | p | (s1+1) << 8 | (s2+1) << 14 | (s3+1) << 20`: `p` (8 bits) the destination
/// param's id, `s1` the source of the cable nearest the param, `s2` and `s3` the sources of cables controlling the
/// depth below them; each source field is 6 bits holding the source's value + 1, 0 when absent. Pinned forever by
/// tests/unit/instruments/classic/cables_spec.cpp.
using plugin::kCableDepthFlag;

/// @brief The destination of a cable whose destination name the instrument does not know. The runtime keeps such a
/// cable and leaves it out of patching.
inline constexpr plugin::ParamId kUnknownDestination{55};

/// @brief A cable's ends: its source and its destination (a param, or another cable's depth).
struct CableRoute {
	PatchSource source{PatchSource::NONE};
	plugin::ParamId destination{};
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
/// The param a cable's depth id roots at: the low byte, whatever the cable's depth level.
[[nodiscard]] constexpr uint32_t root_param_field(uint32_t id) {
	return id & 0xFFu;
}
} // namespace detail

/// @brief The target of a cable onto the patched param @p patched. A patched param's id is its index in the patched
/// set, which is how a cable's word names its param.
/// @param patched A patched param's id (@c is_patched).
/// @return The cable target.
[[nodiscard]] constexpr synth::CableTarget cable_target_of(plugin::ParamId patched) {
	static_assert(kParamBlocks[0].first.value == 0);
	return synth::CableTarget::param(static_cast<uint8_t>(patched.value));
}

/// @brief Whether @p id is a cable's depth.
/// @param id A param id.
/// @return True when the id carries kCableDepthFlag.
[[nodiscard]] constexpr bool is_cable_depth(plugin::ParamId id) {
	return (id.value & kCableDepthFlag) != 0;
}

/// @brief The depth id of the cable from @p source to @p destination; nullopt when the destination already carries
/// three sources or is neither a cable depth nor a param id below 256.
/// @param source      The cable's source.
/// @param destination The param, or the depth of the cable this one controls.
/// @return The depth id, or nullopt.
[[nodiscard]] constexpr std::optional<plugin::ParamId> cable_depth_id(PatchSource source, plugin::ParamId destination) {
	const uint32_t field = static_cast<uint32_t>(source) + 1;
	if (!is_cable_depth(destination)) {
		if (destination.value > 0xFFu) {
			return std::nullopt;
		}
		return plugin::ParamId{kCableDepthFlag | destination.value | field << detail::kSourceShift[0]};
	}
	const int level = detail::depth_level(destination.value);
	if (level == 3) {
		return std::nullopt;
	}
	return plugin::ParamId{destination.value | field << detail::kSourceShift[level]};
}

/// @brief The source and destination of the cable whose depth is @p depth; nullopt when @p depth is not a cable's.
/// @param depth A cable depth id.
/// @return The cable's route, or nullopt.
[[nodiscard]] constexpr std::optional<CableRoute> cable_route(plugin::ParamId depth) {
	if (!is_cable_depth(depth)) {
		return std::nullopt;
	}
	const int level = detail::depth_level(depth.value);
	if (level == 0) {
		return std::nullopt;
	}
	const uint32_t field = detail::source_field(depth.value, level - 1);
	const uint32_t without = depth.value & ~(0x3Fu << detail::kSourceShift[level - 1]);
	const plugin::ParamId destination{level == 1 ? without & 0xFFu : without};
	return CableRoute{static_cast<PatchSource>(field - 1), destination};
}

/// @brief The depth id of the cable the runtime's packed cable word @p word names: the low byte is the destination
/// param, then one byte per source from the one nearest the param, 0xFF for none (synth::CableTarget's layout).
/// A source that does not fit the destination (a fourth level, a destination the depth id cannot carry) is left out.
/// @param word The packed cable word.
/// @return The cable's depth id.
[[nodiscard]] constexpr plugin::ParamId cable_depth_of_word(uint32_t word) {
	plugin::ParamId id{word & 0xFFu};
	for (uint32_t shift = 8; shift <= 24; shift += 8) {
		const uint32_t source = (word >> shift) & 0xFFu;
		if (source == 0xFFu) {
			break;
		}
		id = cable_depth_id(static_cast<PatchSource>(source), id).value_or(id);
	}
	return id;
}

/// @brief The runtime's packed cable word of the depth id @p depth: the inverse of cable_depth_of_word. A param id
/// that is not a cable's depth packs as a cable to that param with no sources.
/// @param depth A cable depth id.
/// @return The packed cable word.
[[nodiscard]] constexpr uint32_t cable_word_of(plugin::ParamId depth) {
	std::array<uint32_t, 3> sources{0xFFu, 0xFFu, 0xFFu};
	std::size_t n = 0;
	plugin::ParamId at = depth;
	while (const std::optional<CableRoute> route = cable_route(at)) {
		sources[n++] = static_cast<uint32_t>(route->source);
		at = route->destination;
	}
	// The route unwinds from the outermost source in; the word holds the innermost at bits 8-15.
	std::reverse(sources.begin(), sources.begin() + static_cast<std::ptrdiff_t>(n));
	return (at.value & 0xFFu) | sources[0] << 8 | sources[1] << 16 | sources[2] << 24;
}

/// @brief The depth id of the cable from @p from to @p destination.
/// @param from        The cable's source.
/// @param destination The cable's destination descriptor.
/// @return The cable's depth id.
[[nodiscard]] constexpr plugin::ParamId cable_depth_of(PatchSource from, synth::CableTarget destination) {
	return cable_depth_of_word(destination.add_source(from).word);
}

/// @brief The id of what a mod knob's descriptor word points at: a param by its flat index (a patched param's own id,
/// an unpatched one's `kUnpatchedFirstId` (params.h) on from its runtime index: the instrument's id either way), a
/// cable by its depth id.
/// @param word The knob's descriptor word, not the null one.
/// @return The param's or the cable's depth id.
[[nodiscard]] constexpr plugin::ParamId knob_param_of(synth::CableTarget word) {
	return word.is_param() ? plugin::ParamId{word.param_index()} : cable_depth_of_word(word.word);
}

} // namespace deluge::instruments::classic
