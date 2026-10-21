/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "engine-api/state_handle.h"
#include "serialization/value.h"

#include <cstdint>
#include <string>

namespace deluge::project {

/// @brief The instrument a synth track or a sound kit row plays: which engine, and its state, which the model holds
/// without knowing its type. The sound playing it binds to that state.
struct InstrumentSlot {
	std::string engine;        ///< the engine's id, e.g. "deluge.instrument"
	engine::StateHandle state; ///< the engine's state
	/// The song's input tick magnitude when the slot was filled: the context an edit reads and writes its sync levels
	/// in.
	int32_t tick_magnitude{1};
	bool operator==(const InstrumentSlot&) const = default;
};

/// @brief How the model reads and writes a sound element's instrument without understanding it: the host supplies it.
struct InstrumentCodec {
	/// The instrument a sound element holds, its sync levels relative to @p tick_magnitude.
	engine::StateHandle (*decode)(const serialization::Value& element, int32_t tick_magnitude) = nullptr;
	/// Writes @p state's entries into @p element, in the order a sound element holds them, its sync levels relative to
	/// @p tick_magnitude.
	void (*encode)(const engine::StateHandle& state, serialization::Value& element, int32_t tick_magnitude) = nullptr;
};

/// @brief Which slot a setting names: a synth track's, by its output's id, or a sound kit row's, by its drum's id.
struct SlotAddress {
	enum class Kind : uint8_t { OUTPUT, DRUM };
	Kind kind{Kind::OUTPUT};
	uint32_t id{0};
	bool operator==(const SlotAddress&) const = default;
};

} // namespace deluge::project
