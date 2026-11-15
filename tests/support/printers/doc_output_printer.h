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

#include "deluge-project/doc_audio_clip.h"
#include "deluge-project/doc_drum.h"
#include "deluge-project/doc_output.h"
#include "deluge-project/instrument_slot.h"
#include "support/printers/arp_record_printer.h"
#include "support/printers/controller_mapping_printer.h"
#include "support/printers/fx_chain_printer.h"
#include "support/printers/list_printer.h"
#include "support/printers/mod_knob_cc_printer.h"
#include "support/printers/optional_printer.h"
#include "support/printers/state_handle_printer.h"

#include <format>
#include <ostream>
#include <span>
#include <utility>

/// @file
/// @brief Prints an audio clip's, a drum's and an output's structural records, with the instrument slot they hold, in
///        a failed CppSpec comparison, alone or as container elements.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints an audio clip's record field by field, an enum as its number, e.g. `{file "SAMPLES/A.WAV",
///        start 0, end 0, transpose 0, cents 0, interpolation 1, pitch_and_speed_independent 1, reversed 0, …}`.
/// @param os The stream to print to.
/// @param c The record.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DocAudioClip& c) {
	return os << std::format("{{file \"{}\", start {}, end {}, transpose {}, cents {}, interpolation {}, "
	                         "pitch_and_speed_independent {}, reversed {}, attack {}, voice_priority {}, "
	                         "overdubs_should_clone {}}}",
	                         c.file_path, c.start_pos, c.end_pos, c.transpose, c.cents,
	                         std::to_underlying(c.interpolation_mode), int{c.pitch_and_speed_independent},
	                         int{c.reversed}, c.attack, std::to_underlying(c.voice_priority),
	                         int{c.overdubs_should_clone});
}

/// @brief Prints an instrument slot as its plugin's id, its state and the tick magnitude it was filled at, e.g.
///        `{plugin "deluge.instrument", state {plugin …}, tick_magnitude 1}`.
/// @param os The stream to print to.
/// @param s The slot.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const InstrumentSlot& s) {
	return os << "{plugin \"" << s.plugin << "\", state " << s.state << ", tick_magnitude " << s.tick_magnitude << "}";
}

/// @brief Prints a drum's record field by field, an enum as its number and an empty slot or arp as `none`, e.g.
///        `{drum_type 0, channel 0, note 0, name "KICK", instrument none, controls [], arp none}`.
/// @param os The stream to print to.
/// @param d The record.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DocDrum& d) {
	using deluge::test::printers::print_list;
	using deluge::test::printers::print_optional;
	os << std::format("{{drum_type {}, channel {}, note {}, name \"{}\", instrument ", std::to_underlying(d.drum_type),
	                  d.channel, d.note, d.name);
	print_optional(os, d.instrument) << ", controls ";
	print_list(os, std::span{d.controls}) << ", arp ";
	return print_optional(os, d.arp) << "}";
}

/// @brief Prints an output's record field by field, the ones its `operator==` compares, an enum as its number and
///        an empty optional as `none`, e.g. `{name "SYNTH", colour 0, …, cc_labels [74 "Cutoff"], …, arp none}`.
/// @param os The stream to print to.
/// @param o The record.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DocOutput& o) {
	using deluge::test::printers::print_list;
	using deluge::test::printers::print_optional;
	os << std::format("{{name \"{}\", colour {}, mod_knob_mode {}, muted_in_arrangement {}, soloing_in_arrangement {}, "
	                  "armed_for_recording {}, dir_path \"{}\", default_velocity {}, channel {}, channel_suffix {}, "
	                  "device_definition_file_name \"{}\", collapse_aftertouch {}, collapse_mpe {}, output_mpe_y {}, "
	                  "cv2_source {}, hide_unlabeled_cc {}, cc_labels [",
	                  o.name, o.colour, o.mod_knob_mode, int{o.muted_in_arrangement}, int{o.soloing_in_arrangement},
	                  int{o.armed_for_recording}, o.dir_path, o.default_velocity, o.channel, o.channel_suffix,
	                  o.device_definition_file_name, int{o.collapse_aftertouch}, int{o.collapse_mpe}, o.output_mpe_y,
	                  std::to_underlying(o.cv2_source), int{o.hide_unlabeled_cc});
	const char* separator = "";
	for (const auto& [number, label] : o.cc_labels) {
		os << separator << std::format("{} \"{}\"", number, label);
		separator = ", ";
	}
	os << "], mod_knob_ccs ";
	print_list(os, std::span{o.mod_knob_ccs})
	    << std::format(", input_channel {}, audio_mode {}, recording_from_output_id ", o.input_channel, o.audio_mode);
	print_optional(os, o.recording_from_output_id) << ", instrument ";
	print_optional(os, o.instrument) << ", controls ";
	print_list(os, std::span{o.controls}) << ", effects ";
	print_optional(os, o.effects) << ", arp ";
	return print_optional(os, o.arp) << "}";
}

} // namespace deluge::project
