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
#pragma once

#include "storage/wave_table/wave_table.h"
namespace deluge::dsp {

class Oscillator {
	static void maybeStorePhase(const OscType& type, uint32_t* startPhase, uint32_t phase, bool doPulseWave);
	static void applyAmplitudeVectorToBuffer(int32_t amplitude, int32_t numSamples, int32_t amplitudeIncrement,
	                                         int32_t* outputBufferPos, int32_t* inputBuferPos);

public:
	static void renderOsc(OscType type, int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end,
	                      int32_t num_samples, uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
	                      bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync, uint32_t resetter_phase,
	                      uint32_t resetter_phase_increment, uint32_t retrigger_phase, int32_t wave_index_increment,
	                      int source_wave_index_last_time, WaveTable* wave_table);
	static void renderSine(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
	                       uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase, bool apply_amplitude,
	                       int32_t amplitude_increment, bool do_osc_sync, uint32_t resetter_phase,
	                       uint32_t resetter_phase_increment, uint32_t retrigger_phase, int32_t wave_index_increment,
	                       int source_wave_index_last_time, WaveTable* wave_table);
	static void renderTriangle(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
	                           uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
	                           bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
	                           uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
	                           int32_t wave_index_increment, int source_wave_index_last_time, WaveTable* wave_table);
	static void renderSquare(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
	                         uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
	                         bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
	                         uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
	                         int32_t wave_index_increment, int source_wave_index_last_time, WaveTable* wave_table);
	static void renderSaw(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
	                      uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase, bool apply_amplitude,
	                      int32_t amplitude_increment, bool do_osc_sync, uint32_t resetter_phase,
	                      uint32_t resetter_phase_increment, uint32_t retrigger_phase, int32_t wave_index_increment,
	                      int source_wave_index_last_time, WaveTable* wave_table);
	static void renderWavetable(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
	                            uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
	                            bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
	                            uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
	                            int32_t wave_index_increment, int source_wave_index_last_time, WaveTable* wave_table);
	static void renderAnalogSaw2(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
	                             uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
	                             bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
	                             uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
	                             int32_t wave_index_increment, int source_wave_index_last_time, WaveTable* wave_table);
	static void renderAnalogSquare(int32_t amplitude, int32_t* buffer_start, int32_t* buffer_end, int32_t num_samples,
	                               uint32_t phase_increment, uint32_t pulse_width, uint32_t* start_phase,
	                               bool apply_amplitude, int32_t amplitude_increment, bool do_osc_sync,
	                               uint32_t resetter_phase, uint32_t resetter_phase_increment, uint32_t retrigger_phase,
	                               int32_t wave_index_increment, int source_wave_index_last_time,
	                               WaveTable* wave_table);
};

} // namespace deluge::dsp
