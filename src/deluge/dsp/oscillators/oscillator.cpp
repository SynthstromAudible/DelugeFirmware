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
#include "dsp-next/fixed/amplitude.h"
#include "dsp-next/fixed/analog_saw.h"
#include "dsp-next/fixed/analog_square.h"
#include "dsp-next/fixed/band_limited_table.h"
#include "dsp-next/fixed/band_limited_table_oscillator.h"
#include "dsp-next/fixed/crude_amplitude.h"
#include "dsp-next/fixed/crude_oscillators.h"
#include "dsp-next/fixed/pulse_oscillator.h"
#include "dsp-next/fixed/resetter.h"
#include "dsp-next/fixed/saw.h"
#include "dsp-next/fixed/square.h"
#include "dsp-next/fixed/synced_oscillator.h"
#include "dsp-next/fixed/synced_phasor.h"
#include "dsp-next/fixed/table_oscillator.h"
#include "dsp-next/fixed/triangle.h"
#include "processing/engines/audio_engine.h"
#include "storage/wave_table/wave_table.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include "wavetable_oscillator.h"

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

	// The band-limited wave selected for this voice (one of the kernel BandlimitedTables); the
	// band itself is picked by pitch inside the BandLimitedTableOscillator.
	const fixed::BandlimitedTable* bands = nullptr;

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

	// Each sync path (the SyncedOscillator/SyncedPhasor components and the wavetable adapter) now
	// derives the resetter reciprocal itself from the resetter increment, so there is nothing to
	// set up here — `doOscSync` is just threaded through to the render below.

	// How an oscillator's un-amplified ("standalone") output relates to the raw value its VCA
	// consumes: band-limited / pulse waves emit final-scale directly; the crude saw/square emit
	// double (so the standalone is >> 1); the crude triangle emits half (standalone << 1).
	enum class Standalone { kDirect, kHalve, kDouble };

	// The shared render tail for one oscillator: fill its raw wave, then either scale-and-mix it
	// through its VCA (when amplitude applies) or write the standalone wave (scaled) straight out.
	// Collapses the otherwise-identical per-wave amp/no-amp blocks. `osc` already owns the right
	// phase source (plain Phasor or hard-synced); `makeVca` builds the VCA only when needed.
	auto renderThroughVca = [&](auto& osc, auto makeVca, Standalone scale) {
		if (applyAmplitude) {
			osc.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
			auto vca = makeVca();
			vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
			                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
			                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
		}
		else {
			osc.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
			if (scale == Standalone::kHalve) {
				for (int32_t* sample = bufferStart; sample != bufferEnd; ++sample) {
					*sample >>= 1;
				}
			}
			else if (scale == Standalone::kDouble) {
				for (int32_t* sample = bufferStart; sample != bufferEnd; ++sample) {
					*sample <<= 1;
				}
			}
		}
	};

	// Shared tail (Phase A1): render a band-limited wave — a BandLimitedTableOscillator picks the
	// right band of `bands` for this pitch — straight or with osc sync, then apply amplitude.
	// The per-wave call sites vary only which kernel's BandlimitedTable and the amplitude pair.
	// `selectionIncrement` is the pitch that picks the band; it equals `phaseIncrement` except for
	// pulse-width waves, where `phaseIncrement` has been rewritten to the (faster) pulse render rate
	// while the band-limiting must still target the original note (the legacy `getTableNumber`
	// argument, `phaseIncrementForCalculations`).
	auto renderFromTable = [&](const fixed::BandlimitedTable& bands, int32_t amp, int32_t ampIncrement,
	                           uint32_t selectionIncrement) {
		if (doOscSync) {
			// Hard sync: a SyncedOscillator wraps the band-limited oscillator, restarting it at
			// every resetter crossover and crossfading the seam. It fills the raw wave (into the
			// sync scratch when amplitude applies), then the VCA scales-and-mixes it.
			int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
			fixed::SyncedOscillator osc{
			    fixed::BandLimitedTableOscillator{bands, phase, phaseIncrement, selectionIncrement}, phase,
			    phaseIncrement, fixed::Resetter{resetterPhase, resetterPhaseIncrement}, retriggerPhase};
			osc.renderBlock(fixed::Buffer{bufferStartThisSync, static_cast<size_t>(numSamples)});
			phase = osc.phase();
			applyAmplitudeVectorToBuffer(amp, numSamples, ampIncrement, bufferStart, oscSyncRenderingBuffer);
			maybeStorePhase(type, startPhase, phase, doPulseWave);
		}
		else {
			// Generate the raw band-limited wave, then (when amplitude is applied) scale-and-mix it
			// with the Amplitude VCA. The osc's owned phase is fill-only; the persistent per-voice
			// phase was advanced at the top of renderOsc.
			fixed::BandLimitedTableOscillator osc{bands, phase, phaseIncrement, selectionIncrement};
			renderThroughVca(osc, [&] { return fixed::Amplitude{amp, ampIncrement}; }, Standalone::kDirect);
		}
	};

	if (type == OscType::SINE) [[unlikely]] {
		// Sine has no aliasing — a single band (the small sine table) covers every pitch.
		static const fixed::Band kSineBand[] = {{sineWaveSmall, 8, 0xFFFFFFFFu}};
		static const fixed::BandlimitedTable kSineTable{kSineBand};
		renderFromTable(kSineTable, amplitude, amplitudeIncrement, phaseIncrement);
		return;
	}

	else if (type == OscType::WAVETABLE) {

		int32_t waveIndex = sourceWaveIndexLastTime + 1073741824;

		int32_t* wavetableRenderingBuffer = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;

		// The sample-backed wavetable engine, behind the dsp-next block-generator interface (it
		// depends on storage/, so it adapts app-side rather than living in the dsp-next library).
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

	else if (type == OscType::TRIANGLE) {

		if (phaseIncrement < 69273666 || AudioEngine::cpuDireness >= 7) {
			// Crude triangle: half-scale getTriangleSmall fed to a doubled-amplitude VCA, so the
			// standalone (un-amplified) output is << 1. Plain Phasor free-runs; SyncedPhasor hard-syncs.
			auto makeVca = [&] { return fixed::CrudeAmplitude{amplitude << 1, amplitudeIncrement << 1}; };
			if (doOscSync) {
				fixed::CrudeTriangle osc{fixed::SyncedPhasor{
				    Phasor{phase, phaseIncrement}, Phasor{resetterPhase, resetterPhaseIncrement}, retriggerPhase}};
				renderThroughVca(osc, makeVca, Standalone::kDouble);
				phase = osc.phase();
				maybeStorePhase(type, startPhase, phase, doPulseWave);
				return;
			}
			fixed::CrudeTriangle osc{Phasor{phase, phaseIncrement}};
			renderThroughVca(osc, makeVca, Standalone::kDouble);
			return;
		}

		else {

			// Triangle's band ladder always used the (post-pulse-width) `phaseIncrement` directly, so
			// its selection increment is `phaseIncrement`, not `phaseIncrementForCalculations`.
			renderFromTable(fixed::triangle_bandlimited_table(), amplitude, amplitudeIncrement, phaseIncrement);
			return;
		}
	}

	else [[likely]] {

		uint32_t phaseToAdd;

		if (type == OscType::SAW) {
			// If frequency low enough, we just use a crude calculation for the wave without anti-aliasing
			if (tableNumber < AudioEngine::cpuDireness + 6) {

				// Crude saw: the phase ramp itself (full scale), so the standalone output is >> 1.
				auto makeVca = [&] { return fixed::CrudeAmplitude{amplitude, amplitudeIncrement}; };
				if (!doOscSync) {
					fixed::CrudeSaw osc{Phasor{phase, phaseIncrement}};
					renderThroughVca(osc, makeVca, Standalone::kHalve);
					return;
				}
				fixed::CrudeSaw osc{fixed::SyncedPhasor{Phasor{phase, phaseIncrement},
				                                        Phasor{resetterPhase, resetterPhaseIncrement}, retriggerPhase}};
				renderThroughVca(osc, makeVca, Standalone::kHalve);
				phase = osc.phase();
				maybeStorePhase(type, startPhase, phase, doPulseWave);
				return;
			}

			else {
				bands = &fixed::saw_bandlimited_table();
			}
		}

		else if (type == OscType::SQUARE) {
			// If frequency low enough, we just use a crude calculation for the wave without anti-aliasing
			if (tableNumber < AudioEngine::cpuDireness + 6) {

				// Crude square: full-scale getSquare, so the standalone output is >> 1 (== getSquareSmall).
				auto makeVca = [&] { return fixed::CrudeAmplitude{amplitude, amplitudeIncrement}; };
				if (!doOscSync) {
					fixed::CrudeSquare osc{Phasor{phase, phaseIncrement}, pulseWidth};
					renderThroughVca(osc, makeVca, Standalone::kHalve);
					return;
				}
				fixed::CrudeSquare osc{fixed::SyncedPhasor{Phasor{phase, phaseIncrement},
				                                           Phasor{resetterPhase, resetterPhaseIncrement},
				                                           retriggerPhase},
				                       pulseWidth};
				renderThroughVca(osc, makeVca, Standalone::kHalve);
				phase = osc.phase();
				maybeStorePhase(type, startPhase, phase, doPulseWave);
				return;
			}

			else {
				bands = &fixed::square_bandlimited_table();

				// If pulse wave, we have our own special routines here
				if (doPulseWave) {

					amplitude <<= 1;
					amplitudeIncrement <<= 1;

					phaseToAdd = -(pulseWidth >> 1);

					phase >>= 1;
					phaseIncrement >>= 1;

					// The perfect pulse: ring-modulate two band-limited squares offset by the pulse
					// width. Same raw kernel + vqdmulhq VCA as the table path, so the gen-then-VCA
					// (and SyncedOscillator) migration is bit-exact with renderPulseWave/renderOscSync.
					fixed::PulseOscillator pulse{
					    fixed::square_bandlimited_table().select(phaseIncrementForCalculations), phase, phaseIncrement,
					    phaseToAdd};
					if (doOscSync) {
						int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
						fixed::SyncedOscillator osc{pulse, phase, phaseIncrement,
						                            fixed::Resetter{resetterPhase, resetterPhaseIncrement},
						                            retriggerPhase};
						osc.renderBlock(fixed::Buffer{bufferStartThisSync, static_cast<size_t>(numSamples)});
						phase = osc.phase() << 1;
						applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart,
						                             oscSyncRenderingBuffer);
						maybeStorePhase(type, startPhase, phase, doPulseWave);
						return;
					}
					else {
						if (applyAmplitude) {
							pulse.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
							fixed::Amplitude vca{amplitude, amplitudeIncrement};
							vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
							                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
							                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
						}
						else {
							pulse.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
						}
						return;
					}
				}
			}
		}

		else if (type == OscType::ANALOG_SAW_2) {
			bands = &fixed::analog_saw_bandlimited_table();
		}

		else if (type == OscType::ANALOG_SQUARE) {
			// This sounds different enough to the digital square that we can never just swap back to that to save CPU
			bands = &fixed::analog_square_bandlimited_table();
		}

		// If we're still here, we need to render the wave according to a table decided above

		amplitude <<= 1;
		amplitudeIncrement <<= 1;
		// Saw/analog band selection tracks the note (`phaseIncrementForCalculations`), which is what
		// the legacy `getTableNumber(phaseIncrementForCalculations)` indexed — `phaseIncrement` may
		// have been rewritten to the pulse-width render rate above.
		renderFromTable(*bands, amplitude, amplitudeIncrement, phaseIncrementForCalculations);
		return;
	}

	// Rohan thinks we never get here but who knows

	applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);

	maybeStorePhase(type, startPhase, phase, doPulseWave);
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
