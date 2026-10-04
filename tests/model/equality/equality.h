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

#include "deluge-project/document.h"
#include "serialization/value.h"

#include <expected>
#include <functional>
#include <string>
#include <string_view>

namespace deluge::project {

/// @brief Faults a spec injects into the round trip, to prove the gate catches them.
struct EqualityFaults {
	std::function<void(serialization::Value&)> on_encoded; ///< applied to the encoded document before it is written
	std::function<void(SongDocument&)> on_decoded;         ///< applied to the second decode
};

/// @brief The model-equality check for one song: parse @p songText, decode the model's parts (A), encode them, write
/// JSON, parse it and decode again (B); require A == B and that B encodes to the same JSON as A.
/// @return Nothing, or what differed, naming the first record that did.
[[nodiscard]] std::expected<void, std::string> check_equality(std::string_view songText, bool isJson,
                                                              const EqualityFaults& faults = {});

/// @brief Whether two saves of a song hold the same song: parse each (@p aIsJson, @p bIsJson say how), decode the
/// model's parts and require them equal.
/// @return Nothing, or what differed, naming the first record that did.
[[nodiscard]] std::expected<void, std::string> compare_songs(std::string_view a, bool aIsJson, std::string_view b,
                                                             bool bIsJson);

/// @brief Whether two documents hold the same tree (keys, order, scalar text); whitespace and layout are ignored.
/// @return Nothing, or the path of the first entry that differs.
[[nodiscard]] std::expected<void, std::string> compare_trees(std::string_view a, bool aIsJson, std::string_view b,
                                                             bool bIsJson);

} // namespace deluge::project
