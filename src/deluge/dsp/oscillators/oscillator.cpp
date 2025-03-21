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
#include "classic/oscillator.h"
#include "classic/simple_pulse.h"
#include "classic/table_oscillator.h"
#include "definitions_cxx.hpp"
#include "dsp/core/conditional/processor.h"
#include "dsp/core/pipeline.h"
#include "dsp/oscillators/basic_waves.h"
#include "dsp/processors/amplitude.h"
#include "dsp/processors/gain.h"
#include "dsp/stereo_sample.h"
#include "dsp/waves.h"
#include "processing/engines/audio_engine.h"
#include "render_wave.h"
#include "storage/wave_table/wave_table.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include <cstdint>

namespace deluge::dsp {
PLACE_INTERNAL_FRUNK int32_t oscSyncRenderingBuffer[SSI_TX_BUFFER_NUM_SAMPLES + 4]
    __attribute__((aligned(CACHE_LINE_SIZE)));

__attribute__((optimize("unroll-loops"))) //
uint32_t
Oscillator::renderOsc(OscType osc_type, bool do_osc_sync, std::span<int32_t> buffer, PhasorPair<uint32_t> osc,
                      uint32_t pulse_width, bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
                      PhasorPair<uint32_t> resetter, uint32_t retrigger_phase, int32_t wave_index_increment,
                      int source_wave_index_last_time, WaveTable* wave_table) {
	switch (osc_type) {
	case OscType::SINE:
		if (do_osc_sync) {
			return renderSineSync(buffer, osc, apply_amplitude, amplitude, resetter, retrigger_phase);
		}
		else {
			renderSine(buffer, osc, apply_amplitude, amplitude);
		}
		break;
	case OscType::TRIANGLE:
		if (do_osc_sync) {
			return renderTriangleSync(buffer, osc, apply_amplitude, amplitude, resetter, retrigger_phase);
		}
		else {
			renderTriangle(buffer, osc, apply_amplitude, amplitude);
		}
		break;
	case OscType::SQUARE:
		if (pulse_width == 0) {
			if (do_osc_sync) {
				return renderSquareSync(buffer, osc, apply_amplitude, amplitude, resetter, retrigger_phase);
			}
			else {
				renderSquare(buffer, osc, apply_amplitude, amplitude);
			}
		}
		else {
			if (do_osc_sync) {
				return renderPWMSync(buffer, osc, pulse_width, apply_amplitude, amplitude, resetter, retrigger_phase);
			}
			else {
				renderPWM(buffer, osc, pulse_width, apply_amplitude, amplitude);
			}
		}
		break;
	case OscType::SAW:
		if (do_osc_sync) {
			return renderSawSync(buffer, osc, apply_amplitude, amplitude, resetter, retrigger_phase);
		}
		else {
			renderSaw(buffer, osc, apply_amplitude, amplitude);
		}
		break;
	case OscType::WAVETABLE:
		return renderWavetable(buffer, osc, apply_amplitude, amplitude, do_osc_sync, resetter, retrigger_phase,
		                       wave_index_increment, source_wave_index_last_time, wave_table);

	case OscType::ANALOG_SAW_2:
		if (do_osc_sync) {
			return renderAnalogSaw2Sync(buffer, osc, apply_amplitude, amplitude, resetter, retrigger_phase);
		}
		else {
			renderAnalogSaw2(buffer, osc, apply_amplitude, amplitude);
		}
		break;
	case OscType::ANALOG_SQUARE:
		if (pulse_width == 0) {
			if (do_osc_sync) {
				return renderAnalogSquareSync(buffer, osc, apply_amplitude, amplitude, resetter, retrigger_phase);
			}
			else {
				renderAnalogSquare(buffer, osc, apply_amplitude, amplitude);
			}
		}
		else {
			if (do_osc_sync) {
				return renderAnalogPWMSync(buffer, osc, pulse_width, apply_amplitude, amplitude, resetter,
				                           retrigger_phase);
			}
			else {
				renderAnalogPWM(buffer, osc, pulse_width, apply_amplitude, amplitude, retrigger_phase);
			}
		}
		break;
	default:
		// Handle default case if necessary
		break;
	}
	return osc.phase + (osc.phase_increment * buffer.size());
}

void Oscillator::renderSine(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                            PhasorPair<FixedPoint<30>> amplitude) {
	amplitude.phase = amplitude.phase.DivideInt(2);
	amplitude.phase_increment = amplitude.phase_increment.DivideInt(2);
	Pipeline pipeline{
	    TableOscillator(sineWaveSmall, 8),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude.phase, amplitude.phase_increment),
	            GainMixerProcessor(FixedPoint<31>(0.5), buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhaseAndIncrement(osc.phase, osc.phase_increment);

	pipeline.renderBlock(buffer);
}

uint32_t Oscillator::renderSineSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                    PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                    uint32_t retrigger_phase) {

	retrigger_phase += 3221225472u;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();

	renderOscSync(
	    TableOscillator(sineWaveSmall, 8), [](uint32_t) {}, osc.phase, osc.phase_increment, resetter.phase,
	    resetter.phase_increment, retrigger_phase, buffer.size(), buffer_start_this_sync);
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

std::pair<const int16_t*, int32_t> getTriangleTable(uint32_t phase_increment) {
	if (phase_increment <= 102261126) {
		return {triangleWaveAntiAliasing21, 7};
	}
	if (phase_increment <= 143165576) {
		return {triangleWaveAntiAliasing15, 7};
	}
	if (phase_increment <= 238609294) {
		return {triangleWaveAntiAliasing9, 7};
	}
	if (phase_increment <= 429496729) {
		return {triangleWaveAntiAliasing5, 7};
	}
	if (phase_increment <= 715827882) {
		return {triangleWaveAntiAliasing3, 6};
	}
	return {triangleWaveAntiAliasing1, 6};
}

void Oscillator::renderTriangle(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                PhasorPair<FixedPoint<30>> amplitude) {
	bool fast_render = (osc.phase_increment < 69273666 || AudioEngine::cpuDireness >= 7);

	auto simple_triangle = SimpleOscillatorFor(&deluge::dsp::waves::triangle);
	auto [table, table_size_magnitude] = getTriangleTable(osc.phase_increment);
	TableOscillator fancy_triangle(table, table_size_magnitude);
	auto& oscillator = fast_render ? static_cast<ClassicOscillator&>(simple_triangle)
	                               : static_cast<ClassicOscillator&>(fancy_triangle);
	Pipeline pipeline{
	    &oscillator,
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude.phase, amplitude.phase_increment),
	            GainMixerProcessor(FixedPoint<31>(0.5), buffer),
	        },
	    },
	};

	std::get<ClassicOscillator*>(pipeline)->setPhaseAndIncrement(osc.phase, osc.phase_increment);

	pipeline.renderBlock(buffer);
}

uint32_t Oscillator::renderTriangleSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                        PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                        uint32_t retrigger_phase) {
	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16);

	if (osc.phase_increment < 69273666 || AudioEngine::cpuDireness >= 7) {
		int32_t amplitude_now = amplitude.phase.raw() << 1;
		uint32_t phase_now = osc.phase;
		uint32_t resetter_phase_now = resetter.phase;
		for (auto& sample : buffer) {
			phase_now += osc.phase_increment;
			resetter_phase_now += resetter.phase_increment;

			if (resetter_phase_now < resetter.phase_increment) {
				phase_now = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetter_phase_now, osc.phase_increment),
				                                     resetter_divide_by_phase_increment)
				             << 17)
				            + 1 + retrigger_phase;
			}

			int32_t value = getTriangleSmall(phase_now);

			if (apply_amplitude) {
				amplitude_now += amplitude.phase_increment.raw() << 1;
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, value, amplitude_now);
			}
			else {
				sample = value << 1;
			}
		}

		return phase_now;
	}

	auto [table, table_size_magnitude] = getTriangleTable(osc.phase_increment);
	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();
	renderOscSync(
	    PWMTableOscillator(table, table_size_magnitude), [](uint32_t) {}, osc.phase, osc.phase_increment,
	    resetter.phase, resetter.phase_increment, retrigger_phase, num_samples_this_osc_sync_session,
	    buffer_start_this_sync);
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderSquare(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                              PhasorPair<FixedPoint<30>> amplitude) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	bool fast_render = (table_number < AudioEngine::cpuDireness + 6);

	auto simple_square = SimpleOscillatorFor(&deluge::dsp::waves::square);
	auto table_square = TableOscillator(dsp::squareTables[table_number], table_size_magnitude);
	auto& oscillator =
	    fast_render ? static_cast<ClassicOscillator&>(simple_square) : static_cast<ClassicOscillator&>(table_square);

	Pipeline pipeline{
	    &oscillator,
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude.phase, amplitude.phase_increment),
	            GainMixerProcessor(FixedPoint<31>(0.5), buffer),
	        },
	    },
	};

	std::get<ClassicOscillator*>(pipeline)->setPhaseAndIncrement(osc.phase, osc.phase_increment);

	pipeline.renderBlock(buffer);
}

uint32_t Oscillator::renderSquareSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                      PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                      uint32_t retrigger_phase) {

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16);

	uint32_t phase_increment_for_calculations = osc.phase_increment;

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment_for_calculations);

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude.phase.raw();
		uint32_t phase_now = osc.phase;
		uint32_t resetter_phase_now = resetter.phase;

		for (auto& sample : buffer) {
			phase_now += osc.phase_increment;
			resetter_phase_now += resetter.phase_increment;

			if (resetter_phase_now < resetter.phase_increment) {
				phase_now = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetter_phase_now, osc.phase_increment),
				                                     resetter_divide_by_phase_increment)
				             << 17)
				            + 1 + retrigger_phase;
			}

			if (apply_amplitude) {
				amplitude_now += amplitude.phase_increment.raw();
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, getSquare(phase_now), amplitude_now);
			}
			else {
				sample = getSquareSmall(phase_now);
			}
		}

		return phase_now;
	}
	const int16_t* table = dsp::squareTables[table_number];

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    TableOscillator(table, table_size_magnitude), [](uint32_t) {}, osc.phase, osc.phase_increment, resetter.phase,
	    resetter.phase_increment, retrigger_phase, num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderPWM(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
                           bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment * 0.6);

	bool fast_render = (table_number < AudioEngine::cpuDireness + 6);

	SimplePulseOscillator simple_pulse{};
	PWMTableOscillator table_pulse(dsp::squareTables[table_number], table_size_magnitude);
	auto& oscillator =
	    fast_render ? static_cast<PWMOscillator&>(simple_pulse) : static_cast<PWMOscillator&>(table_pulse);

	Pipeline pipeline{
	    &oscillator,
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude.phase, amplitude.phase_increment),
	            GainMixerProcessor(FixedPoint<31>(0.5), buffer),
	        },
	    },
	};

	/// @stellar-aria: For some reason the phase and phase_increment need to be halved for the table rendering to work
	/// correctly
	if (!fast_render) {
		osc.phase >>= 1;
		osc.phase_increment >>= 1;
	}

	// @stellar-aria: Both oscillator types are derived from ClassicOscillator, so we can safely reinterpret_cast
	// the pointer to ClassicOscillator and call setPhaseAndIncrement on it
	reinterpret_cast<ClassicOscillator*>(std::get<PWMOscillator*>(pipeline))
	    ->setPhaseAndIncrement(osc.phase, osc.phase_increment);
	std::get<PWMOscillator*>(pipeline)->setPulseWidth(pulse_width + FixedPoint<31>::one());

	pipeline.renderBlock(buffer);
}

uint32_t Oscillator::renderPWMSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
                                   bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
                                   PhasorPair<uint32_t> resetter, uint32_t retrigger_phase) {

	bool do_pulse_wave = false;

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16);

	pulse_width += 2147483648u;
	uint32_t phase_increment_for_calculations = osc.phase_increment * 0.6;

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(phase_increment_for_calculations);

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude.phase.raw();
		uint32_t phase_now = osc.phase;
		uint32_t resetter_phase_now = resetter.phase;

		for (auto& sample : buffer) {
			phase_now += osc.phase_increment;
			resetter_phase_now += resetter.phase_increment;

			if (resetter_phase_now < resetter.phase_increment) {
				phase_now = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetter_phase_now, osc.phase_increment),
				                                     resetter_divide_by_phase_increment)
				             << 17)
				            + 1 + retrigger_phase;
			}

			if (apply_amplitude) {
				amplitude_now += amplitude.phase_increment.raw();
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, getSquare(phase_now, pulse_width),
				                                                    amplitude_now);
			}
			else {
				sample = getSquareSmall(phase_now, pulse_width);
			}
		}

		return phase_now;
	}

	uint32_t phase_to_add = -(pulse_width >> 1);

	osc.phase >>= 1;
	osc.phase_increment >>= 1;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    PWMTableOscillator(dsp::squareTables[table_number], table_size_magnitude), [](uint32_t) {}, osc.phase,
	    osc.phase_increment, resetter.phase, resetter.phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	osc.phase <<= 1;
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderSaw(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                           PhasorPair<FixedPoint<30>> amplitude) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	auto simple_saw = SimpleOscillatorFor(&deluge::dsp::waves::saw);
	TableOscillator proper_saw(dsp::sawTables[table_number], table_size_magnitude);

	ClassicOscillator& oscillator = (table_number < AudioEngine::cpuDireness + 6)
	                                    ? static_cast<ClassicOscillator&>(simple_saw)
	                                    : static_cast<ClassicOscillator&>(proper_saw);

	Pipeline pipeline{
	    &oscillator,
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude.phase, amplitude.phase_increment),
	            GainMixerProcessor(FixedPoint<31>(0.5), buffer),
	        },
	    },
	};

	std::get<ClassicOscillator*>(pipeline)->setPhaseAndIncrement(osc.phase, osc.phase_increment);

	pipeline.renderBlock(buffer);
}

uint32_t Oscillator::renderSawSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                   PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                   uint32_t retrigger_phase) {

	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16);

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	retrigger_phase += 2147483648u;

	if (table_number < AudioEngine::cpuDireness + 6) {
		int32_t amplitude_now = amplitude.phase.raw();
		uint32_t phase_now = osc.phase;
		uint32_t resetter_phase_now = resetter.phase;

		for (auto& sample : buffer) {
			phase_now += osc.phase_increment;
			resetter_phase_now += resetter.phase_increment;

			if (resetter_phase_now < resetter.phase_increment) {
				phase_now = (multiply_32x32_rshift32(multiply_32x32_rshift32(resetter_phase_now, osc.phase_increment),
				                                     resetter_divide_by_phase_increment)
				             << 17)
				            + 1 + retrigger_phase;
			}

			if (apply_amplitude) {
				amplitude_now += amplitude.phase_increment.raw();
				sample = multiply_accumulate_32x32_rshift32_rounded(sample, (int32_t)phase_now, amplitude_now);
			}
			else {
				sample = (int32_t)phase_now >> 1;
			}
		}

		return phase_now;
	}

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    PWMTableOscillator(dsp::sawTables[table_number], table_size_magnitude), [](uint32_t) {}, osc.phase,
	    osc.phase_increment, resetter.phase, resetter.phase_increment, retrigger_phase,
	    num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

uint32_t Oscillator::renderWavetable(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                     PhasorPair<FixedPoint<30>> amplitude, bool do_osc_sync,
                                     PhasorPair<uint32_t> resetter, uint32_t retrigger_phase,
                                     int32_t wave_index_increment, int source_wave_index_last_time,
                                     WaveTable* wave_table) {

	int32_t resetter_divide_by_phase_increment =
	    do_osc_sync ? (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16) : 0;

	int32_t wave_index = source_wave_index_last_time + 1073741824;

	int32_t* wavetable_rendering_buffer = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();

	osc.phase =
	    wave_table->render(wavetable_rendering_buffer, buffer.size(), osc.phase_increment, osc.phase, do_osc_sync,
	                       resetter.phase, resetter.phase_increment, resetter_divide_by_phase_increment,
	                       retrigger_phase, wave_index, wave_index_increment);

	if (apply_amplitude) {
		AmplitudeProcessor(amplitude.phase.MultiplyInt(4), amplitude.phase_increment.MultiplyInt(4))
		    .renderBlock(oscSyncRenderingBuffer, buffer);
	}
	return osc.phase;
}

void Oscillator::renderAnalogSaw2(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                  PhasorPair<FixedPoint<30>> amplitude) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	if (table_number >= 8 && table_number < AudioEngine::cpuDireness + 6) {
		renderSaw(buffer, osc, apply_amplitude, amplitude);
		return;
	}

	Pipeline pipeline{
	    TableOscillator(dsp::analogSawTables[table_number], table_size_magnitude),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude.phase, amplitude.phase_increment),
	            GainMixerProcessor(FixedPoint<31>(0.5), buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhaseAndIncrement(osc.phase, osc.phase_increment);

	pipeline.renderBlock(buffer);
}

uint32_t Oscillator::renderAnalogSaw2Sync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                          PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                          uint32_t retrigger_phase) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	if (table_number >= 8 && table_number < AudioEngine::cpuDireness + 6) {
		return renderSawSync(buffer, osc, apply_amplitude, amplitude, resetter, retrigger_phase);
	}

	const int16_t* table = dsp::analogSawTables[table_number];

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();
	renderOscSync(
	    TableOscillator(table, table_size_magnitude), [](uint32_t) {}, osc.phase, osc.phase_increment, resetter.phase,
	    resetter.phase_increment, retrigger_phase, buffer.size(), buffer_start_this_sync);
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderAnalogSquare(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                    PhasorPair<FixedPoint<30>> amplitude) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	Pipeline pipeline{
	    TableOscillator(dsp::analogSquareTables[table_number], table_size_magnitude),
	    ConditionalProcessor{
	        apply_amplitude,
	        Pipeline{
	            AmplitudeProcessor(amplitude.phase, amplitude.phase_increment),
	            GainMixerProcessor(FixedPoint<31>(0.5), buffer),
	        },
	    },
	};

	std::get<TableOscillator>(pipeline).setPhaseAndIncrement(osc.phase, osc.phase_increment);

	pipeline.renderBlock(buffer);
}

uint32_t Oscillator::renderAnalogSquareSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                            PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                            uint32_t retrigger_phase) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();

	renderOscSync(
	    TableOscillator(dsp::analogSquareTables[table_number], table_size_magnitude), [](uint32_t) {}, osc.phase,
	    osc.phase_increment, resetter.phase, resetter.phase_increment, retrigger_phase, buffer.size(),
	    buffer_start_this_sync);
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderAnalogPWM(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
                                 bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude, uint32_t retrigger_phase) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	uint32_t pulse_width_absolute = ((int32_t)pulse_width >= 0) ? pulse_width : -pulse_width;

	PhasorPair<uint32_t> resetter = {
	    .phase = osc.phase,
	    .phase_increment = osc.phase_increment,
	};

	int64_t resetter_phase_to_divide = (uint64_t)resetter.phase << 30;

	if ((uint32_t)(resetter.phase) >= (uint32_t)-(resetter.phase_increment >> 1)) {
		resetter_phase_to_divide -= (uint64_t)1 << 62;
	}

	osc.phase = resetter_phase_to_divide / (int32_t)((pulse_width_absolute + 2147483648u) >> 1);
	osc.phase_increment = ((uint64_t)osc.phase_increment << 31) / (pulse_width_absolute + 2147483648u);

	osc.phase += retrigger_phase;

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();

	renderOscSync(
	    PWMTableOscillator(dsp::analogSquareTables[table_number], table_size_magnitude), [](uint32_t) {}, osc.phase,
	    osc.phase_increment, resetter.phase, resetter.phase_increment, retrigger_phase, buffer.size(),
	    buffer_start_this_sync);
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
}

uint32_t Oscillator::renderAnalogPWMSync(std::span<int32_t> buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
                                         bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
                                         PhasorPair<uint32_t> resetter, uint32_t retrigger_phase) {

	const auto [table_number, table_size_magnitude] = dsp::getTableNumber(osc.phase_increment);

	int32_t* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer : buffer.data();

	renderOscSync(
	    PWMTableOscillator(dsp::analogSquareTables[table_number], table_size_magnitude), [](uint32_t) {}, osc.phase,
	    osc.phase_increment, resetter.phase, resetter.phase_increment, retrigger_phase, buffer.size(),
	    buffer_start_this_sync);
	AmplitudeProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}
} // namespace deluge::dsp
