/*
 * Copyright © 2017-2023 Synthstrom Audible Limited, 2025 Mark Adams
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
#include "oscillator.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/core/ramp.h"
#include "dsp-next/fixed/bandlimited/band_limited_table.h" // getTableNumber
#include "dsp-next/fixed/vca/amplitude.h"
#include "processing/engines/audio_engine.h"
#include "storage/wave_table/wave_table.h"
#include "synth/source/virtual_analog_engine.h" // the L1 VA engine + BlockControls
#include "util/fixedpoint.h"
#include "wavetable_oscillator.h"
#include <optional>

namespace deluge::dsp {
PLACE_INTERNAL_FRUNK int32_t oscSyncRenderingBuffer[SSI_TX_BUFFER_NUM_SAMPLES + 4]
    __attribute__((aligned(CACHE_LINE_SIZE)));

bool isVirtualAnalog(OscType type) {
	// The six VA waves are the first OscType entries, up to ANALOG_SAW_2; WAVETABLE and above are not.
	return util::to_underlying(type) <= util::to_underlying(OscType::ANALOG_SAW_2);
}

// The app's open OscType → the VA engine's closed wave set.
synth::va_shape::Wave vaWaveFor(OscType type) {
	switch (type) {
	case OscType::TRIANGLE:
		return synth::va_shape::Wave::Triangle;
	case OscType::SQUARE:
		return synth::va_shape::Wave::Square;
	case OscType::SAW:
		return synth::va_shape::Wave::Saw;
	case OscType::ANALOG_SAW_2:
		return synth::va_shape::Wave::AnalogSaw;
	case OscType::ANALOG_SQUARE:
		return synth::va_shape::Wave::AnalogSquare;
	default: // SINE
		return synth::va_shape::Wave::Sine;
	}
}
__attribute__((optimize("unroll-loops"))) void
Oscillator::renderOsc(OscType type, int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                      uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase, bool applyAmplitude,
                      int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase,
                      uint32_t resetterPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement,
                      int sourceWaveIndexLastTime, WaveTable* waveTable) {

	// We save a decent bit of processing power by grabbing a local copy of the phase to work with, and just
	// incrementing the startPhase once
	uint32_t phase = *startPhase;
	*startPhase += phaseIncrement * numSamples;

	if (type == OscType::WAVETABLE) {
		// The wavetable engine still lives app-side (it depends on storage/), so the shared pulse-width-as-sync
		// preamble stays here for it. The very same derivation now also lives in the VA shapes; the two copies
		// converge when the wavetable becomes a SourceEngine. Wavetable is never SINE/SAW/SQUARE, so it takes
		// none of the per-wave retrigger/resetter tweaks — only the plain multiply rewrite.
		bool doPulseWave = (pulseWidth && !doOscSync);
		if (doPulseWave) {
			doOscSync = true;

			uint32_t pulseWidthAbsolute = ((int32_t)pulseWidth >= 0) ? pulseWidth : -pulseWidth;
			resetterPhase = phase;
			resetterPhaseIncrement = phaseIncrement;

			int32_t resetterPhaseToMultiply = resetterPhase >> 1;
			if ((uint32_t)(resetterPhase) >= (uint32_t)-(resetterPhaseIncrement >> 1)) {
				resetterPhaseToMultiply -= ((uint32_t)1 << 31); // Count the last little bit of the cycle as
				                                                // actually a negative-number bit of the next one.
			}

			phase = (uint32_t)multiply_32x32_rshift32_rounded((pulseWidthAbsolute >> 1) + 1073741824,
			                                                  resetterPhaseToMultiply)
			        << 3;
			phaseIncrement = ((uint32_t)multiply_32x32_rshift32_rounded((pulseWidthAbsolute >> 1) + 1073741824,
			                                                            (uint32_t)phaseIncrement >> 1)
			                  << 3);
			phase += retriggerPhase;
		}

		int32_t waveIndex = sourceWaveIndexLastTime + 1073741824;

		int32_t* wavetableRenderingBuffer = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;

		// The sample-backed wavetable engine, behind the dsp-next block-generator interface (it depends on
		// storage/, so it stays app-side rather than becoming a synth/source SourceEngine for now).
		WavetableOscillator osc{waveTable,         Phasor{phase, phaseIncrement},
		                        doOscSync,         Phasor{resetterPhase, resetterPhaseIncrement},
		                        retriggerPhase,    waveIndex,
		                        waveIndexIncrement};
		osc.renderBlock(fixed::Buffer{wavetableRenderingBuffer, static_cast<size_t>(numSamples)});
		phase = osc.phase();

		amplitude <<= 3;
		amplitudeIncrement <<= 3;
		if (applyAmplitude) { // change from original code but if we don't render into the osc sync buffer this is just
			                  // a very long useless operation
			applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart,
			                             oscSyncRenderingBuffer);
		}
		maybeStorePhase(type, startPhase, phase, doPulseWave);
		return;
	}

	// Every remaining (virtual-analog) wave is rendered by the L1 engine, which now owns its full per-block
	// derivation: the adapter hands it the *original* (unrewritten) phase + increment and the *raw* controls
	// (raw pulse width, real osc sync only, raw retrigger), and each shape applies its own pulse-as-sync /
	// retrigger / band-select / analog-saw downgrade internally. Phase persistence: the original-domain advance
	// above is what carries over, except on a real hard-sync block, where the engine's post-render phase (the
	// synced oscillator's advanced phase) replaces it — exactly the legacy maybeStorePhase rule, which for any
	// real-sync block always stores (pulse-as-sync, the only case it skipped, can't co-occur with real sync).
	synth::VirtualAnalogEngine engine{vaWaveFor(type), Phasor{phase, phaseIncrement}};
	synth::BlockControls controls{
	    .amplitude = Ramp{amplitude, amplitudeIncrement},
	    .apply_amplitude = applyAmplitude,
	    .pulse_width = pulseWidth,
	    .retrigger_phase = retriggerPhase,
	    .sync = doOscSync ? std::optional<Phasor>{Phasor{resetterPhase, resetterPhaseIncrement}} : std::nullopt,
	    .cpu_direness = AudioEngine::cpuDireness,
	};
	engine.renderBlock(controls, fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)},
	                   fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
	if (doOscSync) {
		*startPhase = engine.phase();
	}
}
void Oscillator::applyAmplitudeVectorToBuffer(int32_t amplitude, int32_t numSamples, int32_t amplitudeIncrement,
                                              int32_t* outputBufferPos, int32_t* inputBuferPos) {
	// Delegates to the dsp-next Amplitude mixer: out = (raw * amplitude) + mix, with the
	// same vqdmulhq + accumulate and the same ramped amplitude vector — the VCA arithmetic
	// now lives in one component. Bit-exact with the former inline loop (the masked tail
	// also drops that loop's harmless 4-sample overshoot). Call sites switch to the mixer
	// directly in a later step, retiring this shim.
	fixed::Amplitude vca{amplitude, amplitudeIncrement};
	vca.renderBlock(fixed::Signal{inputBuferPos, static_cast<size_t>(numSamples)},
	                fixed::Signal{outputBufferPos, static_cast<size_t>(numSamples)},
	                fixed::Buffer{outputBufferPos, static_cast<size_t>(numSamples)});
}
void Oscillator::maybeStorePhase(const OscType& type, uint32_t* startPhase, uint32_t phase, bool doPulseWave) {
	if (!(doPulseWave && type != OscType::SQUARE)) {
		*startPhase = phase;
	}
}

} // namespace deluge::dsp
