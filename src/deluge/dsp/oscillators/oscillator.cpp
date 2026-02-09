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
#include "processing/engines/audio_engine.h"
#include "processing/render_wave.h"
#include "storage/wave_table/wave_table.h"
#include "util/fixedpoint.h"
#include "util/waves.h"

namespace deluge::dsp {
PLACE_INTERNAL_FRUNK int32_t oscSyncRenderingBuffer[SSI_TX_BUFFER_NUM_SAMPLES + 4]
    __attribute__((aligned(CACHE_LINE_SIZE)));
__attribute__((optimize("unroll-loops"))) void
Oscillator::renderOsc(OscType type, int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                      uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase, bool applyAmplitude,
                      int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase,
                      uint32_t resetterPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement,
                      int sourceWaveIndexLastTime, WaveTable* waveTable, uint64_t* prevPhaseScaler) {

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

	else if (type != OscType::TRIANGLE && type != OscType::TRIANGLE_PW) [[likely]] { // Not sines and not triangles
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

	if (type != OscType::SQUARE && type != OscType::TRIANGLE_PW) {
		// PW for oscillators other than the perfect mathematical square
		// TRIANGLE_PW handles pulse width differently (dead zones, not phase scaling)
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

skipPastOscSyncStuff:
	if (type == OscType::SINE) [[unlikely]] {
		table = sineWaveSmall;
		tableSizeMagnitude = 8;
		goto callRenderWave;
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

				int32_t amplitudeNow = amplitude << 1;
				uint32_t phaseNow = phase;
				int32_t* thisSample = bufferStart;
				uint32_t resetterPhaseNow = resetterPhase;
				amplitudeIncrement <<= 1;
				do {
					phaseNow += phaseIncrement;
					resetterPhaseNow += resetterPhaseIncrement;

					// Do the reset
					if (resetterPhaseNow < resetterPhaseIncrement) {
						phaseNow = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetterPhaseNow, phaseIncrement),
						                                    resetterDivideByPhaseIncrement)
						            << 17)
						           + 1 + retriggerPhase;
					}

					int32_t value = getTriangleSmall(phaseNow);

					if (applyAmplitude) {
						amplitudeNow += amplitudeIncrement;
						*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, value, amplitudeNow);
					}
					else {
						*thisSample = value << 1;
					}
				} while (++thisSample != bufferEnd);

				phase = phaseNow;
				maybeStorePhase(type, startPhase, phase, doPulseWave);
				return;
			}
			else {
				int32_t amplitudeNow = amplitude << 1;
				uint32_t phaseNow = phase;
				int32_t* thisSample = bufferStart;
				amplitudeIncrement <<= 1;
				do {
					phaseNow += phaseIncrement;

					int32_t value = getTriangleSmall(phaseNow);

					if (applyAmplitude) {
						amplitudeNow += amplitudeIncrement;
						*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, value, amplitudeNow);
					}
					else {
						*thisSample = value << 1;
					}
				} while (++thisSample != bufferEnd);
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
			goto callRenderWave;
		}
	}

	else if (type == OscType::TRIANGLE_PW) {
		// pulseWidth: 0 (UI 0) → INT32_MAX (UI 50) via half-precision
		// Inverted to match square wave UX: CW = narrower pulses
		constexpr uint32_t kMinPhaseWidth = 0x00800000; // ~0.2% duty cycle minimum

		uint32_t phaseWidth = 0xFFFFFFFF - (pulseWidth << 1);
		if (phaseWidth < kMinPhaseWidth) {
			phaseWidth = kMinPhaseWidth;
		}

		int32_t amplitudeNow = amplitude << 1;
		uint32_t phaseNow = phase + retriggerPhase;
		int32_t* thisSample = bufferStart;
		amplitudeIncrement <<= 1;

		uint64_t phaseScaler = computeTrianglePhaseScaler(phaseWidth);

		uint64_t phaseScalerNow;
		int64_t phaseScalerIncrement;
		if (prevPhaseScaler != nullptr) {
			phaseScalerNow = *prevPhaseScaler;
			phaseScalerIncrement = static_cast<int64_t>(phaseScaler - *prevPhaseScaler) / numSamples;
			*prevPhaseScaler = phaseScaler;
		}
		else {
			phaseScalerNow = phaseScaler;
			phaseScalerIncrement = 0;
		}

		if (applyAmplitude) {
			do {
				phaseNow += phaseIncrement;
				amplitudeNow += amplitudeIncrement;
				phaseScalerNow += phaseScalerIncrement;
				int32_t value = triangleWithDeadzoneBipolar(phaseNow, phaseWidth, phaseScalerNow);
				*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, value, amplitudeNow);
			} while (++thisSample != bufferEnd);
		}
		else {
			do {
				phaseNow += phaseIncrement;
				phaseScalerNow += phaseScalerIncrement;
				int32_t value = triangleWithDeadzoneBipolar(phaseNow, phaseWidth, phaseScalerNow);
				*thisSample = value << 1;
			} while (++thisSample != bufferEnd);
		}
		return;
	}

	else [[likely]] {

		uint32_t phaseToAdd;

		if (type == OscType::SAW) {
doSaw:
			// If frequency low enough, we just use a crude calculation for the wave without anti-aliasing
			if (tableNumber < AudioEngine::cpuDireness + 6) {

				if (!doOscSync) {
					if (applyAmplitude) {
						dsp::renderCrudeSawWaveWithAmplitude(bufferStart, bufferEnd, phase, phaseIncrement, amplitude,
						                                     amplitudeIncrement, numSamples);
					}
					else {
						dsp::renderCrudeSawWaveWithoutAmplitude(bufferStart, bufferEnd, phase, phaseIncrement,
						                                        numSamples);
					}
					return;
				}

				else {
					int32_t amplitudeNow = amplitude;
					uint32_t phaseNow = phase;
					uint32_t resetterPhaseNow = resetterPhase;
					int32_t* thisSample = bufferStart;

					do {
						phaseNow += phaseIncrement;
						resetterPhaseNow += resetterPhaseIncrement;

						// Do the reset
						if (resetterPhaseNow < resetterPhaseIncrement) {
							phaseNow =
							    (multiply_32x32_rshift32(multiply_32x32_rshift32(resetterPhaseNow, phaseIncrement),
							                             resetterDivideByPhaseIncrement)
							     << 17)
							    + 1 + retriggerPhase;
						}

						if (applyAmplitude) {
							amplitudeNow += amplitudeIncrement;
							*thisSample = multiply_accumulate_32x32_rshift32_rounded(
							    *thisSample, (int32_t)phaseNow,
							    amplitudeNow); // Using multiply_accumulate saves like 10% here!
						}
						else {
							*thisSample = (int32_t)phaseNow >> 1;
						}
					} while (++thisSample != bufferEnd);

					phase = phaseNow;
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

				int32_t amplitudeNow = amplitude;
				uint32_t phaseNow = phase;
				uint32_t resetterPhaseNow = resetterPhase;
				int32_t* thisSample = bufferStart;

				if (!doOscSync) {

					if (applyAmplitude) {

						do {
							phaseNow += phaseIncrement;
							amplitudeNow += amplitudeIncrement;
							*thisSample = multiply_accumulate_32x32_rshift32_rounded(
							    *thisSample, getSquare(phaseNow, pulseWidth),
							    amplitudeNow); // Using multiply_accumulate saves like 20% here, WTF!!!!
							++thisSample;
						} while (thisSample != bufferEnd);
					}

					else {
						int32_t* remainderSamplesEnd = bufferStart + (numSamples & 3);

						while (thisSample != remainderSamplesEnd) {
							phaseNow += phaseIncrement;
							*thisSample = getSquareSmall(phaseNow, pulseWidth);
							++thisSample;
						}

						while (thisSample != bufferEnd) {
							phaseNow += phaseIncrement;
							*thisSample = getSquareSmall(phaseNow, pulseWidth);
							++thisSample;

							phaseNow += phaseIncrement;
							*thisSample = getSquareSmall(phaseNow, pulseWidth);
							++thisSample;

							phaseNow += phaseIncrement;
							*thisSample = getSquareSmall(phaseNow, pulseWidth);
							++thisSample;

							phaseNow += phaseIncrement;
							*thisSample = getSquareSmall(phaseNow, pulseWidth);
							++thisSample;
						}
					}
					return;
				}

				else {
					do {

						phaseNow += phaseIncrement;

						resetterPhaseNow += resetterPhaseIncrement;

						// Do the reset
						if (resetterPhaseNow < resetterPhaseIncrement) {
							phaseNow =
							    (multiply_32x32_rshift32(multiply_32x32_rshift32(resetterPhaseNow, phaseIncrement),
							                             resetterDivideByPhaseIncrement)
							     << 17)
							    + 1 + retriggerPhase;
						}

						if (applyAmplitude) {
							amplitudeNow += amplitudeIncrement;
							*thisSample = multiply_accumulate_32x32_rshift32_rounded(
							    *thisSample, getSquare(phaseNow, pulseWidth), amplitudeNow);
						}
						else {
							*thisSample = getSquareSmall(phaseNow, pulseWidth);
						}
					} while (++thisSample != bufferEnd);

					phase = phaseNow;
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

					if (doOscSync) {
						int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
						int32_t numSamplesThisOscSyncSession = numSamples;
						auto storeVectorWaveForOneSync = [&](int32_t const* const bufferEndThisSyncRender,
						                                     uint32_t phase, int32_t* __restrict__ writePos) {
							int32x4_t valueVector;
							do {
								std::tie(valueVector, phase) = waveRenderingFunctionPulse(
								    phase, phaseIncrement, phaseToAdd, table, tableSizeMagnitude);
								vst1q_s32(writePos, valueVector);
								writePos += 4;
							} while (writePos < bufferEndThisSyncRender);
						};
						renderOscSync(
						    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase,
						    resetterPhaseIncrement, resetterDivideByPhaseIncrement, retriggerPhase,
						    numSamplesThisOscSyncSession, bufferStartThisSync);
						phase <<= 1;
						applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart,
						                             oscSyncRenderingBuffer);
						maybeStorePhase(type, startPhase, phase, doPulseWave);
						return;
					}
					else {
						dsp::renderPulseWave(table, tableSizeMagnitude, amplitude, bufferStart, bufferEnd,
						                     phaseIncrement, phase, applyAmplitude, phaseToAdd, amplitudeIncrement);
						return;
					}
				}
			}
		}

		else if (type == OscType::ANALOG_SAW_2) {
			table = dsp::analogSawTables[tableNumber];
		}

		else if (type == OscType::ANALOG_SQUARE) {
doAnalogSquare:
			// This sounds different enough to the digital square that we can never just swap back to that to save CPU
			table = dsp::analogSquareTables[tableNumber];
		}

		// If we're still here, we need to render the wave according to a table decided above

		amplitude <<= 1;
		amplitudeIncrement <<= 1;

callRenderWave:
		if (doOscSync) {
			int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
			int32_t numSamplesThisOscSyncSession = numSamples;
			auto storeVectorWaveForOneSync = //<
			    [&](int32_t const* const bufferEndThisSyncRender, uint32_t phase, int32_t* __restrict__ writePos) {
				    int32x4_t valueVector;
				    do {
					    std::tie(valueVector, phase) =
					        waveRenderingFunctionGeneral(phase, phaseIncrement, phaseToAdd, table, tableSizeMagnitude);
					    vst1q_s32(writePos, valueVector);
					    writePos += 4;
				    } while (writePos < bufferEndThisSyncRender);
			    };
			renderOscSync(
			    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase,
			    resetterPhaseIncrement, resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession,
			    bufferStartThisSync);
			applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart,
			                             oscSyncRenderingBuffer);
			maybeStorePhase(type, startPhase, phase, doPulseWave);
			return;
		}
		else {
			dsp::renderWave(table, tableSizeMagnitude, amplitude, bufferStart, bufferEnd, phaseIncrement, phase,
			                applyAmplitude, phaseToAdd, amplitudeIncrement);
			return;
		}
	}

	// Rohan thinks we never get here but who knows

	applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);

	maybeStorePhase(type, startPhase, phase, doPulseWave);
}
void Oscillator::applyAmplitudeVectorToBuffer(int32_t amplitude, int32_t numSamples, int32_t amplitudeIncrement,
                                              int32_t* outputBufferPos, int32_t* inputBuferPos) {
	int32_t const* const bufferEnd = outputBufferPos + numSamples;

	int32x4_t amplitudeVector = createAmplitudeVector(amplitude, amplitudeIncrement);
	int32x4_t amplitudeIncrementVector = vdupq_n_s32(amplitudeIncrement << 1);

	do {
		int32x4_t waveDataFromBefore = vld1q_s32(inputBuferPos);
		int32x4_t existingDataInBuffer = vld1q_s32(outputBufferPos);
		int32x4_t dataWithAmplitudeApplied = vqdmulhq_s32(amplitudeVector, waveDataFromBefore);
		amplitudeVector = vaddq_s32(amplitudeVector, amplitudeIncrementVector);
		int32x4_t sum = vaddq_s32(dataWithAmplitudeApplied, existingDataInBuffer);

		vst1q_s32(outputBufferPos, sum);

		outputBufferPos += 4;
		inputBuferPos += 4;
	} while (outputBufferPos < bufferEnd);
}
void Oscillator::maybeStorePhase(const OscType& type, uint32_t* startPhase, uint32_t phase, bool doPulseWave) {
	if (!(doPulseWave && type != OscType::SQUARE)) {
		*startPhase = phase;
	}
}

} // namespace deluge::dsp
