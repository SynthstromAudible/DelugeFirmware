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
#include "basic_waves.h"
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/amplitude.h"
#include "dsp-next/fixed/crude_amplitude.h"
#include "dsp-next/fixed/crude_oscillators.h"
#include "dsp-next/fixed/pulse_oscillator.h"
#include "dsp-next/fixed/synced_oscillator.h"
#include "dsp-next/fixed/synced_phasor.h"
#include "dsp-next/fixed/table_oscillator.h"
#include "processing/engines/audio_engine.h"
#include "processing/render_wave.h"
#include "storage/wave_table/wave_table.h"
#include "util/fixedpoint.h"

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

	int32_t resetterDivideByPhaseIncrement;
	const int16_t* table;

	// For cases other than sines and triangles, we use these standard table lookup size thingies. We need to work this
	// out now so we can decide whether to switch the analog saw to the digital one
	int32_t tableNumber; // These only apply for waves other than sine and triangle
	int32_t tableSizeMagnitude;

	if (type == OscType::SINE) {
		retriggerPhase += 3221225472u;
	}

	else if (type != OscType::TRIANGLE) [[likely]] { // Not sines and not triangles
		uint32_t phaseIncrementForCalculations = phaseIncrement;

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

		std::tie(tableNumber, tableSizeMagnitude) = dsp::getTableNumber(phaseIncrementForCalculations);
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
			goto doOscSyncSetup;
		}
	}

	// We want to see if we're within half a phase-increment of the "reset" pos
	if (doOscSync) [[unlikely]] {
doOscSyncSetup:

		resetterDivideByPhaseIncrement = // You should >> 47 if multiplying by this.
		    (uint32_t)2147483648u
		    / (uint16_t)((resetterPhaseIncrement + 65535)
		                 >> 16); // Round resetterPhaseIncrement up first, so resetterDivideByPhaseIncrement gets a tiny
		                         // bit smaller, so things multiplied by it don't get a bit too big and overflow.
	}

	// Shared tail (Phase A1): render a band-limited wave from `renderTable` — straight or with
	// osc sync — and apply amplitude. Replaces the `callRenderWave` goto target. The rendering
	// context is captured by reference; the per-wave call sites vary only the table, its size
	// magnitude, and the amplitude pair. Behaviour unchanged.
	auto renderFromTable = [&](const int16_t* renderTable, int32_t renderTableSizeMagnitude, int32_t amp,
	                           int32_t ampIncrement) {
		if (doOscSync) {
			// Hard sync: a SyncedOscillator wraps the TableOscillator, restarting it at every
			// resetter crossover and crossfading the seam. It fills the raw wave (into the sync
			// scratch when amplitude applies), then the VCA scales-and-mixes it — bit-exact with
			// the legacy renderOscSync + overshoot storageFunction, minus the overshoot.
			int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
			fixed::SyncedOscillator osc{
			    fixed::TableOscillator{fixed::table_from_pointer(renderTable, renderTableSizeMagnitude), phase,
			                           phaseIncrement},
			    phase, phaseIncrement,
			    fixed::Resetter{resetterPhase, resetterPhaseIncrement, resetterDivideByPhaseIncrement}, retriggerPhase};
			osc.renderBlock(fixed::Buffer{bufferStartThisSync, static_cast<size_t>(numSamples)});
			phase = osc.phase();
			applyAmplitudeVectorToBuffer(amp, numSamples, ampIncrement, bufferStart, oscSyncRenderingBuffer);
			maybeStorePhase(type, startPhase, phase, doPulseWave);
		}
		else {
			// Generate the raw band-limited wave with the TableOscillator, then (when amplitude
			// is applied) scale-and-mix it with the Amplitude VCA — bit-exact with the fused
			// renderWave path. The osc's owned phase is fill-only; the persistent per-voice phase
			// was advanced at the top of renderOsc.
			fixed::TableOscillator osc{fixed::table_from_pointer(renderTable, renderTableSizeMagnitude), phase,
			                           phaseIncrement};
			if (applyAmplitude) {
				osc.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
				fixed::Amplitude vca{amp, ampIncrement};
				vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
				                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
				                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
			}
			else {
				osc.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
			}
		}
	};

	if (type == OscType::SINE) [[unlikely]] {
		renderFromTable(sineWaveSmall, 8, amplitude, amplitudeIncrement);
		return;
	}

	else if (type == OscType::WAVETABLE) {

		int32_t waveIndex = sourceWaveIndexLastTime + 1073741824;

		int32_t* wavetableRenderingBuffer = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;

		phase = waveTable->render(wavetableRenderingBuffer, numSamples, phaseIncrement, phase, doOscSync, resetterPhase,
		                          resetterPhaseIncrement, resetterDivideByPhaseIncrement, retriggerPhase, waveIndex,
		                          waveIndexIncrement);

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
			if (doOscSync) {
				// Same crude triangle generator as the non-sync arm, but driven by a SyncedPhasor
				// (per-sample hard reset) instead of a plain Phasor.
				fixed::CrudeTriangle osc{fixed::SyncedPhasor{Phasor{phase, phaseIncrement},
				                                             Phasor{resetterPhase, resetterPhaseIncrement},
				                                             resetterDivideByPhaseIncrement, retriggerPhase}};
				if (applyAmplitude) {
					osc.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
					fixed::CrudeAmplitude vca{amplitude << 1, amplitudeIncrement << 1};
					vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
					                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
					                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
				}
				else {
					osc.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
					for (int32_t* sample = bufferStart; sample != bufferEnd; ++sample) {
						*sample <<= 1;
					}
				}
				phase = osc.phase();
				maybeStorePhase(type, startPhase, phase, doPulseWave);
				return;
			}
			else {
				// Crude triangle generator, then the crude (rounded-MAC) VCA. Without amplitude the
				// raw getTriangleSmall is the half-scale value, so the standalone output is << 1.
				fixed::CrudeTriangle osc{Phasor{phase, phaseIncrement}};
				if (applyAmplitude) {
					osc.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
					fixed::CrudeAmplitude vca{amplitude << 1, amplitudeIncrement << 1};
					vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
					                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
					                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
				}
				else {
					osc.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
					for (int32_t* sample = bufferStart; sample != bufferEnd; ++sample) {
						*sample <<= 1;
					}
				}
				return;
			}
		}

		else {

			// Size 7
			if (phaseIncrement <= 429496729) {
				tableSizeMagnitude = 7;
				if (phaseIncrement <= 102261126) {
					table = triangleWaveAntiAliasing21;
				}
				else if (phaseIncrement <= 143165576) {
					table = triangleWaveAntiAliasing15;
				}
				else if (phaseIncrement <= 238609294) {
					table = triangleWaveAntiAliasing9;
				}
				else if (phaseIncrement <= 429496729) {
					table = triangleWaveAntiAliasing5;
				}
			}

			// Size 6
			else {
				tableSizeMagnitude = 6;
				if (phaseIncrement <= 715827882) {
					table = triangleWaveAntiAliasing3;
				}
				else {
					table = triangleWaveAntiAliasing1;
				}
			}
			renderFromTable(table, tableSizeMagnitude, amplitude, amplitudeIncrement);
			return;
		}
	}

	else [[likely]] {

		uint32_t phaseToAdd;

		if (type == OscType::SAW) {
			// If frequency low enough, we just use a crude calculation for the wave without anti-aliasing
			if (tableNumber < AudioEngine::cpuDireness + 6) {

				if (!doOscSync) {
					// Crude saw generator (the phase ramp itself), then the crude (rounded-MAC) VCA.
					// Without amplitude the standalone saw is the ramp >> 1.
					fixed::CrudeSaw osc{Phasor{phase, phaseIncrement}};
					if (applyAmplitude) {
						osc.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
						fixed::CrudeAmplitude vca{amplitude, amplitudeIncrement};
						vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
						                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
						                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
					}
					else {
						osc.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
						for (int32_t* sample = bufferStart; sample != bufferEnd; ++sample) {
							*sample >>= 1;
						}
					}
					return;
				}

				else {
					// Crude saw under hard sync: same generator, SyncedPhasor phase source.
					fixed::CrudeSaw osc{fixed::SyncedPhasor{Phasor{phase, phaseIncrement},
					                                        Phasor{resetterPhase, resetterPhaseIncrement},
					                                        resetterDivideByPhaseIncrement, retriggerPhase}};
					if (applyAmplitude) {
						osc.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
						fixed::CrudeAmplitude vca{amplitude, amplitudeIncrement};
						vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
						                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
						                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
					}
					else {
						osc.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
						for (int32_t* sample = bufferStart; sample != bufferEnd; ++sample) {
							*sample >>= 1;
						}
					}
					phase = osc.phase();
					maybeStorePhase(type, startPhase, phase, doPulseWave);
					return;
				}
			}

			else {
				table = dsp::sawTables[tableNumber];
			}
		}

		else if (type == OscType::SQUARE) {
			// If frequency low enough, we just use a crude calculation for the wave without anti-aliasing
			if (tableNumber < AudioEngine::cpuDireness + 6) {

				if (!doOscSync) {

					// Crude square / pulse generator, then the crude (rounded-MAC) VCA. Without
					// amplitude the standalone square is the full getSquare >> 1 (== getSquareSmall).
					fixed::CrudeSquare osc{Phasor{phase, phaseIncrement}, pulseWidth};
					if (applyAmplitude) {
						osc.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
						fixed::CrudeAmplitude vca{amplitude, amplitudeIncrement};
						vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
						                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
						                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
					}
					else {
						osc.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
						for (int32_t* sample = bufferStart; sample != bufferEnd; ++sample) {
							*sample >>= 1;
						}
					}
					return;
				}

				else {
					// Crude square under hard sync: same generator, SyncedPhasor phase source.
					fixed::CrudeSquare osc{fixed::SyncedPhasor{Phasor{phase, phaseIncrement},
					                                           Phasor{resetterPhase, resetterPhaseIncrement},
					                                           resetterDivideByPhaseIncrement, retriggerPhase},
					                       pulseWidth};
					if (applyAmplitude) {
						osc.renderBlock(fixed::Buffer{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)});
						fixed::CrudeAmplitude vca{amplitude, amplitudeIncrement};
						vca.renderBlock(fixed::Signal{oscSyncRenderingBuffer, static_cast<size_t>(numSamples)},
						                fixed::Signal{bufferStart, static_cast<size_t>(numSamples)},
						                fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
					}
					else {
						osc.renderBlock(fixed::Buffer{bufferStart, static_cast<size_t>(numSamples)});
						for (int32_t* sample = bufferStart; sample != bufferEnd; ++sample) {
							*sample >>= 1;
						}
					}
					phase = osc.phase();
					maybeStorePhase(type, startPhase, phase, doPulseWave);
					return;
				}
			}

			else {
				table = dsp::squareTables[tableNumber];

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
					fixed::PulseOscillator pulse{fixed::table_from_pointer(table, tableSizeMagnitude), phase,
					                             phaseIncrement, phaseToAdd};
					if (doOscSync) {
						int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
						fixed::SyncedOscillator osc{
						    pulse, phase, phaseIncrement,
						    fixed::Resetter{resetterPhase, resetterPhaseIncrement, resetterDivideByPhaseIncrement},
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
			table = dsp::analogSawTables[tableNumber];
		}

		else if (type == OscType::ANALOG_SQUARE) {
			// This sounds different enough to the digital square that we can never just swap back to that to save CPU
			table = dsp::analogSquareTables[tableNumber];
		}

		// If we're still here, we need to render the wave according to a table decided above

		amplitude <<= 1;
		amplitudeIncrement <<= 1;
		renderFromTable(table, tableSizeMagnitude, amplitude, amplitudeIncrement);
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
