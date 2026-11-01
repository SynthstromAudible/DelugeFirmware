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

#include "deluge-project/controller_mapping.h"
#include "deluge-project/mapping_table.h"
#include "plugin-api/types/param_naming.h"
#include "serialization/value.h"

#include <cstdint>
#include <variant>

namespace deluge::project {

/// @brief A clip, as the owner of its mute command.
struct ClipOwner {
	uint32_t clip_id{0};
	bool operator==(const ClipOwner&) const = default;
};
/// @brief A kit row, as the owner of its mute command and its input.
struct DrumOwner {
	uint32_t drum_id{0};
	bool operator==(const DrumOwner&) const = default;
};
/// @brief An instrument track, as the owner of its input: a kit's names its root note.
struct TrackOwner {
	uint32_t output_id{0};
	bool kit{false};
	bool operator==(const TrackOwner&) const = default;
};
/// @brief Whose element a binding is read from or written to: the knobs of an owner's controls, a clip, a kit row or
/// a track.
using MappingOwner = std::variant<ControlsAddress, ClipOwner, DrumOwner, TrackOwner>;

/// @brief Read into @p table the bindings @p owner's @p element holds, each owner kind by its own keys:
/// - an owner's controls: `midiKnobs` (each knob's `channel` or `mpeZone`, `ccNumber`, `relative`, the param
///   @p naming gives `controlsParam` and its patch sources, and `device`); a knob @p naming cannot name, or any knob
///   when @p naming is null, is left out;
/// - a clip: `muteMidiCommand`;
/// - a kit row: `midiMuteCommand` and `midiInput`;
/// - a track: `inputMidiChannel` or `inputMPEZone`, and `inputMidiDevice`; a kit's `MIDIInput`, with its root note.
/// The owner's knobs replace any it had; any other command replaces its target's entry. A command naming no channel
/// is nothing learned.
/// @param element The owner's element.
/// @param owner   Whose bindings these are.
/// @param table   The table to fill.
/// @param naming  How the owner's engine names its params.
void decode_mappings(const serialization::Value& element, MappingOwner owner, MappingTable& table,
                     const deluge::plugin::ParamNaming* naming = nullptr);

/// @brief Write @p owner's bindings in @p table into @p element as decode_mappings reads them.
/// @param table   The table holding them.
/// @param owner   Whose bindings to write.
/// @param element The owner's element.
/// @param naming  How the owner's engine names its params; with none, no knob is written.
void encode_mappings(const MappingTable& table, MappingOwner owner, serialization::Value& element,
                     const deluge::plugin::ParamNaming* naming = nullptr);

/// @brief Read section @p section's launch command from its `section` element (`midiCommandChannel`,
/// `midiCommandNote`, `midiCommandDevice`) into @p table: a program change or a note out of range is nothing learned.
/// @param node    The section's element.
/// @param section The section's number.
/// @param table   The table to fill.
void decode_section_command(const serialization::Value& node, uint8_t section, MappingTable& table);

/// @brief Write section @p section's launch command in @p table into its `section` element as
/// decode_section_command reads it; nothing when it has none.
/// @param table   The table holding it.
/// @param section The section's number.
/// @param node    The section's element.
void encode_section_command(const MappingTable& table, uint8_t section, serialization::Value& node);

} // namespace deluge::project
