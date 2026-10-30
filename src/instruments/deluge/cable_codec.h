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

#include "instruments/deluge/cables.h"
#include "plugin-api/types/param.h"
#include "serialization/value.h"

#include <cstddef>
#include <functional>
#include <span>
#include <vector>

namespace deluge::instrument {

/// @brief The most cables one param manager holds.
inline constexpr size_t kMaxCables = 32;

/// @brief One cable as a file holds it: its route, its polarity, and its depth's automation leaf, which the host reads.
struct CableState {
	PatchSource source{PatchSource::NONE};
	/// A param, another cable's depth, or kUnknownDestination.
	plugin::ParamId destination{};
	Polarity polarity{Polarity::BIPOLAR};
	/// The cable's `amount`: the host's automation leaf, pointing into the decoded tree.
	const serialization::Value* amount{nullptr};

	/// @brief The id of this cable's depth.
	/// @return The depth id, or a default-constructed id when the destination already carries three sources or is
	///         not a valid destination (neither a cable depth nor a param id below 256).
	[[nodiscard]] plugin::ParamId depth() const;
};

/// @brief Whether an automation leaf holds something (a non-zero value, or automation): the host's reading of it.
using AmountTest = std::function<bool(const serialization::Value& amount)>;

/// @brief The cables a `patchCables` node holds, as the runtime keeps them: each cable's depth-controlling cables
/// first, then the cable; at most kMaxCables.
///
/// Drops what the runtime drops: a cable with no known source, no destination or an empty amount, `x` → `pitch`
/// (a 3.2.0-alpha mistake), a cable repeating an earlier cable's source and destination, and a depth-controlling
/// cable repeating a source among its siblings. An AFTERTOUCH cable defaults to unipolar.
/// @param patchCables         The file's `patchCables` node.
/// @param contains_something The host's test of whether an `amount` leaf holds something.
/// @return The kept cables, depth controllers before the cable they control.
[[nodiscard]] std::vector<CableState> decode_cables(const serialization::Value& patchCables,
                                                    const AmountTest& contains_something);

/// @brief One cable as a file writes it: a cable to a param, and the cables controlling its depth, each with the index
/// of its record, whose depth lane the host renders as the cable's `amount`.
struct EncodedCable {
	CableRoute route;
	Polarity polarity{Polarity::BIPOLAR};
	size_t record{};
	struct Controller {
		PatchSource source{PatchSource::NONE};
		Polarity polarity{Polarity::BIPOLAR};
		size_t record{};
	};
	std::vector<Controller> depth_controllers;
};

/// @brief The cables a `patchCables` node holds, in the records' order: each cable to a param, followed by the cables
/// whose destination is its depth, in the records' order. A depth cable is written only beneath the cable it controls,
/// so one whose cable is absent is not written.
/// @param records The clip's cables in the order the save walks them (their `amount` is not read).
[[nodiscard]] std::vector<EncodedCable> encode_cables(std::span<const CableState> records);

} // namespace deluge::instrument
