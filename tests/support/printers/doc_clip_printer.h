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

#include "deluge-project/doc_clip.h"
#include "support/printers/arp_record_printer.h"

#include <format>
#include <optional>
#include <ostream>
#include <utility>

/// @file
/// @brief Prints a clip's structural record (`DocClip`), with its values, its arp and its note rows, in a failed
///        CppSpec comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

namespace doc_clip_printer_detail {

/// @brief Prints @p v, or `none` where it is empty.
/// @param os The stream to print to.
/// @param v The value.
/// @return @p os.
template <typename T>
std::ostream& print_optional(std::ostream& os, const std::optional<T>& v) {
	return v ? os << *v : os << "none";
}

} // namespace doc_clip_printer_detail

/// @brief Prints a clip's values field by field, an empty one as `none`, e.g.
///        `{midi_bank none, midi_sub none, midi_pgm 5, bend_range 2, bend_range_mpe 48, arp none}`.
/// @param os The stream to print to.
/// @param v The values.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ClipValues& v) {
	using doc_clip_printer_detail::print_optional;
	os << "{midi_bank ";
	print_optional(os, v.midi_bank) << ", midi_sub ";
	print_optional(os, v.midi_sub) << ", midi_pgm ";
	print_optional(os, v.midi_pgm) << ", bend_range ";
	print_optional(os, v.bend_range) << ", bend_range_mpe ";
	print_optional(os, v.bend_range_mpe) << ", arp ";
	return print_optional(os, v.arp) << "}";
}

/// @brief Prints a note row's persistent fields, the ones its `operator==` compares, an enum as its number, e.g.
///        `{y 3, loop_length_if_independent 0, colour_offset 0, sequence_direction 0, muted 0}`.
/// @param os The stream to print to.
/// @param r The note row.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DocNoteRow& r) {
	return os << std::format(
	           "{{y {}, loop_length_if_independent {}, colour_offset {}, sequence_direction {}, muted {}}}", r.y,
	           r.loop_length_if_independent, r.colour_offset, std::to_underlying(r.sequence_direction), int{r.muted});
}

/// @brief Prints a clip's record field by field, an enum as its number and the note rows as a list, e.g.
///        `{name "", sequence_direction 0, values {…}, loop_length 96, colour_offset -60, launch_style 0, section 0,
///        note_rows [{y 0, …}]}`.
/// @param os The stream to print to.
/// @param c The record.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const DocClip& c) {
	os << std::format("{{name \"{}\", sequence_direction {}, values ", c.name,
	                  std::to_underlying(c.sequence_direction));
	os << c.values;
	os << std::format(", loop_length {}, colour_offset {}, launch_style {}, section {}, note_rows [", c.loop_length,
	                  c.colour_offset, std::to_underlying(c.launch_style), c.section);
	const char* separator = "";
	for (const DocNoteRow& r : c.note_rows) {
		os << separator << r;
		separator = ", ";
	}
	return os << "]}";
}

} // namespace deluge::project
