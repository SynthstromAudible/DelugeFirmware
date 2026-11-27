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

#include "effects/filter_slots.h"
#include "instruments/classic/block_facts.h"
#include "instruments/classic/codec.h"
#include "instruments/classic/sound_state.h"
#include "instruments/classic/voice_pool.h"
#include "plugin/plugin.h"
#include "synth/voice/block_values.h"
#include "synth/voice/host_block.h"
#include "synth/voice/sound_config.h"

#include <bitset>
#include <cstdint>
#include <utility>

namespace deluge::instruments::classic {

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
	DX7_TUNING,        ///< the DX7 engine's transpose and cents
	DX7_PATCH,         ///< the DX7 engine's voice
	RETRIGGER,         ///< the oscillators', modulators' and DX7's retrigger phases
	MODULATOR_TUNING,  ///< FM's modulators' transposes and cents
	MODULATOR_ROUTING, ///< modulator 1 into modulator 0
	LFO_WAVE,
	LFO_SYNC,
	EFFECTS,
};

/// @brief The parts an edit changed.
using Parts = std::bitset<std::to_underlying(Part::EFFECTS) + 1>;

/// @brief The Deluge synth: a sound's settings (SoundState), its codec, and what the settings make of the sound.
struct DelugeSynth {
	using State = SoundState;
	/// @brief Its id, state version and kind.
	static constexpr plugin::PluginDescriptor descriptor() {
		return {.id = plugin::kDelugeInstrument, .version = 1, .kind = plugin::PluginKind::INSTRUMENT};
	}
	/// @brief The instrument's entries for @p state (instruments::classic::encode).
	static serialization::Value encode(const State& state, const plugin::CodecContext& context) {
		return instruments::classic::encode(state, context);
	}
	/// @brief Read @p value into @p state (instruments::classic::decode).
	static std::expected<void, plugin::StateError> decode(const serialization::Value& value, State& state,
	                                                      const plugin::CodecContext& context) {
		return instruments::classic::decode(value, state, context);
	}
	/// @brief The config @p state makes: every value a sound's voices read, derived here and only here.
	[[nodiscard]] static synth::SoundConfig derive(const State& state) noexcept;
	/// @brief Which parts differ between @p before and @p after.
	[[nodiscard]] static Parts changes(const State& before, const State& after) noexcept;

	/// @brief The values a block of a sound's voices plays with, from the engine's facts and the sound's own. A
	///        filter runs when it is set and could be heard: the low-pass when driven, modulated, below its top or
	///        morphed; the high-pass when modulated, above its bottom or morphed.
	/// @param host The engine's facts for the block.
	/// @param facts The sound's own facts for the block.
	/// @return The block's values.
	[[nodiscard]] static synth::BlockValues begin_block(const synth::HostBlock& host,
	                                                    const SoundBlockFacts& facts) noexcept;

	/// @brief One sound's synth: the config its state derives and the voices it plays.
	/// @tparam H The host its voices come from (synth::voice_host).
	template <synth::voice_host H>
	class Instance {
	public:
		/// @brief An instance with the default config and no voices, over @p host.
		/// @param host The host the voices come from; it outlives the instance.
		explicit Instance(H& host) : voices_{host} {}

		/// @brief Derives the config from @p state.
		/// @param state The sound's state.
		void configure(const State& state) { config_ = derive(state); }
		/// @brief The config the voices play.
		/// @return The config.
		[[nodiscard]] const synth::SoundConfig& config() const { return config_; }
		/// @brief The sound's voices.
		/// @return The pool.
		[[nodiscard]] VoicePool<H>& voices() { return voices_; }
		/// @brief The sound's voices, to read.
		/// @return The pool.
		[[nodiscard]] const VoicePool<H>& voices() const { return voices_; }

	private:
		synth::SoundConfig config_{};
		VoicePool<H> voices_;
	};
};

static_assert(plugin::InstrumentPlugin<DelugeSynth>);

} // namespace deluge::instruments::classic
