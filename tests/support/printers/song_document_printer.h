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

#include "project/document.h"
#include "support/printers/doc_clip_printer.h"
#include "support/printers/doc_output_printer.h"
#include "support/printers/fx_chain_printer.h"
#include "support/printers/groove_printer.h"
#include "support/printers/list_printer.h"
#include "support/printers/mapping_printer.h"
#include "support/printers/song_settings_printer.h"

#include <format>
#include <ostream>
#include <span>

/// @file
/// @brief Prints a whole song's document in a failed CppSpec comparison, record by record.
///
/// CppSpec prints a value through `operator<<` when argument-dependent lookup finds one, and a type it cannot stream
/// as its address. The operator lives in the type's own namespace so that lookup finds it from inside CppSpec. Every
/// spec that compares the type includes this header: whether a type streams is decided once per type in a binary, so
/// a spec linked with others that print the same type must agree with them.
namespace deluge::project {

/// @brief Prints a clip's place in the song's clip order, e.g. `{id 3, audio 0, in_kit 0, arrangement_only 0}`.
/// @param os The stream to print to.
/// @param e The entry.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SongDocument::ClipEntry& e) {
	return os << std::format("{{id {}, audio {}, in_kit {}, arrangement_only {}}}", e.id, int{e.audio}, int{e.in_kit},
	                         int{e.arrangement_only});
}

/// @brief Prints a drum's place in its kit's order, e.g. `{id 7, element "sound"}`.
/// @param os The stream to print to.
/// @param e The entry.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SongDocument::DrumEntry& e) {
	return os << std::format("{{id {}, element \"{}\"}}", e.id, e.element);
}

/// @brief Prints an output's place in the song's output order, with its drums', e.g.
///        `{id 2, element "kit", drums [{id 7, element "sound"}]}`.
/// @param os The stream to print to.
/// @param e The entry.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SongDocument::OutputEntry& e) {
	os << std::format("{{id {}, element \"{}\", drums ", e.id, e.element);
	return deluge::test::printers::print_list(os, std::span{e.drums}) << "}";
}

/// @brief Prints a song's document field by field, each record through its own printer, e.g.
///        `{transport {…}, groove {…}, scale {…}, mod_knob_mode 1, reverb {…}, arrangement {…}, clip_order […], …}`.
/// @param os The stream to print to.
/// @param d The document.
/// @return @p os.
inline std::ostream& operator<<(std::ostream& os, const SongDocument& d) {
	using deluge::test::printers::print_by_id;
	using deluge::test::printers::print_list;
	os << "{transport " << d.transport << ", groove " << d.groove << ", scale " << d.scale << ", mod_knob_mode "
	   << int{d.mod_knob_mode} << ", reverb " << d.reverb << ", arrangement " << d.arrangement << ", clip_order ";
	print_list(os, std::span{d.clip_order}) << ", output_order ";
	print_list(os, std::span{d.output_order}) << ", clips ";
	print_by_id(os, d.clips) << ", audio_clips ";
	print_by_id(os, d.audio_clips) << ", outputs ";
	print_by_id(os, d.outputs) << ", drums ";
	print_by_id(os, d.drums) << ", mappings ";
	return os << d.mappings << ", master_effects " << d.master_effects << "}";
}

} // namespace deluge::project
