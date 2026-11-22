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

#include "project/arrangement.h"
#include "project/reverb_settings.h"
#include "project/scale_settings.h"
#include "project/transport.h"
#include "support/printers/note_set_printer.h"
#include "support/printers/optional_printer.h"

#include <format>
#include <ostream>
#include <utility>

/// @file
/// @brief Prints the song's transport, scale, reverb and arrangement settings in a failed CppSpec comparison, alone or
///        as fields of a record that holds them.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints the transport's persistent fields, the ones its `operator==` compares, an empty clip id as `none`,
///        e.g. `{samples_per_timer_tick_big 0, inside_world_tick_magnitude 1, offset_from_bpm 0, sync_scaling_clip_id
///        none}`.
/// @param os The stream to print to.
/// @param t The transport.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Transport& t) {
	os << std::format("{{samples_per_timer_tick_big {}, inside_world_tick_magnitude {}, offset_from_bpm {}, "
	                  "sync_scaling_clip_id ",
	                  t.samples_per_timer_tick_big, t.inside_world_tick_magnitude,
	                  t.inside_world_tick_magnitude_offset_from_bpm);
	return deluge::test::printers::print_optional(os, t.sync_scaling_clip_id) << "}";
}

/// @brief Prints the scale settings its `operator==` compares: the key, the disabled preset scales as bits and the
///        user scale, e.g.
///        `{key {root 0, mode NoteSet(C, D, E, F, G, A, B)}, disabled_preset_scales 0000…, user_scale NoteSet()}`.
/// @param os The stream to print to.
/// @param s The settings.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ScaleSettings& s) {
	return os << "{key " << s.key << ", disabled_preset_scales " << s.disabled_preset_scales << ", user_scale "
	          << s.user_scale_notes << "}";
}

/// @brief Prints the song's reverb field by field, an enum as its number, e.g. `{room_size 0.5, hpf 0, lpf 0,
///        damping 0.5, width 1, pan 0, sidechain_volume 0, …, sidechain_sync 0, model 0}`.
/// @param os The stream to print to.
/// @param r The settings.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const ReverbSettings& r) {
	return os << std::format("{{room_size {}, hpf {}, lpf {}, damping {}, width {}, pan {}, sidechain_volume {}, "
	                         "sidechain_shape {}, sidechain_attack {}, sidechain_release {}, sidechain_sync {}, "
	                         "model {}}}",
	                         r.room_size, r.hpf, r.lpf, r.damping, r.width, r.pan, r.sidechain_volume,
	                         r.sidechain_shape, r.sidechain_attack, r.sidechain_release,
	                         std::to_underlying(r.sidechain_sync), std::to_underlying(r.model));
}

/// @brief Prints a session macro field by field, an empty id as `none`, e.g.
///        `{kind 1, clip 3, output none, section 0}`.
/// @param os The stream to print to.
/// @param m The macro.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SessionMacro& m) {
	using deluge::test::printers::print_optional;
	os << "{kind " << +std::to_underlying(m.kind) << ", clip ";
	print_optional(os, m.clip_id) << ", output ";
	return print_optional(os, m.output_id) << ", section " << int{m.section} << "}";
}

/// @brief Prints an arrangement as each section's repetition count and each session macro, e.g.
///        `{sections [0 0 …], session_macros [{kind 0, …} …]}`.
/// @param os The stream to print to.
/// @param a The arrangement.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const Arrangement& a) {
	os << "{sections [";
	const char* separator = "";
	for (const Section& s : a.sections) {
		os << separator << s.num_repetitions;
		separator = " ";
	}
	os << "], session_macros [";
	separator = "";
	for (const SessionMacro& m : a.session_macros) {
		os << separator << m;
		separator = " ";
	}
	return os << "]}";
}

} // namespace deluge::project
