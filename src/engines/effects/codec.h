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

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

namespace deluge::effects {

/// @brief An owner element's effect entries, in file order, in the three places a file puts them: the attributes
/// after the owner's first ones, the delay, and the rest after the owner's own tags.
struct EffectPieces {
	serialization::Value attributes; ///< modFXType, lpfMode, hpfMode, filterRoute, clippingAmount (when not 0)
	serialization::Value delay;      ///< delay
	serialization::Value tags;       ///< sidechain, audioCompressor, stutter
};

/// @brief The entries a file writes for an owner's effects, the delay's and sidechain's sync levels relative to
/// @p context's tick magnitude.
/// @param states The effect states to encode.
/// @param context The codec context; sync levels are written relative to its tick magnitude.
/// @param compressor_threshold The threshold to write in place of the state's: a sound's file holds its param's, as
/// the render last applied it. A host chain's file holds the state's.
/// @return The entries, as pieces in file order.
[[nodiscard]] EffectPieces encode_pieces(const EffectStates& states, const engine::CodecContext& context,
                                         std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief The effects' encoding of top-level entry @p key, as encode_pieces writes it.
/// @param states               The effect states to encode.
/// @param key                  An owner element's top-level key.
/// @param context              The codec context; sync levels are written relative to its tick magnitude.
/// @param compressor_threshold As encode_pieces takes it.
/// @return The entry, or nullopt where @p states write none there (a clipping amount of 0) or @p key is not theirs.
[[nodiscard]] std::optional<serialization::Value>
encode_entry(const EffectStates& states, std::string_view key, const engine::CodecContext& context,
             std::optional<int32_t> compressor_threshold = std::nullopt);

/// @brief The pieces of encode_pieces as one Map, in file order.
/// @param states The effect states to encode.
/// @param context The codec context; sync levels are written relative to its tick magnitude.
/// @return The Map of effect entries.
[[nodiscard]] serialization::Value encode(const EffectStates& states, const engine::CodecContext& context);

/// @brief Read every effect entry @p element (an owner's element, a Map) holds into @p states, as the runtime's
/// reader does; what the element does not name keeps its value. A delay's feedback and rate are params, not read here.
/// @param element An owner's element (a Map).
/// @param states The effect states to fill in.
/// @param context The codec context; sync levels are read relative to its tick magnitude.
void decode(const serialization::Value& element, EffectStates& states, const engine::CodecContext& context);

/// @brief Whether @p element saves its delay's type: a `type` entry in its `delay` map.
///
/// A file from a firmware without the type saves only whether its delay is analog; its owner works out whether the
/// delay is off once its params are read.
/// @param element An owner's element (a Map).
/// @return True if the `delay` map holds a `type` entry.
[[nodiscard]] bool saves_delay_type(const serialization::Value& element);

/// @brief The effect entries encode writes, in file order.
inline constexpr std::array<std::string_view, 9> kEffectKeys{"modFXType",   "lpfMode",         "hpfMode",
                                                             "filterRoute", "clippingAmount",  "delay",
                                                             "sidechain",   "audioCompressor", "stutter"};

/// @brief Whether @p key, at the top of an owner element, is an effect entry: one encode writes, or the sidechain as
/// the factory presets on every card name it ("compressor").
/// @param key A top-level key of an owner element.
/// @return True if @p key is an effect entry.
[[nodiscard]] bool is_effect_key(std::string_view key);

/// @brief The part of @p states the effect entry @p key holds, copied from @p from.
/// @param key An effect entry key (see is_effect_key).
/// @param states The states to copy into.
/// @param from The states to copy from.
void keep_effect_entry(std::string_view key, EffectStates& states, const EffectStates& from);

} // namespace deluge::effects
