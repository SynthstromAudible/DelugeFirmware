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
Oscillator::renderOsc(OscType osc_type, int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end,
                      int32_t num_samples, uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
                      bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync, uint32_t resetter_phase,
                      uint32_t resetter_phase_increment, uint32_t retrigger_phase, int32_t wave_index_increment,
                      int source_wave_index_last_time, WaveTable* wave_table) {
	switch (osc_type) {
	case OscType::SINE:
		renderSine(amplitude, buffer_start, buffer_end, num_samples, phase_increment, pulse_width, start_phase,
		           apply_amplitude, amplitude_increment, do_osc_sync, resetter_phase, resetter_phase_increment,
		           retrigger_phase, wave_index_increment, source_wave_index_last_time, wave_table);
		break;
	case OscType::TRIANGLE:
		renderTriangle(amplitude, buffer_start, buffer_end, num_samples, phase_increment, pulse_width, start_phase,
		               apply_amplitude, amplitude_increment, do_osc_sync, resetter_phase, resetter_phase_increment,
		               retrigger_phase, wave_index_increment, source_wave_index_last_time, wave_table);
		break;
	case OscType::SQUARE:
		renderSquare(amplitude, buffer_start, buffer_end, num_samples, phase_increment, pulse_width, start_phase,
		             apply_amplitude, amplitude_increment, do_osc_sync, resetter_phase, resetter_phase_increment,
		             retrigger_phase, wave_index_increment, source_wave_index_last_time, wave_table);
		break;
	case OscType::SAW:
		renderSaw(amplitude, buffer_start, buffer_end, num_samples, phase_increment, pulse_width, start_phase,
		          apply_amplitude, amplitude_increment, do_osc_sync, resetter_phase, resetter_phase_increment,
		          retrigger_phase, wave_index_increment, source_wave_index_last_time, wave_table);
		break;
	case OscType::WAVETABLE:
		renderWavetable(amplitude, buffer_start, buffer_end, num_samples, phase_increment, pulse_width, start_phase,
		                apply_amplitude, amplitude_increment, do_osc_sync, resetter_phase, resetter_phase_increment,
		                retrigger_phase, wave_index_increment, source_wave_index_last_time, wave_table);
		break;
	case OscType::ANALOG_SAW_2:
		renderAnalogSaw2(amplitude, buffer_start, buffer_end, num_samples, phase_increment, pulse_width, start_phase,
		                 apply_amplitude, amplitude_increment, do_osc_sync, resetter_phase, resetter_phase_increment,
		                 retrigger_phase, wave_index_increment, source_wave_index_last_time, wave_table);
		break;
	case OscType::ANALOG_SQUARE:
		renderAnalogSquare(amplitude, buffer_start, buffer_end, num_samples, phase_increment, pulse_width, start_phase,
		                   apply_amplitude, amplitude_increment, do_osc_sync, resetter_phase, resetter_phase_increment,
		                   retrigger_phase, wave_index_increment, source_wave_index_last_time, wave_table);
		break;
	default:
		// Handle default case if necessary
		break;
	}
}

void Oscillator::renderSine(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
                            uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase, bool apply_amplitude,
                            int32_t amplitude_increment, bool do_osc_sync, uint32_t resetter_phase,
                            uint32_t resetter_phase_increment, uint32_t retrigger_phase, int32_t wave_index_increment,
                            int source_wave_index_last_time, WaveTable* wave_table) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * num_samples;

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment;
	const int16_t* table;

	retrigger_phase += 3221225472u;

	if (do_osc_sync) [[unlikely]] {
		resetter_divide_by_phase_increment =
		    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);
	}

	table = sineWaveSmall;
	int32_t table_size_magnitude = 8;

	if (!do_osc_sync) {
		dsp::renderWave(table, table_size_magnitude, amplitude, {buffer_start, buffer_end}, phase_increment, phase,
		                apply_amplitude, 0, amplitude_increment);
		return;
	}
	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer_start;
	int32_t num_samples_this_osc_sync_session = num_samples;
	auto store_vector_wave_for_one_sync = [&](std::span<q31_t> buffer, uint32_t phase) {
		for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
			std::tie(value_vector, phase) =
			    waveRenderingFunctionPulse(phase, phase_increment, 0, table, table_size_magnitude);
		}
	};
	renderOscSync(
	    store_vector_wave_for_one_sync, [](uint32_t) {}, phase, phase_increment, resetter_phase,
	    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	applyAmplitudeVectorToBuffer(amplitude, num_samples, amplitude_increment, buffer_start, oscSyncRenderingBuffer);
	if (!do_pulse_wave) {
		*start_phase = phase;
	}
}

void Oscillator::renderTriangle(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
                                uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
                                bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
                                uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
                                int32_t wave_index_increment, int source_wave_index_last_time, WaveTable* wave_table) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * num_samples;

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment;
	const int16_t* table;

	if (phase_increment < 69273666 || AudioEngine::cpuDireness >= 7) {
		if (do_osc_sync) {
			int32_t amplitude_now = amplitude << 1;
			uint32_t phase_now = phase;
			uint32_t resetter_phase_now = resetter_phase;
			amplitude_increment <<= 1;
			for (int32_t* this_sample = buffer_start; this_sample != buffer_end; ++this_sample) {
				phase_now += phase_increment;
				resetter_phase_now += resetter_phase_increment;

				if (resetter_phase_now < resetter_phase_increment) {
					phase_now = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetter_phase_now, phase_increment),
					                                     resetter_divide_by_phase_increment)
					             << 17)
					            + 1 + retrigger_phase;
				}

				int32_t value = getTriangleSmall(phase_now);

				if (apply_amplitude) {
					amplitude_now += amplitude_increment;
					*this_sample = multiply_accumulate_32x32_rshift32_rounded(*this_sample, value, amplitude_now);
				}
				else {
					*this_sample = value << 1;
				}
			}

			phase = phase_now;
			if (!do_pulse_wave) {
				*start_phase = phase;
			}
			return;
		}
		else {
			int32_t amplitude_now = amplitude << 1;
			uint32_t phase_now = phase;
			amplitude_increment <<= 1;
			for (int32_t* this_sample = buffer_start; this_sample != buffer_end; ++this_sample) {
				phase_now += phase_increment;

				int32_t value = getTriangleSmall(phase_now);

				if (apply_amplitude) {
					amplitude_now += amplitude_increment;
					*this_sample = multiply_accumulate_32x32_rshift32_rounded(*this_sample, value, amplitude_now);
				}
				else {
					*this_sample = value << 1;
				}
			}
			return;
		}
	}
	else {
		int32_t table_size_magnitude;
		if (phase_increment <= 429496729) {
			table_size_magnitude = 7;
			if (phase_increment <= 102261126) {
				table = triangleWaveAntiAliasing21;
			}
			else if (phase_increment <= 143165576) {
				table = triangleWaveAntiAliasing15;
			}
			else if (phase_increment <= 238609294) {
				table = triangleWaveAntiAliasing9;
			}
			else if (phase_increment <= 429496729) {
				table = triangleWaveAntiAliasing5;
			}
		}
		else {
			table_size_magnitude = 6;
			if (phase_increment <= 715827882) {
				table = triangleWaveAntiAliasing3;
			}
			else {
				table = triangleWaveAntiAliasing1;
			}
		}

		if (!do_osc_sync) {
			dsp::renderWave(table, table_size_magnitude, amplitude, {buffer_start, buffer_end}, phase_increment, phase,
			                apply_amplitude, 0, amplitude_increment);
			return;
		}
		int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer_start;
		int32_t num_samples_this_osc_sync_session = num_samples;
		auto store_vector_wave_for_one_sync = [&](std::span<q31_t> buffer, uint32_t phase) {
			for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
				std::tie(value_vector, phase) =
				    waveRenderingFunctionPulse(phase, phase_increment, 0, table, table_size_magnitude);
			}
		};
		renderOscSync(
		    store_vector_wave_for_one_sync, [](uint32_t) {}, phase, phase_increment, resetter_phase,
		    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
		    num_samples_this_osc_sync_session, buffer_start_this_sync);
		applyAmplitudeVectorToBuffer(amplitude, num_samples, amplitude_increment, buffer_start, oscSyncRenderingBuffer);
		if (!do_pulse_wave) {
			*start_phase = phase;
		}
		return;
	}
}

void Oscillator::renderSquare(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
                              uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
                              bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
                              uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
                              int32_t wave_index_increment, int source_wave_index_last_time, WaveTable* wave_table) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * num_samples;

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment;
	const int16_t* table;

	uint32_t phase_increment_for_calculations = phase_increment;

	do_pulse_wave = (pulse_width != 0);
	pulse_width += 2147483648u;
	if (do_pulse_wave) {
		phase_increment_for_calculations = phase_increment * 0.6;
	}

	int32_t table_number, table_size_magnitude;
	std::tie(table_number, table_size_magnitude) = dsp::getTableNumber(phase_increment_for_calculations);

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude;
		uint32_t phase_now = phase;
		uint32_t resetter_phase_now = resetter_phase;

		if (!do_osc_sync) {
			if (apply_amplitude) {
				for (int32_t* this_sample = buffer_start; this_sample != buffer_end; ++this_sample) {
					phase_now += phase_increment;
					amplitude_now += amplitude_increment;
					*this_sample = multiply_accumulate_32x32_rshift32_rounded(
					    *this_sample, getSquare(phase_now, pulse_width), amplitude_now);
				}
			}
			else {
#pragma GCC unroll 4
				for (int32_t* this_sample = buffer_start; this_sample != buffer_end; ++this_sample) {
					phase_now += phase_increment;
					*this_sample = getSquareSmall(phase_now, pulse_width);
				}
			}
			return;
		}
		else {
			for (int32_t* this_sample = buffer_start; this_sample != buffer_end; ++this_sample) {
				phase_now += phase_increment;
				resetter_phase_now += resetter_phase_increment;

				if (resetter_phase_now < resetter_phase_increment) {
					phase_now = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetter_phase_now, phase_increment),
					                                     resetter_divide_by_phase_increment)
					             << 17)
					            + 1 + retrigger_phase;
				}

				if (apply_amplitude) {
					amplitude_now += amplitude_increment;
					*this_sample = multiply_accumulate_32x32_rshift32_rounded(
					    *this_sample, getSquare(phase_now, pulse_width), amplitude_now);
				}
				else {
					*this_sample = getSquareSmall(phase_now, pulse_width);
				}
			}

			phase = phase_now;
			*start_phase = phase;
			return;
		}
	}
	else {
		table = dsp::squareTables[table_number];

		if (do_pulse_wave) {
			amplitude <<= 1;
			amplitude_increment <<= 1;

			uint32_t phase_to_add = -(pulse_width >> 1);

			phase >>= 1;
			phase_increment >>= 1;

			if (!do_osc_sync) {
				dsp::renderPulseWave(table, table_size_magnitude, amplitude, {buffer_start, buffer_end},
				                     phase_increment, phase, apply_amplitude, phase_to_add, amplitude_increment);
				return;
			}

			int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer_start;
			int32_t num_samples_this_osc_sync_session = num_samples;
			auto store_vector_wave_for_one_sync = [&](std::span<q31_t> buffer, uint32_t phase) {
				for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
					std::tie(value_vector, phase) =
					    waveRenderingFunctionPulse(phase, phase_increment, phase_to_add, table, table_size_magnitude);
				}
			};
			renderOscSync(
			    store_vector_wave_for_one_sync, [](uint32_t) {}, phase, phase_increment, resetter_phase,
			    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
			    num_samples_this_osc_sync_session, buffer_start_this_sync);
			phase <<= 1;
			applyAmplitudeVectorToBuffer(amplitude, num_samples, amplitude_increment, buffer_start,
			                             oscSyncRenderingBuffer);
			*start_phase = phase;
		}

		amplitude <<= 1;
		amplitude_increment <<= 1;

		if (!do_osc_sync) {
			dsp::renderWave(table, table_size_magnitude, amplitude, {buffer_start, buffer_end}, phase_increment, phase,
			                apply_amplitude, 0, amplitude_increment);
			return;
		}

		int32_t* bufferStartThisSync = apply_amplitude ? oscSyncRenderingBuffer : buffer_start;
		int32_t numSamplesThisOscSyncSession = num_samples;
		auto storeVectorWaveForOneSync = [&](std::span<q31_t> buffer, uint32_t phase) {
			for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
				std::tie(value_vector, phase) =
				    waveRenderingFunctionPulse(phase, phase_increment, 0, table, table_size_magnitude);
			}
		};
		renderOscSync(
		    storeVectorWaveForOneSync, [](uint32_t) {}, phase, phase_increment, resetter_phase, resetter_phase_increment,
		    resetter_divide_by_phase_increment, retrigger_phase, numSamplesThisOscSyncSession, bufferStartThisSync);
		applyAmplitudeVectorToBuffer(amplitude, num_samples, amplitude_increment, buffer_start, oscSyncRenderingBuffer);
		maybeStorePhase(OscType::SAW, start_phase, phase, do_pulse_wave);
	}
}

void Oscillator::renderSaw(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
                           uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase, bool apply_amplitude,
                           int32_t amplitude_increment, bool do_osc_sync, uint32_t resetter_phase,
                           uint32_t resetter_phase_increment, uint32_t retrigger_phase, int32_t wave_index_increment,
                           int source_wave_index_last_time, WaveTable* wave_table) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * num_samples;

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment;
	const int16_t* table;

	int32_t table_number, table_size_magnitude;
	std::tie(table_number, table_size_magnitude) = dsp::getTableNumber(phase_increment);

	retrigger_phase += 2147483648u;

	if (table_number < AudioEngine::cpuDireness + 6) {
		if (!do_osc_sync) {
			if (apply_amplitude) {
				dsp::renderCrudeSawWave({buffer_start, buffer_end}, phase, phase_increment, amplitude,
				                        amplitude_increment);
			}
			else {
				dsp::renderCrudeSawWave({buffer_start, buffer_end}, phase, phase_increment);
			}
			return;
		}
		int32_t amplitude_now = amplitude;
		uint32_t phase_now = phase;
		uint32_t resetter_phase_now = resetter_phase;

		for (int32_t* this_sample = buffer_start; this_sample != buffer_end; ++this_sample) {
			phase_now += phase_increment;
			resetter_phase_now += resetter_phase_increment;

			if (resetter_phase_now < resetter_phase_increment) {
				phase_now = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetter_phase_now, phase_increment),
				                                     resetter_divide_by_phase_increment)
				             << 17)
				            + 1 + retrigger_phase;
			}

			if (apply_amplitude) {
				amplitude_now += amplitude_increment;
				*this_sample =
				    multiply_accumulate_32x32_rshift32_rounded(*this_sample, (int32_t)phase_now, amplitude_now);
			}
			else {
				*this_sample = (int32_t)phase_now >> 1;
			}
		}

		phase = phase_now;
		if (!do_pulse_wave) {
			*start_phase = phase;
		}
		return;
	}

	table = dsp::sawTables[table_number];

	amplitude <<= 1;
	amplitude_increment <<= 1;

	if (!do_osc_sync) {
		dsp::renderWave(table, table_size_magnitude, amplitude, {buffer_start, buffer_end}, phase_increment, phase,
		                apply_amplitude, 0, amplitude_increment);
		return;
	}

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer_start;
	int32_t num_samples_this_osc_sync_session = num_samples;
	auto store_vector_wave_for_one_sync = [&](std::span<q31_t> buffer, uint32_t phase) {
		for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
			std::tie(value_vector, phase) =
			    waveRenderingFunctionPulse(phase, phase_increment, 0, table, table_size_magnitude);
		}
	};
	renderOscSync(
	    store_vector_wave_for_one_sync, [](uint32_t) {}, phase, phase_increment, resetter_phase,
	    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	applyAmplitudeVectorToBuffer(amplitude, num_samples, amplitude_increment, buffer_start, oscSyncRenderingBuffer);
	if (!do_pulse_wave) {
		*start_phase = phase;
	}
}

void Oscillator::renderWavetable(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
                                 uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
                                 bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
                                 uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
                                 int32_t wave_index_increment, int source_wave_index_last_time, WaveTable* wave_table) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * num_samples;

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment;
	const int16_t* table;

	int32_t wave_index = source_wave_index_last_time + 1073741824;

	int32_t* wavetable_rendering_buffer = apply_amplitude ? oscSyncRenderingBuffer : buffer_start;

	phase = wave_table->render(wavetable_rendering_buffer, num_samples, phase_increment, phase, do_osc_sync,
	                           resetter_phase, resetter_phase_increment, resetter_divide_by_phase_increment,
	                           retrigger_phase, wave_index, wave_index_increment);

	amplitude <<= 3;
	amplitude_increment <<= 3;
	if (apply_amplitude) {
		applyAmplitudeVectorToBuffer(amplitude, num_samples, amplitude_increment, buffer_start, oscSyncRenderingBuffer);
	}
	if (!do_pulse_wave) {
		*start_phase = phase;
	}
}

void Oscillator::renderAnalogSaw2(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
                                  uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
                                  bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
                                  uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
                                  int32_t wave_index_increment, int source_wave_index_last_time,
                                  WaveTable* wave_table) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * num_samples;

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment;
	const int16_t* table;

	int32_t table_number, table_size_magnitude;
	std::tie(table_number, table_size_magnitude) = dsp::getTableNumber(phase_increment);

	if (table_number >= 8 && table_number < AudioEngine::cpuDireness + 6) {
		renderSaw(amplitude, buffer_start, buffer_end, num_samples, phase_increment, pulse_width, start_phase,
		          apply_amplitude, amplitude_increment, do_osc_sync, resetter_phase, resetter_phase_increment,
		          retrigger_phase, wave_index_increment, source_wave_index_last_time, wave_table);
		return;
	}

	table = dsp::analogSawTables[table_number];

	amplitude <<= 1;
	amplitude_increment <<= 1;

	if (!do_osc_sync) {
		dsp::renderWave(table, table_size_magnitude, amplitude, {buffer_start, buffer_end}, phase_increment, phase,
		                apply_amplitude, 0, amplitude_increment);
		return;
	}

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer_start;
	int32_t num_samples_this_osc_sync_session = num_samples;
	auto store_vector_wave_for_one_sync = [&](std::span<q31_t> buffer, uint32_t phase) {
		for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
			std::tie(value_vector, phase) =
			    waveRenderingFunctionGeneral(phase, phase_increment, 0, table, table_size_magnitude);
		}
	};
	renderOscSync(
	    store_vector_wave_for_one_sync, [](uint32_t) {}, phase, phase_increment, resetter_phase,
	    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	applyAmplitudeVectorToBuffer(amplitude, num_samples, amplitude_increment, buffer_start, oscSyncRenderingBuffer);
	if (!do_pulse_wave) {
		*start_phase = phase;
	}
}

void Oscillator::renderAnalogSquare(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
                                    uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
                                    bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
                                    uint32_t resetter_phase, uint32_t resetter_phase_increment,
                                    uint32_t retrigger_phase, int32_t wave_index_increment,
                                    int source_wave_index_last_time, WaveTable* wave_table) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * num_samples;

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment;
	const int16_t* table;

	int32_t table_number, table_size_magnitude;
	std::tie(table_number, table_size_magnitude) = dsp::getTableNumber(phase_increment);

	do_pulse_wave = (pulse_width && !do_osc_sync);
	if (do_pulse_wave) {
		do_osc_sync = true;

		uint32_t pulse_width_absolute = ((int32_t)pulse_width >= 0) ? pulse_width : -pulse_width;

		resetter_phase = phase;
		resetter_phase_increment = phase_increment;

		int64_t resetter_phase_to_divide = (uint64_t)resetter_phase << 30;

		if ((uint32_t)(resetter_phase) >= (uint32_t)-(resetter_phase_increment >> 1)) {
			resetter_phase_to_divide -= (uint64_t)1 << 62;
		}

		phase = resetter_phase_to_divide / (int32_t)((pulse_width_absolute + 2147483648u) >> 1);
		phase_increment = ((uint64_t)phase_increment << 31) / (pulse_width_absolute + 2147483648u);

		phase += retrigger_phase;
	}

	table = dsp::analogSquareTables[table_number];

	amplitude <<= 1;
	amplitude_increment <<= 1;

	if (!do_osc_sync) {
		dsp::renderWave(table, table_size_magnitude, amplitude, {buffer_start, buffer_end}, phase_increment, phase,
		                apply_amplitude, 0, amplitude_increment);
		return;
	}
	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer_start;
	int32_t num_samples_this_osc_sync_session = num_samples;
	auto store_vector_wave_for_one_sync = [&](std::span<q31_t> buffer, uint32_t phase) {
		for (Argon<q31_t>& value_vector : argon::vectorize(buffer)) {
			std::tie(value_vector, phase) =
			    waveRenderingFunctionPulse(phase, phase_increment, 0, table, table_size_magnitude);
		}
	};

	renderOscSync(
	    store_vector_wave_for_one_sync, [](uint32_t) {}, phase, phase_increment, resetter_phase,
	    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	applyAmplitudeVectorToBuffer(amplitude, num_samples, amplitude_increment, buffer_start, oscSyncRenderingBuffer);
	if (!do_pulse_wave) {
		*start_phase = phase;
	}
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

} // namespace deluge::dsp
