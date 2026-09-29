/*
 * Copyright © 2014-2023 Synthstrom Audible Limited, 2026 Synthstrom Audible Limited
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

#include "model/voice/ringmod_engine.h"
#include "dsp-next/core/types.h"                 // deluge::dsp::fixed::Buffer
#include "dsp/oscillators/oscillator.h"          // deluge::dsp::oscSyncRenderingBuffer
#include "model/voice/source_component_render.h" // voice::makeBlockControls
#include "model/voice/voice.h"                   // Voice (member access — pre-roll + per-source state)
#include "model/voice/voice_unison_part_source.h"
#include "modulation/params/param.h"         // LOCAL_OSC_A_PITCH_ADJUST / LOCAL_OSC_A_PHASE_WIDTH
#include "storage/audio/audio_file_holder.h" // AudioFileHolder::audioFile (the wavetable lookup)
#include "storage/wave_table/wave_table.h"   // WaveTable::view() (band-data view for the wavetable source)
#include "util/fixedpoint.h"
#include "util/functions.h" // shouldDoPanning

using namespace deluge;

namespace params = deluge::modulation::params;

void RingModEngine::render(Voice& voice, int32_t* oscBuffer, int32_t numSamples, bool doOscSync, uint32_t* oscSyncPos,
                           int32_t overallPitchAdjust, const int32_t* sourceWaveIndexIncrements, bool stereoUnison,
                           int32_t filterGain, bool& didStereoTempBuffer, int32_t* scratch0, int32_t* scratch1,
                           const VoiceConfig& cfg) {

	if (stereoUnison) {
		// oscBuffer is always a stereo temp buffer
		didStereoTempBuffer = true;
	}

	// For each unison part
	for (int32_t u = 0; u < cfg.numUnison; u++) {

		int32_t unisonAmplitudeL, unisonAmplitudeR;
		shouldDoPanning((stereoUnison ? cfg.unisonPan[u] : 0), &unisonAmplitudeL, &unisonAmplitudeR);

		// Work out the phase increments of the two sources. If these are too high, we abandon all remaining unison
		// parts. Yes this will affect all unison parts, which seems like it's not what we want, but since we're
		// traversing the unison parts in ascending frequency, it's fine!

		uint32_t phaseIncrements[kNumSources];
		for (int32_t s = 0; s < kNumSources; s++) {
			phaseIncrements[s] = voice.unisonParts[u].sources[s].phaseIncrementStoredValue;
		}

		// If overall pitch adjusted...
		if (overallPitchAdjust != kMaxSampleValue) {
			for (int32_t s = 0; s < kNumSources; s++) {
				if (!Voice::adjustPitch(phaseIncrements[s], overallPitchAdjust)) {
					return; // freq too high — all remaining (higher) unison parts are too high too
				}
			}
		}

		// If individual source pitch adjusted...
		for (int32_t s = 0; s < kNumSources; s++) {
			if (!Voice::adjustPitch(phaseIncrements[s],
			                        voice.params.paramFinalValues[params::LOCAL_OSC_A_PITCH_ADJUST + s])) {
				return; // freq too high — abandon the rest of the unison spread
			}
		}

		// Build the per-source render context; renderUnisonPart owns the cross-source ring-mod relationship
		// (render both sources raw with source 0 the hard-sync master, then multiply + pan).
		RingModEngine::Source ringModSources[kNumSources];
		for (int32_t s = 0; s < kNumSources; s++) {
			// Work out pulse width, from parameter. This has no effect if we're not actually using square
			// waves, but just do it anyway, it's a simple calculation
			int32_t pulseWidth =
			    (uint32_t)lshiftAndSaturate<1>(voice.params.paramFinalValues[params::LOCAL_OSC_A_PHASE_WIDTH + s]);

			OscType oscType = cfg.oscType[s];

			// Non-wavetable sources have no audioFileHolder; only OscType::WAVETABLE reads the table.
			WaveTable* waveTable = (oscType == OscType::WAVETABLE)
			                           ? static_cast<WaveTable*>(voice.guides[s].audioFileHolder->audioFile)
			                           : nullptr;

			ringModSources[s] = RingModEngine::Source{
			    .part = &voice.unisonParts[u].sources[s],
			    .phaseIncrement = phaseIncrements[s],
			    .pulseWidth = pulseWidth,
			    .waveTable = waveTable,
			    .waveIndexLast = voice.sourceWaveIndexesLastTime[s],
			    .waveIndexIncrement = sourceWaveIndexIncrements[s],
			    .oscType = oscType,
			    .retriggerPhase = cfg.oscRetriggerPhase[s],
			};
		}

		renderUnisonPart(ringModSources[0], ringModSources[1], doOscSync, oscSyncPos[u], cfg.hasFilters, filterGain,
		                 unisonAmplitudeL, unisonAmplitudeR, stereoUnison, oscBuffer, numSamples, scratch0, scratch1);
	}
}

void RingModEngine::renderUnisonPart(const Source& src0, const Source& src1, bool doOscSync, uint32_t oscSyncPos,
                                     bool hasFilters, int32_t filterGain, int32_t unisonAmplitudeL,
                                     int32_t unisonAmplitudeR, bool stereoUnison, int32_t* oscBuffer,
                                     int32_t numSamples, int32_t* scratch0, int32_t* scratch1) {

	int32_t amplitudeForRingMod = 1 << 27;

	if (hasFilters) {
		amplitudeForRingMod = multiply_32x32_rshift32_rounded(amplitudeForRingMod, filterGain) << 4;
	}

	const Source* sources[2] = {&src0, &src1};
	int32_t* scratch[2] = {scratch0, scratch1};

	for (int32_t s = 0; s < 2; s++) {
		// Source 0 can never be the synced (slave) oscillator — it is the master.
		bool doingOscSyncThisOscillator = (s == 0) ? false : doOscSync;
		const Source& source = *sources[s];

		// Both ring-moddable wave families now render through their persistent per-source engine (VA or
		// wavetable) via the engine's public renderInto. Ring-mod renders the raw wave: applyAmplitude=false,
		// amplitude 0. The hard-sync master is source 0 (phase oscSyncPos, increment src0.phaseIncrement).
		const synth::BlockControls controls =
		    voice::makeBlockControls(0, 0, source.pulseWidth, /*applyAmplitude=*/false, doingOscSyncThisOscillator,
		                             oscSyncPos, src0.phaseIncrement, source.retriggerPhase);
		const dsp::fixed::Buffer out{scratch[s], static_cast<size_t>(numSamples)};
		const dsp::fixed::Buffer syncScratch{dsp::oscSyncRenderingBuffer, static_cast<size_t>(numSamples)};
		if (source.part->vaSource.has_value()) {
			source.part->vaSource->renderInto(controls, source.phaseIncrement, out, syncScratch);
		}
		else {
			source.part->wavetableSource->renderInto(controls, source.waveTable->view(), source.waveIndexLast,
			                                         source.waveIndexIncrement, source.phaseIncrement, out,
			                                         syncScratch);
		}

		// Sine and triangle waves come out bigger in fixed-amplitude rendering (for arbitrary reasons), so we
		// need to compensate
		if (source.oscType == OscType::SAW || source.oscType == OscType::ANALOG_SAW_2) {
			amplitudeForRingMod <<= 1;
		}
		else if (source.oscType == OscType::WAVETABLE) {
			amplitudeForRingMod <<= 2;
		}
	}

	int32_t* __restrict__ output = oscBuffer;
	int32_t* __restrict__ input0 = scratch0;
	int32_t* __restrict__ input1 = scratch1;

	if (stereoUnison) {
		int32_t const* const oscBufferEnd = oscBuffer + 2 * numSamples;
		do {
			int32_t out =
			    multiply_32x32_rshift32_rounded(multiply_32x32_rshift32(*input0, *input1), amplitudeForRingMod);
			*output++ += multiply_32x32_rshift32(out, unisonAmplitudeL) << 2;
			*output++ += multiply_32x32_rshift32(out, unisonAmplitudeR) << 2;
			input0++;
			input1++;
		} while (output != oscBufferEnd);
	}
	else {
		int32_t const* const oscBufferEnd = oscBuffer + numSamples;
		do {
			*output += multiply_32x32_rshift32_rounded(multiply_32x32_rshift32(*input0, *input1), amplitudeForRingMod);
			input0++;
			input1++;
		} while (++output != oscBufferEnd);
	}
}
