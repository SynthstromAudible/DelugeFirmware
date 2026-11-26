/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "definitions_cxx.hpp"
#include "dsp/fixed/noise/cong.h"
#include "synth/sources/dx7/dx7_source.h"
#include "synth/sources/input/input_source.h"
#include "synth/sources/sample/sample_source.h"
#include "synth/sources/source_table.h"
#include "synth/sources/supersaw/supersaw_source.h"
#include "synth/sources/va/virtual_analog_source.h"
#include "synth/sources/wavetable/wavetable_source.h"
#include "synth/voice/voice_inputs.h"
#include <variant>

namespace deluge::synth {

/// @brief The phase state of one source slot that every synth mode shares, and the engines and the app read.
struct SlotPhase {
	/// The phase of a source no oscillator holds (FM's carriers, noise); a free-running oscillator's first phase.
	dsp::UQ<32> osc_pos{};
	dsp::UQ<32> phase_increment_stored_value{}; ///< The source's pitch before modulation
	dsp::Q<31> carrier_feedback{};              ///< FM's carrier feedback memory
	bool active{};                              ///< The slot sounds in this unison part
};

/// @brief One source slot of one unison part: the note's oscillator source held in place, the port-backed sources
/// bound for the voice's life, and the phase state every synth mode shares.
///
/// The oscillator is built at note-on by @ref note_on and kept across notes while its class is unchanged, so a
/// free-running oscillator keeps its phase from note to note. The kinds of source a slot plays are the rows of
/// `source_table.h`: a new source is a row there; the voice is unchanged.
class SourceSlot : public SlotPhase {
public:
	/// The port-free sources a slot can hold (the table's held rows); monostate when the note built none (FM, noise, a
	/// port-backed type).
	using Oscillator = HeldOscillator;

	SourceSlot() = default;
	SourceSlot(const SourceSlot&) = delete;
	SourceSlot& operator=(const SourceSlot&) = delete;
	SourceSlot(SourceSlot&&) = delete;
	SourceSlot& operator=(SourceSlot&&) = delete;
	~SourceSlot();

	/// Plays this slot's port-backed sources through @p ports, which must outlive it.
	void bind(const SourcePorts& ports);
	/// Starts this slot for a note. @p priority_rating is the voice's in the cull order (a sample's cluster loads are
	/// ranked by it). @return false if the note cannot play.
	bool note_on(const SourceSetup& source, uint32_t samples_late, uint32_t osc_retrigger_phase, bool reset_everything,
	             SynthMode synth_mode, int32_t note_code, uint8_t velocity, int32_t priority_rating,
	             dsp::fixed::Cong& noise);
	/// Lets go of the note: the slot goes inactive and its port-backed sources release what they hold.
	void unassign(bool deleting_song);
	/// @brief The note-off for a source of @p type: its ported row's release, which may end the note and unassign the
	/// slot; a held wave has nothing to release.
	/// @param type The slot's type as set.
	/// @param release The voice's cull rating, which a sample's loop-end release ranks its loads by.
	void release(OscType type, const SourceRelease& release);

	/// The source that renders a source of @p type now: a port-backed one by type, else the held oscillator, whatever
	/// its class (a VA wave changed on a re-triggered voice keeps the wave it built). Null when the slot holds none (a
	/// mode change under a live voice can leave it so).
	[[nodiscard]] Source* source_for(OscType type);
	/// The held oscillator, for an engine that renders it directly (ring-mod renders the raw wave).
	[[nodiscard]] Oscillator& oscillator() { return oscillator_; }
	/// The held oscillator's phase, else @ref osc_pos. (Osc sync seeds the slave from the master at block start.)
	[[nodiscard]] dsp::UQ<32> current_osc_phase() const;
	/// Advances the held oscillator's phase by @p delta, else @ref osc_pos (a skipped osc-sync master still runs).
	void advance_osc_phase(dsp::UQ<32> delta);

	[[nodiscard]] SampleSource& sample() { return std::get<SampleSource>(ported_); }
	[[nodiscard]] DX7Source& dx7() { return std::get<DX7Source>(ported_); }

private:
	/// Builds or keeps the oscillator for a SUBTRACTIVE or RINGMOD note of @p osc_type; else holds none.
	void build_oscillator(OscType osc_type, bool engine_mode, uint32_t osc_retrigger_phase, bool reset_everything,
	                      dsp::fixed::Cong& noise);

	/// @brief Calls @p fn with the ported row playing @p type and its source.
	/// @return False when no ported row plays it.
	template <class Fn>
	bool with_ported(OscType type, Fn fn);

	Oscillator oscillator_;
	/// The port-backed sources (the table's ported rows), each bound to its port by @ref bind
	PortedSources ported_{unbound_ported()};

	/// @brief Each ported source with no port yet.
	/// @return The tuple.
	[[nodiscard]] static PortedSources unbound_ported() {
		return []<class... Source>(std::type_identity<std::tuple<Source...>>) {
			return PortedSources{Source{nullptr}...};
		}(std::type_identity<PortedSources>{});
	}
};

} // namespace deluge::synth
