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

#include "controls/knob_focus.h"
#include "support/printers/optional_printer.h"
#include "support/printers/project_address_printer.h"

#include <format>
#include <optional>
#include <ostream>
#include <type_traits>
#include <utility>
#include <variant>

/// @file
/// @brief Prints what the gold knobs point at (`KnobFocus`, the owners it can hold and a held pad's region) in a failed
///        CppSpec comparison, alone or as a container element.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::controls {

/// @brief Prints the song's own focus, `{song}`.
/// @param os The stream to print to.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SongFocus&) {
	return os << "{song}";
}

/// @brief Prints a sound's focus field by field, an empty one as `none`, e.g.
///        `{sound {kind 0, id 2}, clip {clip 7}, row none, kit none}`.
/// @param os The stream to print to.
/// @param f The focus.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SoundFocus& f) {
	using deluge::test::printers::print_optional;
	os << "{sound " << f.slot << ", clip ";
	print_optional(os, f.clip) << ", row ";
	print_optional(os, f.row) << ", kit ";
	return print_optional(os, f.kit) << "}";
}

/// @brief Prints a host chain's focus field by field, its owner as a number, e.g.
///        `{chain {kind 0, id 3}, owner 0, clip none}`.
/// @param os The stream to print to.
/// @param f The focus.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ChainFocus& f) {
	using deluge::test::printers::print_optional;
	os << "{chain " << f.chain << ", owner " << +std::to_underlying(f.owner) << ", clip ";
	return print_optional(os, f.clip) << "}";
}

/// @brief Prints a MIDI track's focus as its track and its clip, e.g. `{midi {track 4}, clip {clip 7}}`.
/// @param os The stream to print to.
/// @param f The focus.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const MidiFocus& f) {
	os << "{midi " << f.track << ", clip ";
	return deluge::test::printers::print_optional(os, f.clip) << "}";
}

/// @brief Prints an owner with no knobs as its kind's number, its track, its drum and its clip's id, e.g.
///        `{no_controls 1, track {track 4}, drum {kit 4, drum 9}, clip none}`.
/// @param os The stream to print to.
/// @param f The focus.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const NoControlsFocus& f) {
	using deluge::test::printers::print_optional;
	os << "{no_controls " << +std::to_underlying(f.kind) << ", track " << f.track << ", drum ";
	print_optional(os, f.drum) << ", clip ";
	return print_optional(os, f.clip_id) << "}";
}

/// @brief Prints the knobs' owner as the focus it holds, or `none`.
/// @param os The stream to print to.
/// @param owner The owner.
/// @return @p os.
inline std::ostream&
operator<<(std::ostream& os,
           const std::variant<std::monostate, SongFocus, SoundFocus, ChainFocus, MidiFocus, NoControlsFocus>& owner) {
	std::visit(
	    [&os]<typename T>(const T& focus) {
		    if constexpr (std::is_same_v<T, std::monostate>) {
			    os << "none";
		    }
		    else {
			    os << focus;
		    }
	    },
	    owner);
	return os;
}

/// @brief Prints a held region as its position, its length and its note row's id, e.g.
///        `{pos 4294967295, length 0, note_row 0}`.
/// @param os The stream to print to.
/// @param r The region.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ModRegion& r) {
	return os << std::format("{{pos {}, length {}, note_row {}}}", r.pos, r.length, r.note_row_id);
}

/// @brief Prints the compressor view as its editing flag and its param, e.g. `{editing 1, param 2}`.
/// @param os The stream to print to.
/// @param v The view.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const CompView& v) {
	return os << "{editing " << int{v.editing} << ", param " << int{v.param} << "}";
}

/// @brief Prints a focus field by field, e.g.
///        `{owner {song}, region {pos 4294967295, length 0, note_row 0}, comp {editing 0, param 0}, vu_meter 0}`.
/// @param os The stream to print to.
/// @param f The focus.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const KnobFocus& f) {
	return os << "{owner " << f.owner << ", region " << f.region << ", comp " << f.comp << ", vu_meter "
	          << int{f.vu_meter} << "}";
}

} // namespace deluge::controls
