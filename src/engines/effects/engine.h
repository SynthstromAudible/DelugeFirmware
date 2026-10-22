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

/// @file
/// @brief The Deluge effects as an engine: a host chain's effect states and their codec, as engine::InstrumentEngine
/// names them, so a chain record can hold them through an engine::StateHandle.

#include "engine-api/engine.h"
#include "engines/effects/codec.h"
#include "engines/effects/state.h"

namespace deluge::effects {

/// @brief The Deluge effects: a host owner's effect chain (EffectStates), read and written by the effects' codec.
struct DelugeEffects {
	using State = EffectStates;
	/// @brief Its id, state version and kind.
	static constexpr engine::EngineDescriptor descriptor() {
		return {.id = engine::kDelugeEffects, .version = 1, .kind = engine::EngineKind::EFFECT};
	}
	/// @brief The effect entries for @p state (effects::encode).
	static serialization::Value encode(const State& state, const engine::CodecContext& context) {
		return effects::encode(state, context);
	}
	/// @brief Read @p value, an owner element's effect entries, into @p state (effects::decode).
	static std::expected<void, engine::StateError> decode(const serialization::Value& value, State& state,
	                                                      const engine::CodecContext& context) {
		if (!value.isMap()) {
			return std::unexpected(engine::StateError::NOT_A_MAP);
		}
		effects::decode(value, state, context);
		return {};
	}
};

static_assert(engine::InstrumentEngine<DelugeEffects>);

} // namespace deluge::effects
