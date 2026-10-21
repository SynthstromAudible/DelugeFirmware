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

#include "deluge-project/arp_record.h"
#include "deluge-project/controller_mapping.h"
#include "deluge-project/cv_mode.h"
#include "deluge-project/instrument_slot.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace deluge::project {

/// @brief The DOM-owned identity record for one engine Output.
///
/// Keyed by Output::id() in OutputStructure, mirroring DocDrum/DrumStructure. Covers the fields common to every
/// output type (instruments, kits, audio outputs) plus per-instrument-type descriptors. Rebuilt from the engine at
/// load and save.
struct DocOutput {
	std::string name;
	int16_t colour{0};
	uint8_t mod_knob_mode{0};
	bool muted_in_arrangement{false};
	bool soloing_in_arrangement{false};
	bool armed_for_recording{false};
	// Per-instrument-type descriptors (populated only for the relevant OutputType; 0/empty otherwise).
	std::string dir_path;                             ///< Instrument (non-AUDIO)
	uint8_t default_velocity{0};                      ///< Instrument
	int32_t channel{0};                               ///< NonAudioInstrument (MIDI_OUT/CV)
	int32_t channel_suffix{-1};                       ///< MIDIInstrument
	std::string device_definition_file_name;          ///< MIDIInstrument
	bool collapse_aftertouch{false};                  ///< MIDIInstrument
	bool collapse_mpe{true};                          ///< MIDIInstrument
	uint8_t output_mpe_y{0};                          ///< MIDIInstrument (CCNumber underlying)
	CVMode cv2_source{CVMode::OFF};                   ///< CVInstrument
	bool hide_unlabeled_cc{false};                    ///< MIDIInstrument: its CC menus offer only labelled CCs
	std::map<uint8_t, std::string> cc_labels;         ///< MIDIInstrument: the user's names for its CCs, by number
	int8_t input_channel{1};                          ///< AudioOutput: an index of kInputChannelWords (left)
	uint8_t audio_mode{0};                            ///< AudioOutput: an index of kAudioModeWords (player)
	std::optional<uint32_t> recording_from_output_id; ///< AudioOutput: the track it records from, by id
	/// The instrument a synth track (a `sound` element) plays; absent for every other kind.
	std::optional<InstrumentSlot> instrument;
	/// The output's knobs: a synth track's mod knobs and learned MIDI knobs, a kit's or an audio track's learned MIDI
	/// knobs, each pointed at a param of its engine.
	std::vector<ControllerMapping> controls;
	/// A kit's or an audio track's effects; absent for every other kind.
	std::optional<FxChain> effects;
	/// A synth track's own arp, which a new clip on it starts from; absent unless its element held one.
	std::optional<ArpRecord> arp;

	/// Equal when the identity fields, the instrument, the controls, the effects and the arp are.
	bool operator==(const DocOutput& other) const {
		return name == other.name && colour == other.colour && mod_knob_mode == other.mod_knob_mode
		       && muted_in_arrangement == other.muted_in_arrangement
		       && soloing_in_arrangement == other.soloing_in_arrangement
		       && armed_for_recording == other.armed_for_recording && dir_path == other.dir_path
		       && default_velocity == other.default_velocity && channel == other.channel
		       && channel_suffix == other.channel_suffix
		       && device_definition_file_name == other.device_definition_file_name
		       && collapse_aftertouch == other.collapse_aftertouch && collapse_mpe == other.collapse_mpe
		       && hide_unlabeled_cc == other.hide_unlabeled_cc && cc_labels == other.cc_labels
		       && input_channel == other.input_channel && audio_mode == other.audio_mode
		       && recording_from_output_id == other.recording_from_output_id && output_mpe_y == other.output_mpe_y
		       && cv2_source == other.cv2_source && instrument == other.instrument && controls == other.controls
		       && effects == other.effects && arp == other.arp;
	}
};

} // namespace deluge::project
