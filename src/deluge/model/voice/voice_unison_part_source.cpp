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
#include "memory/general_memory_allocator.h"
#include "model/sample/sample_cache.h"
#include "model/song/song.h"
#include "model/voice/voice.h"
#include "model/voice/voice_sample.h"
#include "playback/playback_handler.h"
#include "processing/dx7/dx_engine.h"
#include "processing/dx7/dx_patch.h"
#include "processing/engines/audio_engine.h"
#include "processing/source.h"
#include "storage/multi_range/multisample_range.h"
#include "synth/sources/va/va_shapes.h" // va_shape::is_virtual_analog / wave_for / initial_phase_for_zero
#include "util/functions.h"             // getNoise

VoiceUnisonPartSource::~VoiceUnisonPartSource() {
	unassign(false);
}

bool VoiceUnisonPartSource::noteOn(const deluge::synth::SourceSetup& source, uint32_t samplesLate,
                                   uint32_t oscRetriggerPhase, bool resetEverything, SynthMode synthMode,
                                   int32_t noteCode, uint8_t velocity, int32_t priorityRating) {

	if (synthMode != SynthMode::FM && source.osc_type == OscType::SAMPLE) {
		return sampleSource.port()->note_on(samplesLate, priorityRating);
	}

	if (synthMode != SynthMode::FM
	    && (source.osc_type == OscType::INPUT_L || source.osc_type == OscType::INPUT_R
	        || source.osc_type == OscType::INPUT_STEREO)) {
		inputSource.prepareForNoteOn(source.osc_type); // latch the live-input channel
	}
	else if (synthMode != SynthMode::FM && source.osc_type == OscType::DX7) [[unlikely]] {
		if (!dx7Source.noteOn(source.dx_patch, noteCode, velocity)) { // opaque patch handle → the port
			return false;
		}
	}
	else {
		const OscType oscType = source.osc_type;
		// Virtual-analog + wavetable waves (subtractive + ring-mod) hold their phase in a persistent engine rather
		// than oscPos. Build/reseed it following the legacy oscPos rule: a retrigger seeds init+retrigger; a
		// free-run oscillator on a new voice takes the random phase randomizeOscPhases left in oscPos; a free-run
		// oscillator on a reused voice (same Sound ⇒ same wave) keeps the engine's continuing phase. The two
		// engines are mutually exclusive — reset the other (a pooled voice slot may carry one from a prior sound).
		const bool engineMode = (synthMode == SynthMode::SUBTRACTIVE || synthMode == SynthMode::RINGMOD);
		const uint32_t seedPhase = deluge::synth::va_shape::initial_phase_for_zero(oscType) + oscRetriggerPhase;
		if (engineMode && deluge::synth::va_shape::is_virtual_analog(oscType)) {
			wavetableSource.reset();
			supersawSource.reset();
			const deluge::synth::va_shape::Wave wave = deluge::synth::va_shape::wave_for(oscType);
			if (oscRetriggerPhase != 0xFFFFFFFF) {
				vaSource.emplace(wave, deluge::dsp::Phasor{seedPhase, 0});
			}
			else if (resetEverything || !vaSource.has_value()) {
				vaSource.emplace(wave, deluge::dsp::Phasor{oscPos, 0});
			}
			// else (free-run, reused voice): keep vaSource's phase.
		}
		else if (engineMode && oscType == OscType::WAVETABLE) {
			vaSource.reset();
			supersawSource.reset();
			if (oscRetriggerPhase != 0xFFFFFFFF) {
				wavetableSource.emplace(deluge::dsp::Phasor{seedPhase, 0});
			}
			else if (resetEverything || !wavetableSource.has_value()) {
				wavetableSource.emplace(deluge::dsp::Phasor{oscPos, 0});
			}
			// else (free-run, reused voice): keep wavetableSource's phase.
		}
		else if (engineMode && oscType == OscType::SUPERSAW) {
			vaSource.reset();
			wavetableSource.reset();
			using deluge::dsp::fixed::Supersaw;
			if (oscRetriggerPhase != 0xFFFFFFFF) {
				// The side saws start at fixed offsets from the centre one, so every note starts the same way
				supersawSource.emplace(seedPhase, Supersaw::side_phases_from(seedPhase));
			}
			else if (resetEverything || !supersawSource.has_value()) {
				// Free-running on a new voice: random side saws like the centre one, otherwise every note's side saws
				// would start from the same place and sweep the same way
				Supersaw::SidePhases sidePhases;
				for (uint32_t& phase : sidePhases) {
					phase = getNoise();
				}
				supersawSource.emplace(oscPos, sidePhases);
			}
			// else (free-run, reused voice): keep supersawSource's phases.
		}
		else {
			vaSource.reset(); // non-engine (noise/FM/…) keeps oscPos; drop any engine left from a prior note.
			wavetableSource.reset();
			supersawSource.reset();
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
	sampleVoiceAdapter.unassign(deletingSong);
	dx7Source.unassign();
	inputSource.unassign();
}
