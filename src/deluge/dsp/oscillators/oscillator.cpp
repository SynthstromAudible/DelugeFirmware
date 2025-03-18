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
#include "dsp/oscillators/basic_waves.h"
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
	switch (type) {
	case OscType::SINE:
		renderSine(amplitude, bufferStart, bufferEnd, numSamples, phaseIncrement, pulseWidth, startPhase,
		           applyAmplitude, amplitudeIncrement, doOscSync, resetterPhase, resetterPhaseIncrement, retriggerPhase,
		           waveIndexIncrement, sourceWaveIndexLastTime, waveTable);
		break;
	case OscType::TRIANGLE:
		renderTriangle(amplitude, bufferStart, bufferEnd, numSamples, phaseIncrement, pulseWidth, startPhase,
		               applyAmplitude, amplitudeIncrement, doOscSync, resetterPhase, resetterPhaseIncrement,
		               retriggerPhase, waveIndexIncrement, sourceWaveIndexLastTime, waveTable);
		break;
	case OscType::SQUARE:
		renderSquare(amplitude, bufferStart, bufferEnd, numSamples, phaseIncrement, pulseWidth, startPhase,
		             applyAmplitude, amplitudeIncrement, doOscSync, resetterPhase, resetterPhaseIncrement,
		             retriggerPhase, waveIndexIncrement, sourceWaveIndexLastTime, waveTable);
		break;
	case OscType::SAW:
		renderSaw(amplitude, bufferStart, bufferEnd, numSamples, phaseIncrement, pulseWidth, startPhase, applyAmplitude,
		          amplitudeIncrement, doOscSync, resetterPhase, resetterPhaseIncrement, retriggerPhase,
		          waveIndexIncrement, sourceWaveIndexLastTime, waveTable);
		break;
	case OscType::WAVETABLE:
		renderWavetable(amplitude, bufferStart, bufferEnd, numSamples, phaseIncrement, pulseWidth, startPhase,
		                applyAmplitude, amplitudeIncrement, doOscSync, resetterPhase, resetterPhaseIncrement,
		                retriggerPhase, waveIndexIncrement, sourceWaveIndexLastTime, waveTable);
		break;
	case OscType::ANALOG_SAW_2:
		renderAnalogSaw2(amplitude, bufferStart, bufferEnd, numSamples, phaseIncrement, pulseWidth, startPhase,
		                 applyAmplitude, amplitudeIncrement, doOscSync, resetterPhase, resetterPhaseIncrement,
		                 retriggerPhase, waveIndexIncrement, sourceWaveIndexLastTime, waveTable);
		break;
	case OscType::ANALOG_SQUARE:
		renderAnalogSquare(amplitude, bufferStart, bufferEnd, numSamples, phaseIncrement, pulseWidth, startPhase,
		                   applyAmplitude, amplitudeIncrement, doOscSync, resetterPhase, resetterPhaseIncrement,
		                   retriggerPhase, waveIndexIncrement, sourceWaveIndexLastTime, waveTable);
		break;
	default:
		// Handle default case if necessary
		break;
	}
}

void Oscillator::renderSine(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                            uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase, bool applyAmplitude,
                            int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase,
                            uint32_t resetterPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement,
                            int sourceWaveIndexLastTime, WaveTable* waveTable) {
	uint32_t phase = *startPhase;
	*startPhase += phaseIncrement * numSamples;

	bool doPulseWave = false;

	int32_t resetterDivideByPhaseIncrement;
	const int16_t* table;

	retriggerPhase += 3221225472u;

	if (doOscSync) [[unlikely]] {
		resetterDivideByPhaseIncrement = (uint32_t)2147483648u / (uint16_t)((resetterPhaseIncrement + 65535) >> 16);
	}

	table = sineWaveSmall;
	int32_t tableSizeMagnitude = 8;

	if (!doOscSync) {
		dsp::renderWave(table, tableSizeMagnitude, amplitude, {bufferStart, bufferEnd}, phaseIncrement, phase,
		                applyAmplitude, 0, amplitudeIncrement);
		return;
	}
	int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
	int32_t numSamplesThisOscSyncSession = numSamples;
	auto storeVectorWaveForOneSync = [&](std::span<q31_t> buffer, uint32_t phase) {
		for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
			std::tie(value_vector, phase) =
			    waveRenderingFunctionPulse(phase, phaseIncrement, 0, table, tableSizeMagnitude);
		}
	};
	renderOscSync(
	    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase, resetterPhaseIncrement,
	    resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession, bufferStartThisSync);
	applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);
	maybeStorePhase(OscType::SINE, startPhase, phase, doPulseWave);
}

void Oscillator::renderTriangle(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                                uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase, bool applyAmplitude,
                                int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase,
                                uint32_t resetterPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement,
                                int sourceWaveIndexLastTime, WaveTable* waveTable) {
	uint32_t phase = *startPhase;
	*startPhase += phaseIncrement * numSamples;

	bool doPulseWave = false;

	int32_t resetterDivideByPhaseIncrement;
	const int16_t* table;

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
			maybeStorePhase(OscType::TRIANGLE, startPhase, phase, doPulseWave);
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
		int32_t tableSizeMagnitude;
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
		else {
			tableSizeMagnitude = 6;
			if (phaseIncrement <= 715827882) {
				table = triangleWaveAntiAliasing3;
			}
			else {
				table = triangleWaveAntiAliasing1;
			}
		}

		if (!doOscSync) {
			dsp::renderWave(table, tableSizeMagnitude, amplitude, {bufferStart, bufferEnd}, phaseIncrement, phase,
			                applyAmplitude, 0, amplitudeIncrement);
			return;
		}
		int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
		int32_t numSamplesThisOscSyncSession = numSamples;
		auto storeVectorWaveForOneSync = [&](std::span<q31_t> buffer, uint32_t phase) {
			for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
				std::tie(value_vector, phase) =
				    waveRenderingFunctionPulse(phase, phaseIncrement, 0, table, tableSizeMagnitude);
			}
		};
		renderOscSync(
		    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase, resetterPhaseIncrement,
		    resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession, bufferStartThisSync);
		applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);
		maybeStorePhase(OscType::TRIANGLE, startPhase, phase, doPulseWave);
		return;
	}
}

void Oscillator::renderSquare(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                              uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase, bool applyAmplitude,
                              int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase,
                              uint32_t resetterPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement,
                              int sourceWaveIndexLastTime, WaveTable* waveTable) {
	uint32_t phase = *startPhase;
	*startPhase += phaseIncrement * numSamples;

	bool doPulseWave = false;

	int32_t resetterDivideByPhaseIncrement;
	const int16_t* table;

	uint32_t phaseIncrementForCalculations = phaseIncrement;

	doPulseWave = (pulseWidth != 0);
	pulseWidth += 2147483648u;
	if (doPulseWave) {
		phaseIncrementForCalculations = phaseIncrement * 0.6;
	}

	int32_t tableNumber, tableSizeMagnitude;
	std::tie(tableNumber, tableSizeMagnitude) = dsp::getTableNumber(phaseIncrementForCalculations);

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
					    *thisSample, getSquare(phaseNow, pulseWidth), amplitudeNow);
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

				if (resetterPhaseNow < resetterPhaseIncrement) {
					phaseNow = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetterPhaseNow, phaseIncrement),
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
			maybeStorePhase(OscType::SQUARE, startPhase, phase, doPulseWave);
			return;
		}
	}
	else {
		table = dsp::squareTables[tableNumber];

		if (doPulseWave) {
			amplitude <<= 1;
			amplitudeIncrement <<= 1;

			uint32_t phaseToAdd = -(pulseWidth >> 1);

			phase >>= 1;
			phaseIncrement >>= 1;

			if (!doOscSync) {
				dsp::renderPulseWave(table, tableSizeMagnitude, amplitude, {bufferStart, bufferEnd}, phaseIncrement,
				                     phase, applyAmplitude, phaseToAdd, amplitudeIncrement);
				return;
			}

			int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
			int32_t numSamplesThisOscSyncSession = numSamples;
			auto storeVectorWaveForOneSync = [&](std::span<q31_t> buffer, uint32_t phase) {
				for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
					std::tie(value_vector, phase) =
					    waveRenderingFunctionPulse(phase, phaseIncrement, phaseToAdd, table, tableSizeMagnitude);
				}
			};
			renderOscSync(
			    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase,
			    resetterPhaseIncrement, resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession,
			    bufferStartThisSync);
			phase <<= 1;
			applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart,
			                             oscSyncRenderingBuffer);
			maybeStorePhase(OscType::SQUARE, startPhase, phase, doPulseWave);
		}

		amplitude <<= 1;
		amplitudeIncrement <<= 1;

		if (!doOscSync) {
			dsp::renderWave(table, tableSizeMagnitude, amplitude, {bufferStart, bufferEnd}, phaseIncrement, phase,
			                applyAmplitude, 0, amplitudeIncrement);
			return;
		}

		int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
		int32_t numSamplesThisOscSyncSession = numSamples;
		auto storeVectorWaveForOneSync = [&](std::span<q31_t> buffer, uint32_t phase) {
			for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
				std::tie(value_vector, phase) =
				    waveRenderingFunctionPulse(phase, phaseIncrement, 0, table, tableSizeMagnitude);
			}
		};
		renderOscSync(
		    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase, resetterPhaseIncrement,
		    resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession, bufferStartThisSync);
		applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);
		maybeStorePhase(OscType::SAW, startPhase, phase, doPulseWave);
	}
}

void Oscillator::renderSaw(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                           uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase, bool applyAmplitude,
                           int32_t amplitudeIncrement, bool doOscSync, uint32_t resetterPhase,
                           uint32_t resetterPhaseIncrement, uint32_t retriggerPhase, int32_t waveIndexIncrement,
                           int sourceWaveIndexLastTime, WaveTable* waveTable) {
	uint32_t phase = *startPhase;
	*startPhase += phaseIncrement * numSamples;

	bool doPulseWave = false;

	int32_t resetterDivideByPhaseIncrement;
	const int16_t* table;

	int32_t tableNumber, tableSizeMagnitude;
	std::tie(tableNumber, tableSizeMagnitude) = dsp::getTableNumber(phaseIncrement);

	retriggerPhase += 2147483648u;

	if (tableNumber < AudioEngine::cpuDireness + 6) {
		if (!doOscSync) {
			if (applyAmplitude) {
				dsp::renderCrudeSawWave({bufferStart, bufferEnd}, phase, phaseIncrement, amplitude, amplitudeIncrement);
			}
			else {
				dsp::renderCrudeSawWave({bufferStart, bufferEnd}, phase, phaseIncrement);
			}
			return;
		}
		int32_t amplitudeNow = amplitude;
		uint32_t phaseNow = phase;
		uint32_t resetterPhaseNow = resetterPhase;
		int32_t* thisSample = bufferStart;

		do {
			phaseNow += phaseIncrement;
			resetterPhaseNow += resetterPhaseIncrement;

			if (resetterPhaseNow < resetterPhaseIncrement) {
				phaseNow = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetterPhaseNow, phaseIncrement),
				                                    resetterDivideByPhaseIncrement)
				            << 17)
				           + 1 + retriggerPhase;
			}

			if (applyAmplitude) {
				amplitudeNow += amplitudeIncrement;
				*thisSample = multiply_accumulate_32x32_rshift32_rounded(*thisSample, (int32_t)phaseNow, amplitudeNow);
			}
			else {
				*thisSample = (int32_t)phaseNow >> 1;
			}
		} while (++thisSample != bufferEnd);

		phase = phaseNow;
		maybeStorePhase(OscType::SAW, startPhase, phase, doPulseWave);
		return;
	}

	table = dsp::sawTables[tableNumber];

	amplitude <<= 1;
	amplitudeIncrement <<= 1;

	if (!doOscSync) {
		dsp::renderWave(table, tableSizeMagnitude, amplitude, {bufferStart, bufferEnd}, phaseIncrement, phase,
		                applyAmplitude, 0, amplitudeIncrement);
		return;
	}

	int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
	int32_t numSamplesThisOscSyncSession = numSamples;
	auto storeVectorWaveForOneSync = [&](std::span<q31_t> buffer, uint32_t phase) {
		for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
			std::tie(value_vector, phase) =
			    waveRenderingFunctionPulse(phase, phaseIncrement, 0, table, tableSizeMagnitude);
		}
	};
	renderOscSync(
	    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase, resetterPhaseIncrement,
	    resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession, bufferStartThisSync);
	applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);
	maybeStorePhase(OscType::SAW, startPhase, phase, doPulseWave);
}

void Oscillator::renderWavetable(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                                 uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase,
                                 bool applyAmplitude, int32_t amplitudeIncrement, bool doOscSync,
                                 uint32_t resetterPhase, uint32_t resetterPhaseIncrement, uint32_t retriggerPhase,
                                 int32_t waveIndexIncrement, int sourceWaveIndexLastTime, WaveTable* waveTable) {
	uint32_t phase = *startPhase;
	*startPhase += phaseIncrement * numSamples;

	bool doPulseWave = false;

	int32_t resetterDivideByPhaseIncrement;
	const int16_t* table;

	int32_t waveIndex = sourceWaveIndexLastTime + 1073741824;

	int32_t* wavetableRenderingBuffer = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;

	phase = waveTable->render(wavetableRenderingBuffer, numSamples, phaseIncrement, phase, doOscSync, resetterPhase,
	                          resetterPhaseIncrement, resetterDivideByPhaseIncrement, retriggerPhase, waveIndex,
	                          waveIndexIncrement);

	amplitude <<= 3;
	amplitudeIncrement <<= 3;
	if (applyAmplitude) {
		applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);
	}
	maybeStorePhase(OscType::WAVETABLE, startPhase, phase, doPulseWave);
}

void Oscillator::renderAnalogSaw2(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                                  uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase,
                                  bool applyAmplitude, int32_t amplitudeIncrement, bool doOscSync,
                                  uint32_t resetterPhase, uint32_t resetterPhaseIncrement, uint32_t retriggerPhase,
                                  int32_t waveIndexIncrement, int sourceWaveIndexLastTime, WaveTable* waveTable) {
	uint32_t phase = *startPhase;
	*startPhase += phaseIncrement * numSamples;

	bool doPulseWave = false;

	int32_t resetterDivideByPhaseIncrement;
	const int16_t* table;

	int32_t tableNumber, tableSizeMagnitude;
	std::tie(tableNumber, tableSizeMagnitude) = dsp::getTableNumber(phaseIncrement);

	if (tableNumber >= 8 && tableNumber < AudioEngine::cpuDireness + 6) {
		renderSaw(amplitude, bufferStart, bufferEnd, numSamples, phaseIncrement, pulseWidth, startPhase, applyAmplitude,
		          amplitudeIncrement, doOscSync, resetterPhase, resetterPhaseIncrement, retriggerPhase,
		          waveIndexIncrement, sourceWaveIndexLastTime, waveTable);
		return;
	}

	table = dsp::analogSawTables[tableNumber];

	amplitude <<= 1;
	amplitudeIncrement <<= 1;

	if (!doOscSync) {
		dsp::renderWave(table, tableSizeMagnitude, amplitude, {bufferStart, bufferEnd}, phaseIncrement, phase,
		                applyAmplitude, 0, amplitudeIncrement);
		return;
	}

	int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
	int32_t numSamplesThisOscSyncSession = numSamples;
	auto storeVectorWaveForOneSync = [&](std::span<q31_t> buffer, uint32_t phase) {
		for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
			std::tie(value_vector, phase) =
			    waveRenderingFunctionGeneral(phase, phaseIncrement, 0, table, tableSizeMagnitude);
		}
	};
	renderOscSync(
	    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase, resetterPhaseIncrement,
	    resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession, bufferStartThisSync);
	applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);
	maybeStorePhase(OscType::ANALOG_SAW_2, startPhase, phase, doPulseWave);
}

void Oscillator::renderAnalogSquare(int32_t amplitude, int32_t* bufferStart, int32_t* bufferEnd, int32_t numSamples,
                                    uint32_t phaseIncrement, uint32_t pulseWidth, uint32_t* startPhase,
                                    bool applyAmplitude, int32_t amplitudeIncrement, bool doOscSync,
                                    uint32_t resetterPhase, uint32_t resetterPhaseIncrement, uint32_t retriggerPhase,
                                    int32_t waveIndexIncrement, int sourceWaveIndexLastTime, WaveTable* waveTable) {
	uint32_t phase = *startPhase;
	*startPhase += phaseIncrement * numSamples;

	bool doPulseWave = false;

	int32_t resetterDivideByPhaseIncrement;
	const int16_t* table;

	int32_t tableNumber, tableSizeMagnitude;
	std::tie(tableNumber, tableSizeMagnitude) = dsp::getTableNumber(phaseIncrement);

	doPulseWave = (pulseWidth && !doOscSync);
	if (doPulseWave) {
		doOscSync = true;

		uint32_t pulseWidthAbsolute = ((int32_t)pulseWidth >= 0) ? pulseWidth : -pulseWidth;

		resetterPhase = phase;
		resetterPhaseIncrement = phaseIncrement;

		int64_t resetterPhaseToDivide = (uint64_t)resetterPhase << 30;

		if ((uint32_t)(resetterPhase) >= (uint32_t)-(resetterPhaseIncrement >> 1)) {
			resetterPhaseToDivide -= (uint64_t)1 << 62;
		}

		phase = resetterPhaseToDivide / (int32_t)((pulseWidthAbsolute + 2147483648u) >> 1);
		phaseIncrement = ((uint64_t)phaseIncrement << 31) / (pulseWidthAbsolute + 2147483648u);

		phase += retriggerPhase;
	}

	table = dsp::analogSquareTables[tableNumber];

	amplitude <<= 1;
	amplitudeIncrement <<= 1;

	if (!doOscSync) {
		dsp::renderWave(table, tableSizeMagnitude, amplitude, {bufferStart, bufferEnd}, phaseIncrement, phase,
		                applyAmplitude, 0, amplitudeIncrement);
		return;
	}
	int32_t* bufferStartThisSync = applyAmplitude ? oscSyncRenderingBuffer : bufferStart;
	int32_t numSamplesThisOscSyncSession = numSamples;
	auto storeVectorWaveForOneSync = [&](std::span<q31_t> buffer, uint32_t phase) {
		for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
			std::tie(value_vector, phase) =
			    waveRenderingFunctionPulse(phase, phaseIncrement, 0, table, tableSizeMagnitude);
		}
	};

	renderOscSync(
	    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phaseIncrement, resetterPhase, resetterPhaseIncrement,
	    resetterDivideByPhaseIncrement, retriggerPhase, numSamplesThisOscSyncSession, bufferStartThisSync);
	applyAmplitudeVectorToBuffer(amplitude, numSamples, amplitudeIncrement, bufferStart, oscSyncRenderingBuffer);
	maybeStorePhase(OscType::ANALOG_SQUARE, startPhase, phase, doPulseWave);
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
