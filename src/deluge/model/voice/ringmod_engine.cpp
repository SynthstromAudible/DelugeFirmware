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
#include "model/voice/voice_unison_part_source.h"
#include "util/fixedpoint.h"

using namespace deluge;

void RingModEngine::render(const Source& src0, const Source& src1, bool doOscSync, uint32_t oscSyncPos, bool hasFilters,
                           int32_t filterGain, int32_t unisonAmplitudeL, int32_t unisonAmplitudeR, bool stereoUnison,
                           int32_t* oscBuffer, int32_t numSamples, int32_t* scratch0, int32_t* scratch1) {

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
		if (source.part->vaEngine.has_value()) {
			source.part->vaEngine->renderInto(controls, source.phaseIncrement, out, syncScratch);
		}
		else {
			source.part->wavetableEngine->renderInto(controls, source.waveTable, source.waveIndexLast,
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
