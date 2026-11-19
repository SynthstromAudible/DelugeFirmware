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

#include "instruments/deluge/sound_state.h"
#include "instruments/deluge/state_access.h"

#include <string>

namespace deluge::test {

/// @brief A record read in the file's flat layout, live: each access flattens the record as it is then, so a spec
/// that reads it after an Edit sees the Edit. Empty where there is no record.
class FlatView {
public:
	/// @brief The record flattened for one expression.
	struct Snapshot {
		instrument::FlatSoundState flat;
		const instrument::FlatSoundState* operator->() const { return &flat; }
	};
	explicit FlatView(const instrument::SoundState* record) : record_(record) {}
	Snapshot operator->() const { return {instrument::FlatSoundState(*record_)}; }
	instrument::FlatSoundState operator*() const { return *record_; }
	[[nodiscard]] bool has_value() const { return record_ != nullptr; }
	explicit operator bool() const { return has_value(); }

private:
	const instrument::SoundState* record_;
};

/// @brief @p record read live in the file's flat layout.
[[nodiscard]] inline FlatView flat_of(const instrument::SoundState* record) {
	return FlatView{record};
}

/// @brief Change @p record through its flat layout, as a spec that builds a state field by field does.
template <class Change>
void change_flat(instrument::SoundState& record, Change&& change) {
	instrument::FlatSoundState flat = record;
	change(flat);
	record = flat;
}

/// @brief The first state_access reader on which @p record and its flat layout disagree; empty where none does.
[[nodiscard]] inline std::string reader_difference(const instrument::SoundState& record) {
	namespace instrument = deluge::instrument;
	const instrument::FlatSoundState flat = record;
	if (instrument::synth_mode(record) != flat.synth_mode) {
		return "synth_mode";
	}
	for (size_t s = 0; s < flat.sources.size(); s++) {
		const auto id = static_cast<instrument::SourceId>(s);
		const instrument::SourceState& src = flat.sources[s];
		if (instrument::source_type(record, id) != src.osc_type) {
			return "source_type";
		}
		if (!(instrument::source_tuning(record, id)
		      == instrument::Tuning{.transpose = src.transpose, .cents = src.cents})) {
			return "source_tuning";
		}
		if (!(instrument::sample_playback(record, id)
		      == instrument::SamplePlayback{.repeat_mode = src.repeat_mode,
		                                    .time_stretch_amount = src.time_stretch_amount,
		                                    .interpolation = src.interpolation_mode,
		                                    .pitch_and_speed_independent = src.pitch_and_speed_independent,
		                                    .reversed = src.reversed})) {
			return "sample_playback";
		}
		if (instrument::source_ranges(record, id) != src.ranges) {
			return "source_ranges";
		}
		const instrument::DxPatchState* patch = instrument::dx_patch(record, id);
		if ((patch != nullptr) != src.dx_patch.has_value() || (patch != nullptr && !(*patch == *src.dx_patch))) {
			return "dx_patch";
		}
	}
	if (!(instrument::sound_effects(record) == flat.effects)) {
		return "sound_effects";
	}
	if (!(instrument::unison(record)
	      == deluge::synth::UnisonState{
	          .count = flat.num_unison, .detune = flat.unison_detune, .spread = flat.unison_stereo_spread})) {
		return "unison";
	}
	if (!(instrument::host_fields(record)
	      == instrument::HostFields{.side_chain_send_level = flat.side_chain_send_level,
	                                .output_midi_channel = flat.output_midi_channel,
	                                .output_midi_note_for_drum = flat.output_midi_note_for_drum})) {
		return "host_fields";
	}
	return {};
}

} // namespace deluge::test
