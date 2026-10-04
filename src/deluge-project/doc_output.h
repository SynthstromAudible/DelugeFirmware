/*
 * Copyright © 2024 Synthstrom Audible Limited
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

#include "deluge-project/cv_mode.h"
#include "deluge-project/instrument_slot.h"
#include <cstdint>
#include <optional>
#include <string>

namespace deluge::project {

/// The DOM-owned identity record for one engine Output (M3 owner-5, Output-base fields). Keyed by Output::id() in
/// OutputStructure, mirroring DocDrum/DrumStructure. Covers the fields common to every output type (instruments,
/// kits, audio outputs); per-instrument-type + Sound structure are later owner-5 slices. Rebuilt from the engine
/// at load + save.
struct DocOutput {
	std::string name;
	int16_t colour{0};
	uint8_t mod_knob_mode{0};
	bool muted_in_arrangement{false};
	bool soloing_in_arrangement{false};
	bool armed_for_recording{false};
	// Owner-5 Slice E: per-instrument-type descriptors (populated only for the relevant OutputType; 0/empty otherwise).
	std::string dir_path;                    ///< Instrument (non-AUDIO)
	uint8_t default_velocity{0};             ///< Instrument
	int32_t channel{0};                      ///< NonAudioInstrument (MIDI_OUT/CV)
	int32_t channel_suffix{-1};              ///< MIDIInstrument
	std::string device_definition_file_name; ///< MIDIInstrument
	bool collapse_aftertouch{false};         ///< MIDIInstrument
	bool collapse_mpe{true};                 ///< MIDIInstrument
	uint8_t output_mpe_y{0};                 ///< MIDIInstrument (CCNumber underlying)
	CVMode cv2_source{CVMode::OFF};          ///< CVInstrument
	/// The instrument a synth track (a `sound` element) plays; absent for every other kind.
	std::optional<InstrumentSlot> instrument;

	/// Equal when the identity fields and the instrument are.
	bool operator==(const DocOutput& other) const {
		return name == other.name && colour == other.colour && mod_knob_mode == other.mod_knob_mode
		       && muted_in_arrangement == other.muted_in_arrangement
		       && soloing_in_arrangement == other.soloing_in_arrangement
		       && armed_for_recording == other.armed_for_recording && dir_path == other.dir_path
		       && default_velocity == other.default_velocity && channel == other.channel
		       && channel_suffix == other.channel_suffix
		       && device_definition_file_name == other.device_definition_file_name
		       && collapse_aftertouch == other.collapse_aftertouch && collapse_mpe == other.collapse_mpe
		       && output_mpe_y == other.output_mpe_y && cv2_source == other.cv2_source
		       && instrument == other.instrument;
	}
};

} // namespace deluge::project
