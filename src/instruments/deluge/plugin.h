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
/// @brief The Deluge instrument as a plugin: its state type and codec, as plugin::InstrumentPlugin names them, so a
/// project record can hold its state through an plugin::StateHandle.

#include "instruments/deluge/codec.h"
#include "instruments/deluge/sound_state.h"
#include "plugin-api/plugin.h"

namespace deluge::instrument {

/// @brief The Deluge instrument: a sound's settings (SoundState), read and written by the instrument's codec.
struct DelugeInstrument {
	using State = SoundState;
	/// @brief Its id, state version and kind.
	static constexpr plugin::PluginDescriptor descriptor() {
		return {.id = plugin::kDelugeInstrument, .version = 1, .kind = plugin::PluginKind::INSTRUMENT};
	}
	/// @brief The instrument's entries for @p state (instrument::encode).
	static serialization::Value encode(const State& state, const plugin::CodecContext& context) {
		return instrument::encode(state, context);
	}
	/// @brief Read @p value into @p state (instrument::decode).
	static std::expected<void, plugin::StateError> decode(const serialization::Value& value, State& state,
	                                                      const plugin::CodecContext& context) {
		return instrument::decode(value, state, context);
	}
};

static_assert(plugin::InstrumentPlugin<DelugeInstrument>);

} // namespace deluge::instrument
