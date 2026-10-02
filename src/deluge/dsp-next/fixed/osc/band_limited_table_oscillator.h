/*
 * Copyright © 2026 Synthstrom Audible Limited
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

#include "dsp-next/core/generator.h"
#include "dsp-next/fixed/bandlimited/band_limited_table.h"
#include "dsp-next/fixed/osc/table_oscillator.h"
#include <argon.hpp>
#include <cstdint>

namespace deluge::dsp::fixed {

/// @brief A band-limited wavetable oscillator: it picks the right band of a @ref BandlimitedTable
/// for its pitch and renders it.
///
/// The band depends only on the phase increment (pitch), which is fixed for the block, so the
/// band is selected once at construction and the rest is exactly a @ref TableOscillator over it.
/// This pulls the per-wave band selection (the old `getTableNumber` + table-array indexing /
/// triangle ladder) into the kernel's self-selecting BandlimitedTable; the oscillator just pairs
/// it with the increment. Owns a seekable phase (via TableOscillator) so it composes with
/// @ref SyncedOscillator for hard sync.
///
/// Band selection and render rate are usually the same increment, but pulse-width waves render at
/// a rewritten (faster) increment while their band-limiting must still target the perceived pitch;
/// the four-argument constructor decouples the two (see @p selection_increment).
class [[gnu::hot]] BandLimitedTableOscillator : public Generator<BandLimitedTableOscillator, q31> {
	const BandlimitedTable* bands_; ///< the wave's band set, retained so the band can be re-selected per block
	TableOscillator oscillator_;

public:
	BandLimitedTableOscillator(const BandlimitedTable& bands, uint32_t phase, uint32_t increment)
	    : bands_{&bands}, oscillator_{bands.select(increment), phase, increment} {}

	/// @param selection_increment the pitch used to pick the band, when it differs from the render
	/// increment (pulse-width waves: the band tracks the note, the phase advances at the pulse rate).
	///
	/// TODO(behavior-changing, deferred): for non-square pulse-width waves this preserves a legacy
	/// quirk — the band is chosen from the pre-PW note (`selection_increment`) while the wave renders
	/// at the faster post-PW `increment`, so it is mildly *under*-band-limited and can alias a touch.
	/// This matches the original `getTableNumber(phaseIncrementForCalculations)` behavior (which
	/// carried the same TODO) and is bit-exact; selecting on `increment` instead is arguably more
	/// correct but audibly changes every PW patch, so it must wait for a deliberate re-baseline.
	BandLimitedTableOscillator(const BandlimitedTable& bands, uint32_t phase, uint32_t increment,
	                           uint32_t selection_increment)
	    : bands_{&bands}, oscillator_{bands.select(selection_increment), phase, increment} {}

	void seek(uint32_t phase) { oscillator_.seek(phase); }

	/// The live phase — for persisting/reading a persistent oscillator's phase across blocks.
	[[nodiscard]] uint32_t phase() const { return oscillator_.phase(); }

	/// Set the per-block pitch, **re-selecting the band** for the new increment (bit-identical to constructing a
	/// fresh oscillator at that increment). Keeps the accumulated phase.
	void setIncrement(uint32_t increment) {
		oscillator_.setIncrement(increment);
		oscillator_.setTable(bands_->select(increment));
	}

	/// As above but with a separate band-selection pitch (pulse-width waves render faster than the note they
	/// band-limit for; see the four-argument constructor).
	void setIncrement(uint32_t increment, uint32_t selection_increment) {
		oscillator_.setIncrement(increment);
		oscillator_.setTable(bands_->select(selection_increment));
	}

	[[gnu::always_inline]] Argon<q31> render() { return oscillator_.render(); }
};

} // namespace deluge::dsp::fixed
