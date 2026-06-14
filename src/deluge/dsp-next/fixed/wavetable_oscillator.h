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
#include "dsp-next/fixed/wave_table_data.h"
#include "dsp-next/fixed/wavetable_cycle.h"
#include "util/fixedpoint.h" // multiply_32x32_rshift32_rounded
#include <cstddef>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief A free-running (non-synced) wavetable oscillator, as a dsp-next component.
///
/// Renders a @ref WaveTableData view of a loaded wavetable. A single-cycle wave goes straight
/// through the windowed-sinc interpolation; a multi-cycle (morphing) wave chunks the block at each
/// cross-cycle (wave-index) boundary, picks the band that has data for the current cycle pair, and
/// cross-blends the two adjacent cycles by the morph position. This is the cleaned structural
/// replacement for the non-sync path of the legacy `WaveTable::render` (the inner sinc loops live in
/// @ref render_wavetable_cycle / @ref render_wavetable_single_cycle); the morph chunking is the same
/// arithmetic, re-expressed as a `while` loop, so it is bit-identical.
class [[gnu::hot]] WavetableOscillator : public BlockGenerator<q31> {
public:
	WavetableOscillator(WaveTableData table, uint32_t phase, uint32_t increment, int32_t wave_index,
	                    int32_t wave_index_increment)
	    : table_{table}, phasor_{phase, increment}, wave_index_{wave_index},
	      wave_index_increment_{wave_index_increment} {}

	/// The phase after rendering — what the voice stores back to continue next block.
	[[nodiscard]] uint32_t phase() const { return phasor_.phase(); }

	void renderBlock(Buffer output) override {
		int32_t numSamples = static_cast<int32_t>(output.size());
		int32_t* outputBuffer = output.data();
		uint32_t phase = phasor_.phase();
		uint32_t phaseIncrement = phasor_.increment();

		// Decide on the ideal band for this pitch.
		int32_t bHere = table_.select_band(phaseIncrement);
		const WaveTableBand* bandHere = &table_.band(bHere);

		// A morphing wavetable: render a cycle-pair's worth at a time, advancing the wave index.
		if (table_.num_cycles() > 1) [[likely]] {
			int32_t numSamplesLeftToDo = numSamples;

			// Rounded multiplies needed here — otherwise a click at wav pos 0 with numSamples 32.
			int32_t waveIndexScaled = multiply_32x32_rshift32_rounded(table_.wave_index_multiplier(), wave_index_);
			int32_t waveIndexIncrementScaled =
			    multiply_32x32_rshift32_rounded(table_.wave_index_multiplier(), wave_index_increment_);

			int32_t lshiftAmountToGetCrossCycleStrength =
			    32 + table_.num_cycle_transitions_magnitude() - kWaveIndexScaledInputBits; // Always positive.
			int32_t crossCycleStrength2Increment = waveIndexIncrementScaled << lshiftAmountToGetCrossCycleStrength;

			while (numSamplesLeftToDo > 0) {
				int32_t numSamplesThisCycle = numSamplesLeftToDo;

				int32_t firstCycleNumber =
				    waveIndexScaled >> (kWaveIndexScaledInputBits - table_.num_cycle_transitions_magnitude());
				int32_t firstCycleNumberAfterAllIncrements =
				    (waveIndexScaled + waveIndexIncrementScaled * (numSamplesLeftToDo - 1))
				    >> (kWaveIndexScaledInputBits - table_.num_cycle_transitions_magnitude());

				// Does this window span into the next cycle as the wave index increments? If so, only do
				// up to the boundary now.
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
					// +1: we only increment *between* the samples we render, so 1 increment = 2 samples.
					numSamplesThisCycle = numIncrementsWeCanDoNow + 1;
				}

				uint32_t crossCycleStrength2 = waveIndexScaled << lshiftAmountToGetCrossCycleStrength;

				// Walk up to a band that actually has data for both cycle indexes we'll interpolate between.
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
					render_wavetable_cycle(outputBuffer, outputBuffer + numSamplesThisCycle, firstCycleNumber,
					                       *bandHere, phase, phaseIncrement, crossCycleStrength2,
					                       crossCycleStrength2Increment, kernel);
					phase += phaseIncrement * static_cast<uint32_t>(numSamplesThisCycle);
				}

				numSamplesLeftToDo -= numSamplesThisCycle;
				outputBuffer += numSamplesThisCycle;
				waveIndexScaled += waveIndexIncrementScaled * numSamplesThisCycle;
			}
		}

		// Or a single-cycle waveform: one straight pass.
		else {
			const int16_t* kernel = select_sinc_kernel(phaseIncrement, bandHere->maxPhaseIncrement);
			render_wavetable_single_cycle(outputBuffer, outputBuffer + numSamples, *bandHere, phase, phaseIncrement,
			                              kernel);
			phase += phaseIncrement * static_cast<uint32_t>(numSamples);
		}

		phasor_.seek(phase);
	}

private:
	static constexpr int32_t kWaveIndexScaledInputBits = 30; // legacy NUM_BITS_IN_WAVE_INDEX_SCALED_INPUT

	WaveTableData table_;
	Phasor phasor_;
	int32_t wave_index_;
	int32_t wave_index_increment_;
};

} // namespace deluge::dsp::fixed
