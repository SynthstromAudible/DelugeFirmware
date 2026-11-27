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

#include "definitions_cxx.hpp"
#include "instruments/classic/voice_pool.h"
#include "model/voice/sample_playback_adapter.h"
#include "model/voice/voice_sample_playback_guide.h"
#include "processing/dx7/dx_voice_adapter.h"
#include "processing/live/audio_input_adapter.h"
#include "synth/voice/voice.h"
#include <array>
#include <cstdint>

/// @brief The firmware side of one unison part's source: the ports its sources play through.
struct UnisonPartPorts {
	SamplePlaybackAdapter sample; ///< the VoiceSample pool and reader behind synth::SamplePlaybackPort
	AudioInputAdapter input;      ///< the AudioEngine-backed live input behind synth::AudioInputPort
};

/// @brief The host's part of a sound's voice: the ports its sources play through and the playback guides of the files
/// its note plays. Declared ahead of the kernel in the slot, whose sources point into the ports.
struct VoicePart {
	std::array<std::array<UnisonPartPorts, kNumSources>, kMaxNumVoicesUnison> ports_;
	/// Each unison part's DX7: the pooled dx7::Voice behind synth::DxVoicePort, which the DX7 engine plays.
	std::array<deluge::dsp::DxVoiceAdapter, kMaxNumVoicesUnison> dx7_ports_;
	/// Each source's playback bounds and file for the note playing (basically sample memory bounds).
	std::array<VoiceSamplePlaybackGuide, kNumSources> guides{};

	/// @brief The ports unison part @p u's source @p s plays through.
	/// @return The ports.
	[[nodiscard]] UnisonPartPorts& ports(int32_t u, int32_t s) { return ports_[u][s]; }

	/// @brief The DX7 port unison part @p u plays through.
	/// @return The port.
	[[nodiscard]] deluge::dsp::DxVoiceAdapter& dx7_port(int32_t u) { return dx7_ports_[u]; }
};

/// @brief A sound's voice: the library slot over the host's part, and nothing more. A type of its own rather than an
/// alias so that headers can declare it without the kernel's.
struct SoundVoice final : deluge::instruments::classic::VoiceSlot<VoicePart, deluge::synth::Voice> {};
