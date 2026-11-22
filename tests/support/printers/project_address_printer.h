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

#include "project/clip_address.h"
#include "project/fx_chain.h"
#include "project/instrument_slot.h"
#include "project/kit_row_address.h"
#include "project/note_address.h"
#include "project/setting_group.h"

#include <format>
#include <ostream>
#include <utility>
#include <variant>

/// @file
/// @brief Prints the model's addresses in a failed CppSpec comparison, alone or as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints a clip address as its durable id, e.g. `{clip 7}`.
/// @param os The stream to print to.
/// @param a The address.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ClipAddress& a) {
	return os << std::format("{{clip {}}}", a.clip_id);
}

/// @brief Prints a slot address as its kind's number and its id, e.g. `{kind 1, id 4}`.
/// @param os The stream to print to.
/// @param a The address.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SlotAddress& a) {
	return os << std::format("{{kind {}, id {}}}", std::to_underlying(a.kind), a.id);
}

/// @brief Prints a chain address as its kind's number and its id, e.g. `{kind 0, id 3}`.
/// @param os The stream to print to.
/// @param a The address.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ChainAddress& a) {
	return os << std::format("{{kind {}, id {}}}", std::to_underlying(a.kind), a.id);
}

/// @brief Prints a track address as its output's durable id, e.g. `{output 2}`.
/// @param os The stream to print to.
/// @param a The address.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const TrackAddress& a) {
	return os << std::format("{{output {}}}", a.output_id);
}

/// @brief Prints a kit row address as its kit's and its drum's durable ids, e.g. `{kit 2, drum 9}`.
/// @param os The stream to print to.
/// @param a The address.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const KitRowAddress& a) {
	return os << std::format("{{kit {}, drum {}}}", a.kit_id, a.drum_id);
}

/// @brief Prints a note row address as its clip's durable id, whether it is a kit row and its key, e.g.
///        `{clip 7, kit 0, key 60}`.
/// @param os The stream to print to.
/// @param a The address.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const NoteRowAddress& a) {
	return os << std::format("{{clip {}, kit {:d}, key {}}}", a.clip_id, a.is_kit, a.row_key);
}

/// @brief Prints the song's own address, `{song}`.
/// @param os The stream to print to.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SongAddress&) {
	return os << "{song}";
}

/// @brief Prints whose mod-knob mode a gesture names as the address it holds, e.g. `{song}` or `{output 4}`.
/// @param os The stream to print to.
/// @param owner The owner.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const KnobModeOwner& owner) {
	std::visit([&os](const auto& address) { os << address; }, owner);
	return os;
}

} // namespace deluge::project
