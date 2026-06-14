/*
 * Copyright © 2020-2026 Synthstrom Audible Limited
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

#include "dsp-next/core/generator.h" // BlockGenerator, Buffer, q31
#include "dsp-next/core/phasor.h"
#include "dsp-next/fixed/sinc_kernel.h"
#include "dsp-next/fixed/sync_windower.h" // Resetter, SyncWindower
#include "dsp-next/fixed/wave_table_data.h"
#include "dsp-next/fixed/wavetable_cycle.h"
#include "util/fixedpoint.h" // multiply_32x32_rshift32_rounded
#include <cstddef>
#include <cstdint>
#include <optional>

namespace deluge::dsp::fixed {

/// @brief A hard-synced wavetable oscillator, as a dsp-next component.
///
/// Like @ref WavetableOscillator but each morph chunk is restarted at every @ref Resetter crossover
/// (classic osc-sync) via a @ref SyncWindower, which owns the window split + half-sine seam blend.
///
/// The cross-cycle morph is reformulated: legacy code carried it in a free-running counter nudged by
/// a per-crossover callback, *only* so the redone crossover sample reused the same value. Morph is
/// really a function of output position — `morph(n) = chunkStart + n · increment` — so each window
/// just takes `crossCycleStrength2 + window.offset · increment` (offset is the window's start within
/// the chunk, == the committed samples before it). That is identically the legacy accumulation
/// (Σ increment·(samples−1) = increment·offset), so it is bit-exact, and the redo trivially reuses
/// the same morph because the overlapping window has the same offset.
class [[gnu::hot]] WavetableSyncOscillator : public BlockGenerator<q31> {
public:
	WavetableSyncOscillator(WaveTableData table, uint32_t phase, uint32_t increment, Resetter resetter,
	                        uint32_t retrigger_phase, int32_t wave_index, int32_t wave_index_increment)
	    : table_{table}, phasor_{phase, increment}, resetter_{resetter}, retrigger_phase_{retrigger_phase},
	      wave_index_{wave_index}, wave_index_increment_{wave_index_increment} {}

	/// The audible phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		int32_t numSamples = static_cast<int32_t>(output.size());
		int32_t* outputBuffer = output.data();
		uint32_t phase = phasor_.phase();
		uint32_t phaseIncrement = phasor_.increment();
		uint32_t resetterPhase = resetter_.phase;
		uint32_t resetterPhaseIncrement = resetter_.phase_increment;

		int32_t bHere = table_.select_band(phaseIncrement);
		const WaveTableBand* bandHere = &table_.band(bHere);

		if (table_.num_cycles() > 1) [[likely]] {
			int32_t numSamplesLeftToDo = numSamples;

			int32_t waveIndexScaled = multiply_32x32_rshift32_rounded(table_.wave_index_multiplier(), wave_index_);
			int32_t waveIndexIncrementScaled =
			    multiply_32x32_rshift32_rounded(table_.wave_index_multiplier(), wave_index_increment_);

			int32_t lshiftAmountToGetCrossCycleStrength =
			    32 + table_.num_cycle_transitions_magnitude() - kWaveIndexScaledInputBits;
			int32_t crossCycleStrength2Increment = waveIndexIncrementScaled << lshiftAmountToGetCrossCycleStrength;

			while (numSamplesLeftToDo > 0) {
				int32_t numSamplesThisCycle = numSamplesLeftToDo;

				int32_t firstCycleNumber =
				    waveIndexScaled >> (kWaveIndexScaledInputBits - table_.num_cycle_transitions_magnitude());
				int32_t firstCycleNumberAfterAllIncrements =
				    (waveIndexScaled + waveIndexIncrementScaled * (numSamplesLeftToDo - 1))
				    >> (kWaveIndexScaledInputBits - table_.num_cycle_transitions_magnitude());

				if (firstCycleNumber != firstCycleNumberAfterAllIncrements) {
					int32_t cycleSizeInWaveIndex =
					    1 << (kWaveIndexScaledInputBits - table_.num_cycle_transitions_magnitude());
					int32_t waveIndexPlaceWithinCycle = waveIndexScaled & (cycleSizeInWaveIndex - 1);
					int32_t waveIndexDistanceUntilFinalBigValueStillInThisCycle =
					    (waveIndexIncrementScaled >= 0) ? (cycleSizeInWaveIndex - 1 - waveIndexPlaceWithinCycle)
					                                    : (waveIndexPlaceWithinCycle);
					int32_t waveIndexIncrementScaledAbs = waveIndexIncrementScaled;
					if (waveIndexIncrementScaledAbs < 0) {
						waveIndexIncrementScaledAbs = -waveIndexIncrementScaledAbs;
					}
					int32_t numIncrementsWeCanDoNow = (uint32_t)waveIndexDistanceUntilFinalBigValueStillInThisCycle
					                                  / (uint32_t)waveIndexIncrementScaledAbs;
					numSamplesThisCycle = numIncrementsWeCanDoNow + 1;
				}

				uint32_t crossCycleStrength2 = waveIndexScaled << lshiftAmountToGetCrossCycleStrength;

				while (bandHere->fromCycleNumber > firstCycleNumber
				       || bandHere->toCycleNumber <= firstCycleNumber + 1) {
					bHere++;
					if (bHere == table_.num_bands()) {
						break;
					}
					bandHere = &table_.band(bHere);
				}

				if (bHere < table_.num_bands()) {
					const int16_t* kernel = select_sinc_kernel(phaseIncrement, bandHere->maxPhaseIncrement);
					SyncWindower windower{Buffer{outputBuffer, static_cast<std::size_t>(numSamplesThisCycle)},
					                      Resetter{resetterPhase, resetterPhaseIncrement}, phase, phaseIncrement,
					                      retrigger_phase_};
					while (std::optional<SyncWindower::Window> window = windower.next()) {
						// Morph by output position: the same value the legacy per-crossover callback built up.
						uint32_t crossCycleStrengthHere = crossCycleStrength2
						                                  + static_cast<uint32_t>(crossCycleStrength2Increment)
						                                        * static_cast<uint32_t>(window->offset);
						render_wavetable_cycle(window->data, window->data + window->length, firstCycleNumber, *bandHere,
						                       window->phase, phaseIncrement, crossCycleStrengthHere,
						                       crossCycleStrength2Increment, kernel);
					}
					phase = windower.phase();
				}

				numSamplesLeftToDo -= numSamplesThisCycle;
				outputBuffer += numSamplesThisCycle;
				waveIndexScaled += waveIndexIncrementScaled * numSamplesThisCycle;
				resetterPhase += resetterPhaseIncrement * static_cast<uint32_t>(numSamplesThisCycle);
			}
		}

		else {
			const int16_t* kernel = select_sinc_kernel(phaseIncrement, bandHere->maxPhaseIncrement);
			SyncWindower windower{Buffer{outputBuffer, static_cast<std::size_t>(numSamples)},
			                      Resetter{resetterPhase, resetterPhaseIncrement}, phase, phaseIncrement,
			                      retrigger_phase_};
			while (std::optional<SyncWindower::Window> window = windower.next()) {
				render_wavetable_single_cycle(window->data, window->data + window->length, *bandHere, window->phase,
				                              phaseIncrement, kernel);
			}
			phase = windower.phase();
		}

		phasor_.seek(phase);
	}

private:
	static constexpr int32_t kWaveIndexScaledInputBits = 30; // legacy NUM_BITS_IN_WAVE_INDEX_SCALED_INPUT

	WaveTableData table_;
	Phasor phasor_;
	Resetter resetter_;
	uint32_t retrigger_phase_;
	int32_t wave_index_;
	int32_t wave_index_increment_;
};

} // namespace deluge::dsp::fixed
