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

#include "instruments/deluge/cable_records.h"

#include <cstddef>
#include <cstdint>
#include <optional>

/// @file
/// @brief `deluge.mod_matrix/1`, the Deluge instrument's cable-editing extension, as host-free operations on one
/// clip's cables: each write's validation and its undo record, which cable an undo or redo takes away or brings back,
/// and the editing policies' decisions. The host applies a delta to the clip and compiles the routing.

namespace deluge::instrument {

/// @brief Which side of an edit a clip's cables are put back to: before it (undo) or after it (redo).
enum class CableSide : uint8_t { BEFORE, AFTER };

/// @brief One topology edit's undo record: the op, the cable before and after, its index, and the records before and
/// after the edit. The whole lists are kept because the compile reorders other cables and the order is saved.
struct CableDelta {
	enum class Op : uint8_t { ADD, REMOVE, SET_POLARITY, REROUTE };
	Op op{Op::ADD};
	uint8_t index{};
	CableRecord before{};
	CableRecord after{};
	CableRecords records_before{};
	CableRecords records_after{};

	/// @brief The cable the edit is about: the added one for an add, the edited or removed one otherwise.
	[[nodiscard]] const CableRecord& cable() const { return op == Op::ADD ? after : before; }
	/// @brief Whether that cable is among the clip's cables at @p side.
	[[nodiscard]] bool present_at(CableSide side) const {
		return side == CableSide::AFTER ? op != Op::REMOVE : op != Op::ADD;
	}
};

/// @brief An add of @p cable: refused when the cable is already there, it has no target, or the cables are full.
[[nodiscard]] std::optional<CableDelta> begin_add(const CableRecords& cables, CableRecord cable);
/// @brief A removal of cable @p index; refused when there is no such cable.
[[nodiscard]] std::optional<CableDelta> begin_remove(const CableRecords& cables, size_t index);
/// @brief A polarity change of cable @p index; refused when there is no such cable or it already has @p polarity.
[[nodiscard]] std::optional<CableDelta> begin_set_polarity(const CableRecords& cables, size_t index,
                                                           CablePolarity polarity);
/// @brief A move of cable @p index to @p target; refused when there is no such cable, the target is none, or the
/// cable's source already goes there.
[[nodiscard]] std::optional<CableDelta> begin_reroute(const CableRecords& cables, size_t index, CableTarget target);

/// @brief Create-on-edit: whether a depth written to the cable from @p source to @p target needs the cable added first.
[[nodiscard]] bool needs_cable(const CableRecords& cables, PatchSource source, CableTarget target);
/// @brief Delete-at-zero: whether a cable whose depth was just written goes, which it does when the depth is 0 with no
/// automation and the clip is the one its output plays.
/// @param depth_holds_something Whether the depth is non-zero or automated.
/// @param clip_active          Whether the clip is its output's active clip.
[[nodiscard]] bool settles(bool depth_holds_something, bool clip_active);

} // namespace deluge::instrument
