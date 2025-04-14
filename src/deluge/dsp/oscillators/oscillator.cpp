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
#include "dsp/oscillators/basic_waves.h"
#include "dsp/stereo_sample.h"
#include "dsp_ng/components/processors/amplitude.hpp"
#include "dsp_ng/components/processors/gain.hpp"
#include "dsp_ng/components/waves.hpp"
#include "dsp_ng/core/sequence.hpp"
#include "dsp_ng/oscillators/legacy/oscillator.hpp"
#include "dsp_ng/oscillators/legacy/simple_pulse.hpp"
#include "dsp_ng/oscillators/legacy/sine.hpp"
#include "dsp_ng/oscillators/legacy/table_oscillator.hpp"
#include "dsp_ng/oscillators/legacy/triangle.hpp"
#include "processing/engines/audio_engine.h"
#include "render_wave.h"
#include "storage/wave_table/wave_table.h"
#include "util/fixedpoint.h"
#include "util/lookuptables/lookuptables.h"
#include <cstdint>

using namespace deluge::dsp::oscillators;
using namespace deluge::dsp::processors;

namespace deluge::dsp {
alignas(CACHE_LINE_SIZE)
    PLACE_INTERNAL_FRUNK std::array<FixedPoint<31>, SSI_TX_BUFFER_NUM_SAMPLES + 4> oscSyncRenderingBuffer;

__attribute__((optimize("unroll-loops"))) //
uint32_t
Oscillator::renderOsc(OscType osc_type, bool do_osc_sync, std::span<int32_t> q31_buffer, PhasorPair<uint32_t> osc,
                      uint32_t pulse_width, bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
                      PhasorPair<uint32_t> resetter, uint32_t retrigger_phase, int32_t wave_index_increment,
                      int source_wave_index_last_time, WaveTable* wave_table) {
	std::span<FixedPoint<31>> buffer{reinterpret_cast<FixedPoint<31>*>(q31_buffer.data()), q31_buffer.size()};

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

void Oscillator::renderSine(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                            PhasorPair<FixedPoint<30>> amplitude) {
	amplitude.phase = amplitude.phase.DivideInt(2);
	amplitude.phase_increment = amplitude.phase_increment.DivideInt(2);

	Sine sine{};
	sine.setPhaseIncrement(osc.phase_increment);
	sine.setPhase(osc.phase);

	if (apply_amplitude) {
		Sequence{
		    &sine,
		    AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment),
		    GainMixerProcessor(FixedPoint<31>(0.5), buffer),
		}
		    .renderBlock(buffer);
	}
	else {
		sine.renderBlock(buffer);
	}
}

uint32_t Oscillator::renderSineSync(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                    PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                    uint32_t retrigger_phase) {
	retrigger_phase += 3221225472u;

	auto* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();

	renderOscSync(
	    Sine{}, [](uint32_t) {}, osc.phase, osc.phase_increment, resetter.phase, resetter.phase_increment,
	    retrigger_phase, buffer.size(), buffer_start_this_sync);
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderTriangle(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                PhasorPair<FixedPoint<30>> amplitude) {
	bool fast_render = (osc.phase_increment < 69273666 || AudioEngine::cpuDireness >= 7);

	if (fast_render) {
		int32_t amplitude_now = amplitude.phase.raw() << 1;
		uint32_t phase_now = osc.phase;
		for (auto& sample : buffer) {
			phase_now += osc.phase_increment;

			int32_t value = getTriangleSmall(phase_now);

			if (apply_amplitude) {
				amplitude_now += amplitude.phase_increment.raw() << 1;
				sample = fixed_point::Sample::from_raw(
				    multiply_accumulate_32x32_rshift32_rounded(sample.raw(), value, amplitude_now));
			}
			else {
				sample = fixed_point::Sample::from_raw(value << 1);
			}
		}
		return;
	}

	Triangle triangle{};
	triangle.setPhaseIncrement(osc.phase_increment);
	triangle.setPhase(osc.phase);

	if (apply_amplitude) {
		Sequence{
		    Triangle{},
		    AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment),
		    GainMixerProcessor(FixedPoint<31>(0.5), buffer),
		}
		    .renderBlock(buffer);
	}
	else {
		triangle.renderBlock(buffer);
	}
}

uint32_t Oscillator::renderTriangleSync(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
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
				sample = fixed_point::Sample::from_raw(
				    multiply_accumulate_32x32_rshift32_rounded(sample.raw(), value, amplitude_now));
			}
			else {
				sample = fixed_point::Sample::from_raw(value << 1);
			}
		}

		return phase_now;
	}

	auto* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();
	renderOscSync(
	    Triangle{}, [](uint32_t) {}, osc.phase, osc.phase_increment, resetter.phase, resetter.phase_increment,
	    retrigger_phase, num_samples_this_osc_sync_session, buffer_start_this_sync);
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderSquare(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                              PhasorPair<FixedPoint<30>> amplitude) {
	const auto table_number = dsp::getTableNumber(osc.phase_increment);

	bool fast_render = (table_number < AudioEngine::cpuDireness + 6);
	if (fast_render) {
		int32_t amplitude_now = amplitude.phase.raw();
		uint32_t phase_now = osc.phase;

		if (apply_amplitude) {
#pragma GCC unroll 4
			for (auto& sample : buffer) {
				phase_now += osc.phase_increment;
				amplitude_now += amplitude.phase_increment.raw();
				sample = fixed_point::Sample::from_raw(
				    multiply_accumulate_32x32_rshift32_rounded(sample.raw(), getSquare(phase_now), amplitude_now));
			}
		}
		else {
#pragma GCC unroll 4
			for (auto& sample : buffer) {
				phase_now += osc.phase_increment;
				sample = fixed_point::Sample::from_raw(getSquareSmall(phase_now));
			}
		}
		return;
	}

	TableOscillator oscillator{dsp::squareTables[table_number]};
	oscillator.setPhaseIncrement(osc.phase_increment);
	oscillator.setPhase(osc.phase);

	if (apply_amplitude) {
		Sequence{
		    &oscillator,
		    AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment),
		    GainMixerProcessor(FixedPoint<31>(0.5), buffer),
		}
		    .renderBlock(buffer);
	}
	else {
		oscillator.renderBlock(buffer);
	}
}

uint32_t Oscillator::renderSquareSync(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                      PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                      uint32_t retrigger_phase) {
	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16);

	uint32_t phase_increment_for_calculations = osc.phase_increment;

	const size_t table_number = dsp::getTableNumber(phase_increment_for_calculations);

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
				sample = fixed_point::Sample::from_raw(
				    multiply_accumulate_32x32_rshift32_rounded(sample.raw(), getSquare(phase_now), amplitude_now));
			}
			else {
				sample = fixed_point::Sample::from_raw(getSquareSmall(phase_now));
			}
		}

		return phase_now;
	}

	auto* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    TableOscillator(dsp::squareTables[table_number]), [](uint32_t) {}, osc.phase, osc.phase_increment,
	    resetter.phase, resetter.phase_increment, retrigger_phase, num_samples_this_osc_sync_session,
	    buffer_start_this_sync);
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderPWM(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
                           bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude) {
	const size_t table_number = dsp::getTableNumber(osc.phase_increment * 0.6);

	bool fast_render = (table_number < AudioEngine::cpuDireness + 6);

	if (fast_render) {
		SimplePulseOscillator simple_pulse{};
		simple_pulse.setPhaseIncrement(osc.phase_increment);
		simple_pulse.setPhase(osc.phase);
		simple_pulse.setPulseWidth(pulse_width + FixedPoint<31>::one());

		if (apply_amplitude) {
			Sequence{
			    &simple_pulse,
			    AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment),
			    GainMixerProcessor(FixedPoint<31>(0.5), buffer),
			}
			    .renderBlock(buffer);
		}
		else {
			simple_pulse.renderBlock(buffer);
		}
		return;
	}

	PWMTableOscillator table_pulse(dsp::squareTables[table_number]);
	/// @stellar-aria: For some reason the phase and phase_increment need to be halved for the table rendering to work
	/// correctly
	table_pulse.setPhaseIncrement(osc.phase_increment >> 1);
	table_pulse.setPhase(osc.phase >> 1);
	table_pulse.setPulseWidth(pulse_width + FixedPoint<31>::one());

	if (apply_amplitude) {
		Sequence{
		    &table_pulse,
		    AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment),
		    GainMixerProcessor(FixedPoint<31>(0.5), buffer),
		}
		    .renderBlock(buffer);
	}
	else {
		table_pulse.renderBlock(buffer);
	}
}

uint32_t Oscillator::renderPWMSync(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
                                   bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
                                   PhasorPair<uint32_t> resetter, uint32_t retrigger_phase) {
	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16);

	pulse_width += 2147483648u;
	uint32_t phase_increment_for_calculations = osc.phase_increment * 0.6;

	const auto table_number = dsp::getTableNumber(phase_increment_for_calculations);

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
				sample = fixed_point::Sample::from_raw(multiply_accumulate_32x32_rshift32_rounded(
				    sample.raw(), getSquare(phase_now, pulse_width), amplitude_now));
			}
			else {
				sample = fixed_point::Sample::from_raw(getSquareSmall(phase_now, pulse_width));
			}
		}

		return phase_now;
	}

	osc.phase >>= 1;
	osc.phase_increment >>= 1;

	auto* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    PWMTableOscillator(dsp::squareTables[table_number]), [](uint32_t) {}, osc.phase, osc.phase_increment,
	    resetter.phase, resetter.phase_increment, retrigger_phase, num_samples_this_osc_sync_session,
	    buffer_start_this_sync);
	osc.phase <<= 1;
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderSaw(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                           PhasorPair<FixedPoint<30>> amplitude) {
	const size_t table_number = dsp::getTableNumber(osc.phase_increment);

	auto simple_saw = SimpleOscillatorFor(&deluge::dsp::waves::saw);
	TableOscillator proper_saw(dsp::sawTables[table_number]);

	LegacyOscillator& oscillator = (table_number < AudioEngine::cpuDireness + 6)
	                                   ? static_cast<LegacyOscillator&>(simple_saw)
	                                   : static_cast<LegacyOscillator&>(proper_saw);

	oscillator.setPhaseIncrement(osc.phase_increment);
	oscillator.setPhase(osc.phase);

	if (apply_amplitude) {
		Sequence{
		    &oscillator,
		    AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment),
		    GainMixerProcessor(FixedPoint<31>(0.5), buffer),
		}
		    .renderBlock(buffer);
	}
	else {
		oscillator.renderBlock(buffer);
	}
}

uint32_t Oscillator::renderSawSync(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                   PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                   uint32_t retrigger_phase) {
	int32_t resetter_divide_by_phase_increment =
	    (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16);

	const size_t table_number = dsp::getTableNumber(osc.phase_increment);

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
				sample = fixed_point::Sample::from_raw(
				    multiply_accumulate_32x32_rshift32_rounded(sample.raw(), (int32_t)phase_now, amplitude_now));
			}
			else {
				sample = fixed_point::Sample::from_raw((int32_t)phase_now >> 1);
			}
		}

		return phase_now;
	}

	auto* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();
	int32_t num_samples_this_osc_sync_session = buffer.size();

	renderOscSync(
	    PWMTableOscillator(dsp::sawTables[table_number]), [](uint32_t) {}, osc.phase, osc.phase_increment,
	    resetter.phase, resetter.phase_increment, retrigger_phase, num_samples_this_osc_sync_session,
	    buffer_start_this_sync);
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

uint32_t Oscillator::renderWavetable(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                     PhasorPair<FixedPoint<30>> amplitude, bool do_osc_sync,
                                     PhasorPair<uint32_t> resetter, uint32_t retrigger_phase,
                                     int32_t wave_index_increment, int source_wave_index_last_time,
                                     WaveTable* wave_table) {
	int32_t resetter_divide_by_phase_increment =
	    do_osc_sync ? (uint32_t)2147483648u / (uint16_t)((resetter.phase_increment + 65535) >> 16) : 0;

	int32_t wave_index = source_wave_index_last_time + 1073741824;

	auto* wavetable_rendering_buffer = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();

	osc.phase =
	    wave_table->render((q31_t*)wavetable_rendering_buffer, buffer.size(), osc.phase_increment, osc.phase,
	                       do_osc_sync, resetter.phase, resetter.phase_increment, resetter_divide_by_phase_increment,
	                       retrigger_phase, wave_index, wave_index_increment);

	if (apply_amplitude) {
		AmplitudeStepProcessor(amplitude.phase.MultiplyInt(4), amplitude.phase_increment.MultiplyInt(4))
		    .renderBlock(oscSyncRenderingBuffer, buffer);
	}
	return osc.phase;
}

void Oscillator::renderAnalogSaw2(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                  PhasorPair<FixedPoint<30>> amplitude) {
	const size_t table_number = dsp::getTableNumber(osc.phase_increment);

	if (table_number >= 8 && table_number < AudioEngine::cpuDireness + 6) {
		renderSaw(buffer, osc, apply_amplitude, amplitude);
		return;
	}

	TableOscillator saw{dsp::analogSawTables[table_number]};
	saw.setPhaseIncrement(osc.phase_increment);
	saw.setPhase(osc.phase);

	if (apply_amplitude) {
		Sequence{
		    &saw,
		    AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment),
		    GainMixerProcessor(FixedPoint<31>(0.5), buffer),
		}
		    .renderBlock(buffer);
	}
	else {
		saw.renderBlock(buffer);
	}
}

uint32_t Oscillator::renderAnalogSaw2Sync(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                          PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                          uint32_t retrigger_phase) {
	const size_t table_number = dsp::getTableNumber(osc.phase_increment);

	if (table_number >= 8 && table_number < AudioEngine::cpuDireness + 6) {
		return renderSawSync(buffer, osc, apply_amplitude, amplitude, resetter, retrigger_phase);
	}

	auto* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();
	renderOscSync(
	    TableOscillator(dsp::analogSawTables[table_number]), [](uint32_t) {}, osc.phase, osc.phase_increment,
	    resetter.phase, resetter.phase_increment, retrigger_phase, buffer.size(), buffer_start_this_sync);
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderAnalogSquare(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                    PhasorPair<FixedPoint<30>> amplitude) {

	const size_t table_number = dsp::getTableNumber(osc.phase_increment);

	TableOscillator table_oscillator(dsp::analogSquareTables[table_number]);
	table_oscillator.setPhaseIncrement(osc.phase_increment);
	table_oscillator.setPhase(osc.phase);

	if (apply_amplitude) {
		Sequence{
		    &table_oscillator,
		    AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment),
		    GainMixerProcessor(FixedPoint<31>(0.5), buffer),
		}
		    .renderBlock(buffer);
	}
	else {
		table_oscillator.renderBlock(buffer);
	}
}

uint32_t Oscillator::renderAnalogSquareSync(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, bool apply_amplitude,
                                            PhasorPair<FixedPoint<30>> amplitude, PhasorPair<uint32_t> resetter,
                                            uint32_t retrigger_phase) {
	FixedPoint<31>* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();

	const size_t table_number = dsp::getTableNumber(osc.phase_increment);

	renderOscSync(
	    TableOscillator(dsp::analogSquareTables[table_number]), [](uint32_t) {}, osc.phase, osc.phase_increment,
	    resetter.phase, resetter.phase_increment, retrigger_phase, buffer.size(), buffer_start_this_sync);
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}

void Oscillator::renderAnalogPWM(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
                                 bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude, uint32_t retrigger_phase) {

	const size_t table_number = dsp::getTableNumber(osc.phase_increment);

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

	FixedPoint<31>* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();

	renderOscSync(
	    PWMTableOscillator(dsp::analogSquareTables[table_number]), [](uint32_t) {}, osc.phase, osc.phase_increment,
	    resetter.phase, resetter.phase_increment, retrigger_phase, buffer.size(), buffer_start_this_sync);
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
}

uint32_t Oscillator::renderAnalogPWMSync(fixed_point::Buffer buffer, PhasorPair<uint32_t> osc, uint32_t pulse_width,
                                         bool apply_amplitude, PhasorPair<FixedPoint<30>> amplitude,
                                         PhasorPair<uint32_t> resetter, uint32_t retrigger_phase) {
	FixedPoint<31>* buffer_start_this_sync = apply_amplitude ? oscSyncRenderingBuffer.data() : buffer.data();

	const size_t table_number = dsp::getTableNumber(osc.phase_increment);

	renderOscSync(
	    PWMTableOscillator(dsp::analogSquareTables[table_number]), [](uint32_t) {}, osc.phase, osc.phase_increment,
	    resetter.phase, resetter.phase_increment, retrigger_phase, buffer.size(), buffer_start_this_sync);
	AmplitudeStepProcessor(amplitude.phase, amplitude.phase_increment).renderBlock(oscSyncRenderingBuffer, buffer);
	return osc.phase;
}
} // namespace deluge::dsp
