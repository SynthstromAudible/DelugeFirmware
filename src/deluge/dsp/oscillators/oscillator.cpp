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

	bool doPulseWave = false;

	// The standard band number for non-sine/triangle waves — used to decide whether to drop to the
	// crude (aliasing) calculation and for the analog-saw downgrade. The phase increment used for
	// it (`phaseIncrementForCalculations`) is also what the square-pulse path selects its band by,
	// so it lives at function scope.
	int32_t tableNumber;
	uint32_t phaseIncrementForCalculations = phaseIncrement;

	if (type == OscType::SINE) {
		retriggerPhase += 3221225472u;
	}

	else if (type != OscType::TRIANGLE) [[likely]] { // Not sines and not triangles
		// PW for the perfect mathematical/digital square - we'll do it by multiplying two squares
		if (type == OscType::SQUARE) {
			doPulseWave = (pulseWidth != 0);
			pulseWidth += 2147483648u;
			if (doPulseWave) {
				// Mildly band limit the square waves before they get ringmodded to create the
				// pulse wave. *0.5 would be no band limiting
				phaseIncrementForCalculations = phaseIncrement * 0.6;
			}
		}

		tableNumber = fixed::getTableNumber(phaseIncrementForCalculations).first;
		// TODO: that should really take into account the phaseIncrement (pitch) after it's potentially been altered for
		// non-square PW below.

		if (type == OscType::ANALOG_SAW_2) {
			// Analog saw tables 8 and above are quite saw-shaped and sound relatively similar to the digital saw. So
			// for these, if the CPU load is getting dire, we can do the crude, aliasing digital saw.
			if (tableNumber >= 8 && tableNumber < AudioEngine::cpuDireness + 6) {
				type = OscType::SAW;
			}
		}

		else if (type == OscType::SAW) {
			retriggerPhase += 2147483648u; // This is the normal case, when CPU usage is *not* dire.
		}
	}

	if (type != OscType::SQUARE) {
		// PW for oscillators other than the perfect mathematical square
		doPulseWave = (pulseWidth && !doOscSync);
		if (doPulseWave) {

			doOscSync = true;

			uint32_t pulseWidthAbsolute = ((int32_t)pulseWidth >= 0) ? pulseWidth : -pulseWidth;

			resetterPhase = phase;
			resetterPhaseIncrement = phaseIncrement;

			if (type == OscType::ANALOG_SQUARE) {

				int64_t resetterPhaseToDivide = (uint64_t)resetterPhase << 30;

				if ((uint32_t)(resetterPhase) >= (uint32_t)-(resetterPhaseIncrement >> 1)) {
					resetterPhaseToDivide -= (uint64_t)1 << 62;
				}

				phase = resetterPhaseToDivide / (int32_t)((pulseWidthAbsolute + 2147483648u) >> 1);
				phaseIncrement = ((uint64_t)phaseIncrement << 31) / (pulseWidthAbsolute + 2147483648u);
			}

			else {
				if (type == OscType::SAW) {
					resetterPhase += 2147483648u;
				}
				else if (type == OscType::SINE) {
					resetterPhase -= 3221225472u;
				}

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
			}

			phase += retriggerPhase;
		}
	}

	if (type == OscType::WAVETABLE) {

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

	// Every remaining (virtual-analog) wave is rendered by the L1 engine. The adapter has finalised the control
	// values above (pulse-as-sync, retrigger, analog-saw downgrade) — that derivation is shared with the
	// wavetable path, so it stays here — and just hands the engine the already-modulated values. Phase
	// persistence also stays here: the engine exposes its post-render phase, which on hard-sync blocks we store
	// exactly as the legacy path did via maybeStorePhase (non-sync blocks keep the advance done at the top).
	synth::VirtualAnalogEngine engine{type, Phasor{phase, phaseIncrement}};
	synth::BlockControls controls{
	    .amplitude = Ramp{amplitude, amplitudeIncrement},
	    .apply_amplitude = applyAmplitude,
	    .pulse_width = pulseWidth,
	    .pulse_wave = doPulseWave,
	    .retrigger_phase = retriggerPhase,
	    .sync = doOscSync ? std::optional<Phasor>{Phasor{resetterPhase, resetterPhaseIncrement}} : std::nullopt,
	    .bandlimit_increment = phaseIncrementForCalculations,
	    .cpu_direness = AudioEngine::cpuDireness,
	};
	engine.renderBlock(controls, fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)},
	                   fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
	if (doOscSync) {
		maybeStorePhase(type, startPhase, engine.phase(), doPulseWave);
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
