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
#include "definitions_cxx.hpp"
#include "dsp/core/conditional_processor.h"
#include "dsp/core/pipeline.h"
#include "dsp/core/unity_mixer.h"
#include "dsp/oscillators/basic_waves.h"
#include "dsp/processors/amplitude.h"
#include "processing/engines/audio_engine.h"
#include "render_wave.h"
#include "storage/wave_table/wave_table.h"
#include "table_oscillator.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include <cstdint>

namespace deluge::dsp {
PLACE_INTERNAL_FRUNK int32_t oscSyncRenderingBuffer[SSI_TX_BUFFER_NUM_SAMPLES + 4]
    __attribute__((aligned(CACHE_LINE_SIZE)));

__attribute__((optimize("unroll-loops"))) void
Oscillator::renderOsc(OscType osc_type, int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                      uint32_t pulse_width, uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment,
                      bool do_osc_sync, uint32_t resetter_phase, uint32_t resetter_phase_increment,
                      uint32_t retrigger_phase, int32_t wave_index_increment, int source_wave_index_last_time,
                      WaveTable* wave_table) {
	switch (osc_type) {
	case OscType::SINE:
		if (do_osc_sync) {
			renderSineSync(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment,
			               resetter_phase, resetter_phase_increment, retrigger_phase);
		}
		else {
			renderSine(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment);
		}
		break;
	case OscType::TRIANGLE:
		if (do_osc_sync) {
			renderTriangleSync(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment,
			                   resetter_phase, resetter_phase_increment, retrigger_phase);
		}
		else {
			renderTriangle(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment);
		}
		break;
	case OscType::SQUARE:
		if (pulse_width == 0) {
			if (do_osc_sync) {
				renderSquareSync(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment,
				                 resetter_phase, resetter_phase_increment, retrigger_phase);
			}
			else {
				renderSquare(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment);
			}
		}
		else {
			if (do_osc_sync) {
				renderPWMSync(amplitude, buffer, phase_increment, pulse_width, start_phase, apply_amplitude,
				              amplitude_increment, resetter_phase, resetter_phase_increment, retrigger_phase);
			}
			else {
				renderPWM(amplitude, buffer, phase_increment, pulse_width, start_phase, apply_amplitude,
				          amplitude_increment);
			}
		}
		break;
	case OscType::SAW:
		if (do_osc_sync) {
			renderSawSync(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment,
			              resetter_phase, resetter_phase_increment, retrigger_phase);
		}
		else {
			renderSaw(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment);
		}
		break;
	case OscType::WAVETABLE:
		renderWavetable(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment,
		                do_osc_sync, resetter_phase, resetter_phase_increment, retrigger_phase, wave_index_increment,
		                source_wave_index_last_time, wave_table);

		break;
	case OscType::ANALOG_SAW_2:
		if (do_osc_sync) {
			renderAnalogSaw2Sync(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment,
			                     resetter_phase, resetter_phase_increment, retrigger_phase);
		}
		else {
			renderAnalogSaw2(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment);
		}
		break;
	case OscType::ANALOG_SQUARE:
		if (pulse_width == 0) {
			if (do_osc_sync) {
				renderAnalogSquareSync(amplitude, buffer, phase_increment, start_phase, apply_amplitude,
				                       amplitude_increment, resetter_phase, resetter_phase_increment, retrigger_phase);
			}
			else {
				renderAnalogSquare(amplitude, buffer, phase_increment, start_phase, apply_amplitude,
				                   amplitude_increment);
			}
		}
		else {
			if (do_osc_sync) {
				renderAnalogPWMSync(amplitude, buffer, phase_increment, pulse_width, start_phase, apply_amplitude,
				                    amplitude_increment, resetter_phase, resetter_phase_increment, retrigger_phase);
			}
			else {
				renderAnalogPWM(amplitude, buffer, phase_increment, pulse_width, start_phase, apply_amplitude,
				                amplitude_increment, retrigger_phase);
			}
		}
		break;
	default:
		// Handle default case if necessary
		break;
	}
}

void Oscillator::renderSine(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                            uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	Pipeline pipeline{
	    TableOscillator(sineWaveSmall, 8),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude, amplitude_increment),
	            UnityMixerProcessor(buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhase(phase, phase_increment);

	pipeline.processBlock(buffer);
}

void Oscillator::renderSineSync(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment,
                                uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	bool do_pulse_wave = false;

	retrigger_phase += 3221225472u;

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    TableOscillator(sineWaveSmall, 8), [](uint32_t) {}, phase, phase_increment, resetter_phase,
	    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	if (!do_pulse_wave) {
		*start_phase = phase;
	}
}

std::pair<const int16_t*, int32_t> getTriangleTable(uint32_t phase_increment) {
	if (phase_increment <= 429496729) {
		if (phase_increment <= 102261126) {
			return {triangleWaveAntiAliasing21, 7};
		}
		else if (phase_increment <= 143165576) {
			return {triangleWaveAntiAliasing15, 7};
		}
		else if (phase_increment <= 238609294) {
			return {triangleWaveAntiAliasing9, 7};
		}
		else {
			return {triangleWaveAntiAliasing5, 7};
		}
	}
	else {
		if (phase_increment <= 715827882) {
			return {triangleWaveAntiAliasing3, 6};
		}
		else {
			return {triangleWaveAntiAliasing1, 6};
		}
	}
}

void Oscillator::renderTriangle(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	if (phase_increment < 69273666 || AudioEngine::cpuDireness >= 7) {
		int32_t amplitude_now = amplitude << 1;
		uint32_t phase_now = phase;
		amplitude_increment <<= 1;
		for (auto& sample : buffer) {
			phase_now += phase_increment;

			int32_t value = getTriangleSmall(phase_now);

			if (apply_amplitude) {
				amplitude_now += amplitude_increment;
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, value, amplitude_now);
			}
			else {
				sample = value << 1;
			}
		}
		return;
	}

	auto [table, table_size_magnitude] = getTriangleTable(phase_increment);
	Pipeline pipeline{
	    TableOscillator(table, table_size_magnitude),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude, amplitude_increment),
	            UnityMixerProcessor(buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhase(phase, phase_increment);

	pipeline.processBlock(buffer);
}

void Oscillator::renderTriangleSync(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                    uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment,
                                    uint32_t resetter_phase, uint32_t resetter_phase_increment,
                                    uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	if (phase_increment < 69273666 || AudioEngine::cpuDireness >= 7) {
		int32_t amplitude_now = amplitude << 1;
		uint32_t phase_now = phase;
		uint32_t resetter_phase_now = resetter_phase;
		amplitude_increment <<= 1;
		for (auto& sample : buffer) {
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
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, value, amplitude_now);
			}
			else {
				sample = value << 1;
			}
		}

		phase = phase_now;
		*start_phase = phase;
		return;
	}

	auto [table, table_size_magnitude] = getTriangleTable(phase_increment);
	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();
	renderOscSync(
	    PWMTableOscillator(table, table_size_magnitude), [](uint32_t) {}, phase, phase_increment, resetter_phase,
	    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	*start_phase = phase;
}

void Oscillator::renderSquare(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                              uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	uint32_t phase_increment_for_calculations = phase_increment;

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment_for_calculations);

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude;
		uint32_t phase_now = phase;

		if (apply_amplitude) {
			for (auto& sample : buffer) {
				phase_now += phase_increment;
				amplitude_now += amplitude_increment;
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, getSquare(phase_now), amplitude_now);
			}
		}
		else {
#pragma GCC unroll 4
			for (auto& sample : buffer) {
				phase_now += phase_increment;
				sample = getSquareSmall(phase_now);
			}
		}
		return;
	}

	amplitude <<= 1;
	amplitude_increment <<= 1;

	Pipeline pipeline{
	    TableOscillator(dsp::squareTables[table_number], table_size_magnitude),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude, amplitude_increment),
	            UnityMixerProcessor(buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhase(phase, phase_increment);

	pipeline.processBlock(buffer);
}

void Oscillator::renderSquareSync(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                  uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment,
                                  uint32_t resetter_phase, uint32_t resetter_phase_increment,
                                  uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	uint32_t phase_increment_for_calculations = phase_increment;

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment_for_calculations);

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude;
		uint32_t phase_now = phase;
		uint32_t resetter_phase_now = resetter_phase;

		for (auto& sample : buffer) {
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
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, getSquare(phase_now), amplitude_now);
			}
			else {
				sample = getSquareSmall(phase_now);
			}
		}

		phase = phase_now;
		*start_phase = phase;
		return;
	}
	const int16_t* table = dsp::squareTables[table_number];

	amplitude <<= 1;
	amplitude_increment <<= 1;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    TableOscillator(table, table_size_magnitude), [](uint32_t) {}, phase, phase_increment, resetter_phase,
	    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	*start_phase = phase;
}

void Oscillator::renderPWM(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment, uint32_t pulse_width,
                           uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	uint32_t phase_increment_for_calculations = phase_increment;

	pulse_width += 2147483648u;
	phase_increment_for_calculations = phase_increment * 0.6;

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment_for_calculations);

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude;
		uint32_t phase_now = phase;

		if (apply_amplitude) {
			for (auto& sample : buffer) {
				phase_now += phase_increment;
				amplitude_now += amplitude_increment;
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, getSquare(phase_now, pulse_width),
				                                                    amplitude_now);
			}
		}
		else {
#pragma GCC unroll 4
			for (auto& sample : buffer) {
				phase_now += phase_increment;
				sample = getSquareSmall(phase_now, pulse_width);
			}
		}
		return;
	}
	const int16_t* table = dsp::squareTables[table_number];

	amplitude <<= 1;
	amplitude_increment <<= 1;

	uint32_t phase_to_add = -(pulse_width >> 1);

	phase >>= 1;
	phase_increment >>= 1;

	Pipeline pipeline{
	    PWMTableOscillator(dsp::squareTables[table_number], table_size_magnitude),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude, amplitude_increment),
	            UnityMixerProcessor(buffer),
	        },
	    },
	};

	std::get<PWMTableOscillator>(pipeline).setPhase(phase, phase_increment);
	std::get<PWMTableOscillator>(pipeline).setPhaseToAdd(phase_to_add);

	pipeline.processBlock(buffer);
}

void Oscillator::renderPWMSync(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                               uint32_t pulse_width, uint32_t* start_phase, bool apply_amplitude,
                               int32_t amplitude_increment, uint32_t resetter_phase, uint32_t resetter_phase_increment,
                               uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	pulse_width += 2147483648u;
	uint32_t phase_increment_for_calculations = phase_increment * 0.6;

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment_for_calculations);

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude;
		uint32_t phase_now = phase;
		uint32_t resetter_phase_now = resetter_phase;

		for (auto& sample : buffer) {
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
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, getSquare(phase_now, pulse_width),
				                                                    amplitude_now);
			}
			else {
				sample = getSquareSmall(phase_now, pulse_width);
			}
		}

		phase = phase_now;
		*start_phase = phase;
		return;
	}

	amplitude <<= 1;
	amplitude_increment <<= 1;

	uint32_t phase_to_add = -(pulse_width >> 1);

	phase >>= 1;
	phase_increment >>= 1;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    PWMTableOscillator(dsp::squareTables[table_number], table_size_magnitude), [](uint32_t) {}, phase,
	    phase_increment, resetter_phase, resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	phase <<= 1;
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	*start_phase = phase;
}

void Oscillator::renderSaw(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                           uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment);

	if (table_number < AudioEngine::cpuDireness + 6) {
		if (apply_amplitude) {
			dsp::renderCrudeSawWave(buffer, phase, phase_increment, amplitude, amplitude_increment);
		}
		else {
			dsp::renderCrudeSawWave(buffer, phase, phase_increment);
		}
		return;
	}

	amplitude <<= 1;
	amplitude_increment <<= 1;

	Pipeline pipeline{
	    TableOscillator(dsp::sawTables[table_number], table_size_magnitude),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude, amplitude_increment),
	            UnityMixerProcessor(buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhase(phase, phase_increment);

	pipeline.processBlock(buffer);
}

void Oscillator::renderSawSync(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                               uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment,
                               uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment);

	retrigger_phase += 2147483648u;

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude;
		uint32_t phase_now = phase;
		uint32_t resetter_phase_now = resetter_phase;

		for (auto& sample : buffer) {
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
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, (int32_t)phase_now, amplitude_now);
			}
			else {
				sample = (int32_t)phase_now >> 1;
			}
		}

		phase = phase_now;
		*start_phase = phase;
		return;
	}

	amplitude <<= 1;
	amplitude_increment <<= 1;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    PWMTableOscillator(dsp::sawTables[table_number], table_size_magnitude), [](uint32_t) {}, phase, phase_increment,
	    resetter_phase, resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	*start_phase = phase;
}

void Oscillator::renderWavetable(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                 uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment,
                                 bool do_osc_sync, uint32_t resetter_phase, uint32_t resetter_phase_increment,
                                 uint32_t retrigger_phase, int32_t wave_index_increment,
                                 int source_wave_index_last_time, WaveTable* wave_table) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	int32_t resetter_divide_by_phase_increment =
	    do_osc_sync ? (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16) : 0;

	int32_t wave_index = source_wave_index_last_time + 1073741824;

	int32_t* wavetable_rendering_buffer = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();

	phase = wave_table->render(wavetable_rendering_buffer, buffer.size(), phase_increment, phase, do_osc_sync,
	                           resetter_phase, resetter_phase_increment, resetter_divide_by_phase_increment,
	                           retrigger_phase, wave_index, wave_index_increment);

	amplitude <<= 3;
	amplitude_increment <<= 3;
	if (apply_amplitude) {
		AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	}
	*start_phase = phase;
}

void Oscillator::renderAnalogSaw2(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                  uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment);

	if (table_number >= 8 && table_number < AudioEngine::cpuDireness + 6) {
		renderSaw(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment);
		return;
	}

	amplitude <<= 1;
	amplitude_increment <<= 1;

	Pipeline pipeline{
	    TableOscillator(dsp::analogSawTables[table_number], table_size_magnitude),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude, amplitude_increment),
	            UnityMixerProcessor(buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhase(phase, phase_increment);

	pipeline.processBlock(buffer);
}

void Oscillator::renderAnalogSaw2Sync(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                      uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment,
                                      uint32_t resetter_phase, uint32_t resetter_phase_increment,
                                      uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment);

	if (table_number >= 8 && table_number < AudioEngine::cpuDireness + 6) {
		renderSawSync(amplitude, buffer, phase_increment, start_phase, apply_amplitude, amplitude_increment,
		              resetter_phase, resetter_phase_increment, retrigger_phase);
		return;
	}

	const int16_t* table = dsp::analogSawTables[table_number];

	amplitude <<= 1;
	amplitude_increment <<= 1;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();
	renderOscSync(
	    TableOscillator(table, table_size_magnitude), [](uint32_t) {}, phase, phase_increment, resetter_phase,
	    resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	*start_phase = phase;
}

void Oscillator::renderAnalogSquare(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                    uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment);

	amplitude <<= 1;
	amplitude_increment <<= 1;

	Pipeline pipeline{
	    TableOscillator(dsp::analogSquareTables[table_number], table_size_magnitude),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude, amplitude_increment),
	            UnityMixerProcessor(buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhase(phase, phase_increment);

	pipeline.processBlock(buffer);
}

void Oscillator::renderAnalogSquareSync(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                        uint32_t* start_phase, bool apply_amplitude, int32_t amplitude_increment,
                                        uint32_t resetter_phase, uint32_t resetter_phase_increment,
                                        uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment);

	amplitude <<= 1;
	amplitude_increment <<= 1;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    TableOscillator(dsp::analogSquareTables[table_number], table_size_magnitude), [](uint32_t) {}, phase,
	    phase_increment, resetter_phase, resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	*start_phase = phase;
}

void Oscillator::renderAnalogPWM(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                 uint32_t pulse_width, uint32_t* start_phase, bool apply_amplitude,
                                 int32_t amplitude_increment, uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment);

	uint32_t pulse_width_absolute = ((int32_t)pulse_width >= 0) ? pulse_width : -pulse_width;

	uint32_t resetter_phase = phase;
	uint32_t resetter_phase_increment = phase_increment;

	int64_t resetter_phase_to_divide = (uint64_t)resetter_phase << 30;

	if ((uint32_t)(resetter_phase) >= (uint32_t)-(resetter_phase_increment >> 1)) {
		resetter_phase_to_divide -= (uint64_t)1 << 62;
	}

	phase = resetter_phase_to_divide / (int32_t)((pulse_width_absolute + 2147483648u) >> 1);
	phase_increment = ((uint64_t)phase_increment << 31) / (pulse_width_absolute + 2147483648u);

	phase += retrigger_phase;

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	amplitude <<= 1;
	amplitude_increment <<= 1;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    PWMTableOscillator(dsp::analogSquareTables[table_number], table_size_magnitude), [](uint32_t) {}, phase,
	    phase_increment, resetter_phase, resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
}

void Oscillator::renderAnalogPWMSync(int32_t amplitude, std::span<int32_t> buffer, uint32_t phase_increment,
                                     uint32_t pulse_width, uint32_t* start_phase, bool apply_amplitude,
                                     int32_t amplitude_increment, uint32_t resetter_phase,
                                     uint32_t resetter_phase_increment, uint32_t retrigger_phase) {
	uint32_t phase = *start_phase;
	*start_phase += phase_increment * buffer.size();

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment);

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter_phase_increment + 65535) >> 16);

	amplitude <<= 1;
	amplitude_increment <<= 1;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    PWMTableOscillator(dsp::analogSquareTables[table_number], table_size_magnitude), [](uint32_t) {}, phase,
	    phase_increment, resetter_phase, resetter_phase_increment, resetter_divide_by_phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude, amplitude_increment).processBlock(oscSyncRenderingBuffer, buffer);
	*start_phase = phase;
}
} // namespace deluge::dsp
