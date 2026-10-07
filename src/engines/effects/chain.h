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

#include "engine-api/engine.h"
#include "engines/effects/state.h"
#include "serialization/value.h"

#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace deluge::effects {

/// @brief Where a chain runs: a kit's or an audio track's output, or the song's master.
enum class Home : uint8_t { CLIP, MASTER };

/// @brief One slot of a chain: its effect's id and the owner element's entries that effect owns, as a file holds them.
struct SlotEntries {
	std::string_view effect;
	serialization::Value entries; ///< a map: e.g. filter {lpfMode, hpfMode, filterRoute}, delay {delay: {...}}
};

/// @brief The effects of @p home in processing order (the master's two non-processing slots last).
[[nodiscard]] std::span<const std::string_view> chain_effects(Home home);

/// @brief The index of the master's first slot after the reverb returns; nothing for a clip chain.
[[nodiscard]] std::optional<uint32_t> reverb_return(Home home);

/// @brief Which effect owns the setting at @p path (a kEffectSettings path), or nothing.
[[nodiscard]] std::optional<std::string_view> effect_of_setting(std::string_view path);

/// @brief Each of @p home's slots as the owner element holds it, sync levels relative to @p context.
/// @param compressor_threshold As for encode_pieces: a clip chain's threshold is its param.
[[nodiscard]] std::vector<SlotEntries> encode_chain(const EffectStates& states, Home home,
                                                    const engine::CodecContext& context,
                                                    std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief Reads one slot's entries onto @p states (the effect's part only).
void decode_slot(std::string_view effect, const serialization::Value& entries, EffectStates& states,
                 const engine::CodecContext& context);

/// @brief The view entries a host owner's element holds (modFXCurrentParam, currentFilterType), as a map.
[[nodiscard]] serialization::Value view_entries(const serialization::Value& element);

/// @brief Writes @p view's entries, then @p slots' entries, into @p element in the order an owner element holds them.
void write_chain(const std::vector<SlotEntries>& slots, const serialization::Value& view,
                 serialization::Value& element);

} // namespace deluge::effects
