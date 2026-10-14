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

#include "serialization/value.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace deluge::project {

/// @brief One effect of a chain: which effect, and the owner element's entries it owns, kept as read. The model never
/// interprets `state`.
struct FxSlot {
	std::string effect;                                           ///< an effect id, e.g. "delay"
	serialization::Value state = serialization::Value::makeMap(); ///< the entries, in file order
	/// The song's input tick magnitude when `state` was written: its sync levels are relative to it.
	int32_t tick_magnitude{1};
	bool operator==(const FxSlot&) const = default;
};

/// @brief A host owner's effects in processing order, the master's split at the reverb returns, and which of its
/// values the owner's knobs edit (its saved view).
struct FxChain {
	std::vector<FxSlot> slots;
	std::optional<uint32_t> reverb_return;                       ///< the master's first slot after the reverb returns
	serialization::Value view = serialization::Value::makeMap(); ///< modFXCurrentParam, currentFilterType
	bool operator==(const FxChain&) const = default;
};

/// @brief Which chain an Edit names: a kit's or an audio track's, by its output's id, or the song master's.
struct ChainAddress {
	enum class Kind : uint8_t { OUTPUT, SONG };
	Kind kind{Kind::OUTPUT};
	uint32_t id{0};
	bool operator==(const ChainAddress&) const = default;
};

/// @brief Where a chain runs, as the model names it.
enum class ChainHome : uint8_t { CLIP, MASTER };

/// @brief How the model reads and writes an owner element's chain without understanding effects: the host supplies it.
struct ChainCodec {
	/// The chain an owner element of @p home holds, its sync levels relative to @p tick_magnitude.
	FxChain (*decode)(const serialization::Value& element, ChainHome home, int32_t tick_magnitude) = nullptr;
	/// Writes @p chain's entries into @p element, in the order an owner element holds them, its sync levels relative
	/// to @p tick_magnitude (each slot's are relative to its own).
	void (*encode)(const FxChain& chain, serialization::Value& element, int32_t tick_magnitude) = nullptr;
};

} // namespace deluge::project
