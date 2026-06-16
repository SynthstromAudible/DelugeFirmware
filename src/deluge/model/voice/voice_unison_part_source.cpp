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

#include "model/voice/voice_unison_part_source.h"
#include "dsp-next/core/phasor.h"
#include "dsp/dx/dx7note.h"
#include "dsp/dx/engine.h"
#include "dsp/oscillators/oscillator.h" // isVirtualAnalog / vaWaveFor
#include "memory/general_memory_allocator.h"
#include "model/sample/sample_cache.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "playback/playback_handler.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"

bool VoiceUnisonPartSource::noteOn(Voice* voice, Source* source, VoiceSamplePlaybackGuide* guide, uint32_t samplesLate,
                                   uint32_t oscRetriggerPhase, bool resetEverything, SynthMode synthMode,
                                   uint8_t velocity) {

	if (synthMode != SynthMode::FM && source->oscType == OscType::SAMPLE) {
		return sampleEngine.noteOn(guide, samplesLate, voice->getPriorityRating());
	}

	if (synthMode != SynthMode::FM
	    && (source->oscType == OscType::SAMPLE || source->oscType == OscType::INPUT_L
	        || source->oscType == OscType::INPUT_R || source->oscType == OscType::INPUT_STEREO)) {
		// oscPos = 0;
	}
	else if (synthMode != SynthMode::FM && source->oscType == OscType::DX7) [[unlikely]] {
		DxPatch* patch = source->ensureDxPatch();
		if (!dx7Engine.noteOn(*patch, voice->noteCodeAfterArpeggiation, velocity)) {
			return false;
		}
	}
	else {
		const OscType oscType = source->oscType;
		// Virtual-analog + wavetable waves (subtractive + ring-mod) hold their phase in a persistent engine rather
		// than oscPos. Build/reseed it following the legacy oscPos rule: a retrigger seeds init+retrigger; a
		// free-run oscillator on a new voice takes the random phase randomizeOscPhases left in oscPos; a free-run
		// oscillator on a reused voice (same Sound ⇒ same wave) keeps the engine's continuing phase. The two
		// engines are mutually exclusive — reset the other (a pooled voice slot may carry one from a prior sound).
		const bool engineMode = (synthMode == SynthMode::SUBTRACTIVE || synthMode == SynthMode::RINGMOD);
		const uint32_t seedPhase = getOscInitialPhaseForZero(oscType) + oscRetriggerPhase;
		if (engineMode && deluge::dsp::isVirtualAnalog(oscType)) {
			wavetableEngine.reset();
			const deluge::synth::va_shape::Wave wave = deluge::dsp::vaWaveFor(oscType);
			if (oscRetriggerPhase != 0xFFFFFFFF) {
				vaEngine.emplace(wave, deluge::dsp::Phasor{seedPhase, 0});
			}
			else if (resetEverything || !vaEngine.has_value()) {
				vaEngine.emplace(wave, deluge::dsp::Phasor{oscPos, 0});
			}
			// else (free-run, reused voice): keep vaEngine's phase.
		}
		else if (engineMode && oscType == OscType::WAVETABLE) {
			vaEngine.reset();
			if (oscRetriggerPhase != 0xFFFFFFFF) {
				wavetableEngine.emplace(deluge::dsp::Phasor{seedPhase, 0});
			}
			else if (resetEverything || !wavetableEngine.has_value()) {
				wavetableEngine.emplace(deluge::dsp::Phasor{oscPos, 0});
			}
			// else (free-run, reused voice): keep wavetableEngine's phase.
		}
		else {
			vaEngine.reset(); // non-engine (noise/FM/…) keeps oscPos; drop any engine left from a prior note.
			wavetableEngine.reset();
			if (oscRetriggerPhase != 0xFFFFFFFF) {
				oscPos = seedPhase;
			}
		}
	}

	if (resetEverything) {
		carrierFeedback = 0;
	}

	return true;
}

void VoiceUnisonPartSource::unassign(bool deletingSong) {
	active = false;
	sampleEngine.unassign(deletingSong);
	dx7Engine.unassign();
	inputEngine.unassign();
}
