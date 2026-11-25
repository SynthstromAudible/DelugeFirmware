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

#include "definitions_cxx.hpp" // SynthMode, VoicePriority, PolyphonyMode, kNumSources, kNumModulators, LFO_COUNT
#include "dsp/fixed/pitch/fine_tune.h"
#include "synth/modulation/envelope_state.h"
#include "synth/modulation/lfo_state.h"
#include "synth/sources/source_state.h" // kFreeRunningPhase
#include "synth/voice/unison.h"

#include <array>
#include <cstdint>

/// @file
/// @brief A sound's settings as its voices play them.

namespace deluge::synth {

/// @brief How a sound's voices are allocated and tuned.
struct Voicing {
	VoicePriority priority{VoicePriority::MEDIUM}; ///< ranks the sound's voices for stealing
	PolyphonyMode polyphony{PolyphonyMode::POLY};
	uint8_t max_voices{8};
	int16_t transpose{0};      ///< the master transpose, semitones
	int8_t cents{0};           ///< the master fine tune, −50..50
	dsp::fixed::FineTune tune; ///< of @ref cents
	int8_t unison_detune{8};   ///< cents across the unison parts
	uint8_t unison_spread{0};  ///< how far the parts spread across the field
	UnisonLayout unison;
	bool operator==(const Voicing&) const = default;
};

/// @brief One FM modulator's tuning and retrigger phase.
struct ModulatorSettings {
	int16_t transpose{0};        ///< semitones
	int8_t cents{0};             ///< −50..50
	dsp::fixed::FineTune tune;   ///< of @ref cents
	uint32_t retrigger_phase{0}; ///< the phase a note starts at, a fraction of 2^32; kFreeRunningPhase = free-running
	bool operator==(const ModulatorSettings&) const = default;
};

/// @brief What the engines read: the mode, oscillator sync, each oscillator's retrigger phase, and FM's modulators.
struct EngineSettings {
	SynthMode mode{SynthMode::SUBTRACTIVE};
	bool osc_sync{false};
	/// Each oscillator's retrigger phase, a fraction of 2^32; kFreeRunningPhase = free-running
	std::array<uint32_t, kNumSources> osc_retrigger_phase{kFreeRunningPhase, kFreeRunningPhase};
	std::array<ModulatorSettings, kNumModulators> modulators{
	    ModulatorSettings{}, ModulatorSettings{.transpose = -12, .cents = 0, .tune = {}, .retrigger_phase = 0}};
	bool modulator1_to_modulator0{false}; ///< FM: modulator 1 modulates modulator 0 rather than the carriers
	bool operator==(const EngineSettings&) const = default;
};

/// @brief One oscillator's playback settings, as its voices play them.
struct SourceConfig {
	OscType osc_type{OscType::SQUARE}; ///< what the oscillator plays
	int32_t transpose{0};              ///< semitones; a sample oscillator's ranges carry their own
	int32_t cents{0};                  ///< a sample oscillator's ranges carry their own
	dsp::fixed::FineTune tune;         ///< the cents as a phase-increment multiplier
	SampleRepeatMode repeat_mode{SampleRepeatMode::CUT};
	int32_t time_stretch_amount{0}; ///< a stretched sample's speed offset
	InterpolationMode interpolation_mode{InterpolationMode::LINEAR};
	bool pitch_and_speed_independent{false};
	bool reversed{false};
	bool operator==(const SourceConfig&) const = default;
};

/// @brief A sound's settings as its voices play them, derived from its state in one place (DelugeSynth::derive).
struct SoundConfig {
	Voicing voicing;
	EngineSettings engine;
	std::array<LfoState, LFO_COUNT>
	    lfos{}; ///< each LFO's wave and sync; LFO1 and LFO3 run per sound, 2 and 4 per voice
	std::array<SourceConfig, kNumSources> sources{};      ///< each oscillator's playback settings
	std::array<EnvelopeState, kNumEnvelopes> envelopes{}; ///< each envelope's mode, curve and follow input
	bool operator==(const SoundConfig&) const = default;
};

} // namespace deluge::synth
