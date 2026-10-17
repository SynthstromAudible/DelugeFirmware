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

// The song's MIDI-learned bindings as the runtime reads and edits them: what a load read into the table, an owner's
// learned knobs mirrored from its runtime list, the sections' launch commands, and the table's entries projected onto
// the engine fields that play them.

#include "definitions_cxx.hpp" // Error
#include "deluge-project/controller_mapping.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"

#include <cstdint>
#include <vector>

class ModControllableAudio;
class Sound;
class Song;

namespace deluge::bridge {

/// @brief Put a binding a load read into @p song's table: it replaces its target's entry.
/// @param song    The song being loaded.
/// @param mapping The binding, not a knob (recordOwnerKnobs records those).
void recordLoadedMapping(::Song& song, const deluge::project::Mapping& mapping);

/// @brief The runtime's learned knobs of @p knobs as @p owner's table entries, in the runtime's order.
/// @param owner   Whose knobs.
/// @param knobs   The runtime owner holding them.
/// @param isSound Whether @p knobs is a sound (its unpatched params are a sound's).
/// @return The entries.
[[nodiscard]] std::vector<deluge::project::Mapping> knobsOf(deluge::project::ControlsAddress owner,
                                                            const ::ModControllableAudio& knobs, bool isSound);

/// @brief Make @p owner's knobs in @p song's table the runtime's learned knobs of @p knobs, in the runtime's order,
/// each with its relative flag; they take the place the owner's knobs held.
/// @param owner   Whose knobs.
/// @param knobs   The runtime owner holding them.
/// @param isSound Whether @p knobs is a sound (its unpatched params are a sound's).
/// @param song    The song whose table takes them.
void recordOwnerKnobs(deluge::project::ControlsAddress owner, const ::ModControllableAudio& knobs, bool isSound,
                      ::Song& song);

/// @brief Make @p sound's knobs in @p song's table its runtime's learned knobs (recordOwnerKnobs), when it is bound
/// as a synth track's or a kit row's sound.
/// @param sound The sound.
/// @param song  The song it belongs to.
void recordSoundKnobs(::Sound& sound, ::Song& song);

/// @brief Fill @p song's table from every binding its runtime read, once a load has read the song and given each
/// output and drum its id: the sections' commands it already holds stay first, then the song's knobs, then each
/// output's (its knobs, its input, and each of a kit's rows' knobs, mute and input) in output order, then each clip's
/// mute, session clips before arrangement-only ones.
/// @param song The song just read.
void recordLoadedMappings(::Song& song);

/// @brief Section @p section's launch command.
/// @param mappings The song's bindings.
/// @param section  The section's number.
/// @return The command, or an empty one when none is learned.
[[nodiscard]] deluge::project::MidiCommand sectionCommand(const deluge::project::MappingTable& mappings,
                                                          uint8_t section);

/// @brief Learn @p command as section @p section's launch command, or unlearn it when @p command holds nothing.
/// @param mappings The song's bindings.
/// @param section  The section's number.
/// @param command  The command.
void setSectionCommand(deluge::project::MappingTable& mappings, uint8_t section,
                       const deluge::project::MidiCommand& command);

/// @brief Make the engine field that plays @p target what @p song's table holds for it: an owner's learned knob list
/// (a knob the table leaves as it is stays whole, with its device and takeover state), a clip's or a kit row's mute
/// command, a track's or a kit row's input. A section's command lives in the table alone.
/// @param song   The song.
/// @param target The target to project.
/// @return Error::UNSPECIFIED, with nothing changed, when @p target's owner is not in @p song (no such output, drum or
///         clip, playing or hibernating; no such section); otherwise Error::NONE.
Error projectMapping(::Song& song, const deluge::project::MappingTarget& target);

/// @brief Bring the dispatch index of @p song's bindings into line with its table.
/// @param song The song.
void rebuildMappingIndex(::Song& song);

} // namespace deluge::bridge
