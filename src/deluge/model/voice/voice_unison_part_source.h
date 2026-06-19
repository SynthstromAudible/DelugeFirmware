/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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
#include "dsp/dx/dx_voice_adapter.h"
#include "model/sample/sample.h"
#include "model/voice/sample_playback_adapter.h"
#include "processing/live/audio_input_adapter.h"
#include "synth/source/dx7_source.h"
#include "synth/source/input_source.h"
#include "synth/source/sample_source.h"
#include "synth/source/virtual_analog_source.h"
#include "synth/source/wavetable_source.h"
#include <optional>

class TimeStretcher;
class VoiceSamplePlaybackGuide;
class Source;
class Voice;

class VoiceUnisonPartSource {
public:
	VoiceUnisonPartSource() = default;
	bool noteOn(Voice* voice, Source* source, VoiceSamplePlaybackGuide* voiceSource, uint32_t samplesLate,
	            uint32_t oscPhase, bool resetEverything, SynthMode synthMode, uint8_t velocity);
	void unassign(bool deletingSong);

	uint32_t oscPos{}; // FKA phase. No longer used for Sample playback / rate conversion position. Only waves, including
	                   // wavetable. Retired for virtual-analog waves — their phase now lives in vaSource (below).
	// The persistent oscillator engine for this source, when its oscType is one the engines cover (mutually
	// exclusive). Each owns the phase across render blocks (replacing oscPos for those waves), built at note-on
	// and kept across notes for free-running oscillators (so neither is reset in unassign). vaSource: the six
	// virtual-analog waves (subtractive + ring-mod). wavetableSource: OscType::WAVETABLE. FM/NOISE/SAMPLE/INPUT
	// keep oscPos.
	std::optional<deluge::synth::VirtualAnalogSource> vaSource;
	std::optional<deluge::dsp::WavetableSource> wavetableSource;

	/// The source's current oscillator phase — from whichever engine owns it, else oscPos. (Read by osc-sync to
	/// seed the slave from the master at block start.)
	[[nodiscard]] uint32_t currentOscPhase() const {
		if (vaSource.has_value()) {
			return vaSource->phase();
		}
		if (wavetableSource.has_value()) {
			return wavetableSource->phase();
		}
		return oscPos;
	}
	/// Advance the source's oscillator phase by @p delta (a skipped osc-sync master still progresses).
	void advanceOscPhase(uint32_t delta) {
		if (vaSource.has_value()) {
			vaSource->seek(vaSource->phase() + delta);
		}
		else if (wavetableSource.has_value()) {
			wavetableSource->seek(wavetableSource->phase() + delta);
		}
		else {
			oscPos += delta;
		}
	}

	uint32_t phaseIncrementStoredValue{};
	int32_t carrierFeedback{};
	bool active{};
	SamplePlaybackAdapter
	    sampleVoiceAdapter; // the firmware's VoiceSample pool + reader behind synth::SamplePlaybackPort
	SampleSource sampleSource{&sampleVoiceAdapter}; // the SAMPLE source (drives the reader through the port).
	deluge::dsp::DxVoiceAdapter dxVoiceAdapter; // the firmware's MSFA DxVoice-pool adapter behind synth::DxVoicePort
	deluge::dsp::DX7Source dx7Source{&dxVoiceAdapter}; // the DX7 source (forwards FM to the adapter port).
	AudioInputAdapter audioInputAdapter;         // the firmware's AudioEngine-backed live-input port (ring/clock/pool).
	InputSource inputSource{&audioInputAdapter}; // the INPUT_L/R/STEREO source (reads input through the port).
};
