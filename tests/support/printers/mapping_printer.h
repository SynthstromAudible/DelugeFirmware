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

#include "deluge-project/mapping_table.h"
#include "support/printers/param_id_printer.h"

#include <format>
#include <ostream>
#include <utility>
#include <variant>

/// @file
/// @brief Prints learned MIDI commands, their targets and the mapping table in a failed CppSpec comparison, alone or
///        as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints a device reference as its port's number, then a hosted device's name and USB ids, e.g.
///        `{port 4, name "KEYS", vendor 1234, product 5}`.
/// @param os The stream to print to.
/// @param d The device reference.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const MidiDeviceRef& d) {
	return os << std::format("{{port {}, name \"{}\", vendor {}, product {}}}", std::to_underlying(d.port), d.name,
	                         d.vendor_id, d.product_id);
}

/// @brief Prints a command as its channel or zone, its note or CC (255 for none) and its device, e.g.
///        `{channel 18, note_or_cc 74, device any}`.
/// @param os The stream to print to.
/// @param c The command.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const MidiCommand& c) {
	os << std::format("{{channel {}, note_or_cc {}, device ", c.channel_or_zone, c.note_or_cc);
	if (c.device) {
		os << *c.device;
	}
	else {
		os << "any";
	}
	return os << "}";
}

/// @brief Prints a controls owner as its kind's number and its id, e.g. `{kind 0, id 3}`.
/// @param os The stream to print to.
/// @param a The owner.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ControlsAddress& a) {
	return os << std::format("{{kind {}, id {}}}", std::to_underlying(a.kind), a.id);
}

/// @brief Prints a param target as its owner and its param, e.g. `{owner {kind 0, id 3}, param ParamId 12}`.
/// @param os The stream to print to.
/// @param t The target.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const OwnerParamTarget& t) {
	return os << "{owner " << t.owner << ", param " << t.param << "}";
}

/// @brief Prints a clip's mute target as its clip's id, e.g. `{clip_mute 7}`.
/// @param os The stream to print to.
/// @param t The target.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ClipMuteTarget& t) {
	return os << std::format("{{clip_mute {}}}", t.clip_id);
}

/// @brief Prints a drum's mute target as its drum's id, e.g. `{drum_mute 9}`.
/// @param os The stream to print to.
/// @param t The target.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DrumMuteTarget& t) {
	return os << std::format("{{drum_mute {}}}", t.drum_id);
}

/// @brief Prints a section launch target as its section, e.g. `{section_launch 2}`.
/// @param os The stream to print to.
/// @param t The target.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SectionLaunchTarget& t) {
	return os << std::format("{{section_launch {}}}", t.section);
}

/// @brief Prints a track's input target as its output's id, e.g. `{track_input 4}`.
/// @param os The stream to print to.
/// @param t The target.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const TrackInputTarget& t) {
	return os << std::format("{{track_input {}}}", t.output_id);
}

/// @brief Prints a kit lane's input target as its drum's id, e.g. `{lane_input 9}`.
/// @param os The stream to print to.
/// @param t The target.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const LaneInputTarget& t) {
	return os << std::format("{{lane_input {}}}", t.drum_id);
}

/// @brief Prints a mapping target as the target it holds, e.g. `{drum_mute 9}`.
/// @param os The stream to print to.
/// @param t The target.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const MappingTarget& t) {
	std::visit([&os](const auto& target) { os << target; }, t);
	return os;
}

/// @brief Prints a mapping as its source, its target and its relative flag, 1 for set.
/// @param os The stream to print to.
/// @param m The mapping.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Mapping& m) {
	return os << "{source " << m.source << ", target " << m.target << ", relative " << int{m.relative} << "}";
}

/// @brief Prints a mapping table as its entries, in table order.
/// @param os The stream to print to.
/// @param t The table.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const MappingTable& t) {
	os << "[";
	const char* separator = "";
	for (const Mapping& m : t.entries()) {
		os << separator << m;
		separator = ", ";
	}
	return os << "]";
}

} // namespace deluge::project
