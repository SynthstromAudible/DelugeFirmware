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

#include "engine-api/types/engine_id.h"
#include "serialization/tree.h"
#include "serialization/value.h"

#include <concepts>
#include <cstdint>
#include <expected>

namespace deluge::engine {

/// @brief Where an engine plugs in: as a track's or kit row's instrument, or into an effect chain.
enum class EngineKind : uint8_t {
	INSTRUMENT,
	EFFECT,
};

/// @brief What an engine is: its id, the version of its state, and its kind.
struct EngineDescriptor {
	EngineId id;
	uint32_t version{1}; ///< raised when the state's meaning changes; decode reads every older version
	EngineKind kind{EngineKind::INSTRUMENT};
};

/// @brief What an engine's codec needs from the song it is in.
struct CodecContext {
	/// The song's input tick magnitude: sync values are stored relative to it (see sync_level_to_file).
	int32_t input_tick_magnitude{1};
};

/// @brief Why an engine could not read its state.
enum class StateError : uint8_t {
	NOT_A_MAP, ///< the state's node is not a Map
};

/// @brief An engine whose state the host stores as a Value: a descriptor, a state type, and a codec that writes the
/// state's entries in file order and reads them back, keeping any value the Value lacks.
template <class E>
concept InstrumentEngine = requires(const typename E::State& in, typename E::State& out,
                                    const serialization::Value& value, const CodecContext& context) {
	{ E::descriptor() } -> std::same_as<EngineDescriptor>;
	{ E::encode(in, context) } -> std::same_as<serialization::Value>;
	{ E::decode(value, out, context) } -> std::same_as<std::expected<void, StateError>>;
};

} // namespace deluge::engine
