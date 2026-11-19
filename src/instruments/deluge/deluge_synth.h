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
/// @brief The Deluge synth as a plugin: its state and codec, what its state makes of a sound (its config), and which
/// parts of the state an edit changed.

#include "instruments/deluge/codec.h"
#include "instruments/deluge/sound_state.h"
#include "plugin-api/plugin.h"
#include "synth/voice/sound_config.h"

#include <bitset>
#include <cstdint>
#include <utility>

namespace deluge::instrument {

/// @brief A part of a sound's state whose change has a runtime effect of its own.
enum class Part : uint8_t {
	VOICING, ///< priority, polyphony, max voices
	TUNING,  ///< the master transpose and cents
	UNISON_COUNT,
	UNISON_DETUNE,
	UNISON_SPREAD,
	MODE, ///< which engine
	OSC_SYNC,
	SOURCE_A_TYPE,
	SOURCE_B_TYPE,
	SOURCE_A_TUNING, ///< an oscillator's own transpose and cents
	SOURCE_B_TUNING,
	SOURCE_A_PLAYBACK, ///< loop mode, reversed, time stretch, interpolation
	SOURCE_B_PLAYBACK,
	SOURCE_A_RANGES,
	SOURCE_B_RANGES,
	SOURCE_A_DX,
	SOURCE_B_DX,
	RETRIGGER,         ///< the oscillators' and modulators' retrigger phases
	MODULATOR_TUNING,  ///< FM's modulators' transposes and cents
	MODULATOR_ROUTING, ///< modulator 1 into modulator 0
	LFO_WAVE,
	LFO_SYNC,
	EFFECTS,
	HOST, ///< the sidechain send and the MIDI output, the host's fields
};

/// @brief The parts an edit changed.
using Parts = std::bitset<std::to_underlying(Part::HOST) + 1>;

/// @brief The Deluge synth: a sound's settings (SoundState), its codec, and what the settings make of the sound.
struct DelugeSynth {
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
		FlatSoundState flat = state;
		auto result = instrument::decode(value, flat, context);
		state = flat;
		return result;
	}
	/// @brief The config @p state makes: every value a sound's voices read, derived here and only here.
	[[nodiscard]] static synth::SoundConfig derive(const State& state) noexcept;
	/// @brief Which parts differ between @p before and @p after.
	[[nodiscard]] static Parts changes(const State& before, const State& after) noexcept;
};

static_assert(plugin::InstrumentPlugin<DelugeSynth>);

} // namespace deluge::instrument
