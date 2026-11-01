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
/// @brief The Deluge effects as a plugin: a host chain's effect states and their codec, as plugin::InstrumentPlugin
/// names them, so a chain record can hold them through an plugin::StateHandle.

#include "effects/codec.h"
#include "effects/state.h"
#include "plugin-api/plugin.h"

namespace deluge::effects {

/// @brief The Deluge effects: a host owner's effect chain (EffectStates), read and written by the effects' codec.
struct DelugeEffects {
	using State = EffectStates;
	/// @brief Its id, state version and kind.
	static constexpr plugin::PluginDescriptor descriptor() {
		return {.id = plugin::kDelugeEffects, .version = 1, .kind = plugin::PluginKind::EFFECT};
	}
	/// @brief The effect entries for @p state (effects::encode).
	static serialization::Value encode(const State& state, const plugin::CodecContext& context) {
		return effects::encode(state, context);
	}
	/// @brief Read @p value, an owner element's effect entries, into @p state (effects::decode).
	static std::expected<void, plugin::StateError> decode(const serialization::Value& value, State& state,
	                                                      const plugin::CodecContext& context) {
		if (!value.isMap()) {
			return std::unexpected(plugin::StateError::NOT_A_MAP);
		}
		effects::decode(value, state, context);
		return {};
	}
};

static_assert(plugin::InstrumentPlugin<DelugeEffects>);

} // namespace deluge::effects
