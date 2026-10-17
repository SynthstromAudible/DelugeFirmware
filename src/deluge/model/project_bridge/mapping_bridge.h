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

// The song's MIDI-learned bindings as the runtime reads and edits them: what a load read into the table, what an owner
// arriving in a running song brings, the table's side of the runtime's structural steps (a preset change, a clone, a
// kit change), the sections' launch commands, and the table's entries projected onto the engine fields that play them.

#include "definitions_cxx.hpp" // Error
#include "deluge-project/controller_mapping.h"
#include "deluge-project/mapping_table.h"
#include "deluge-project/midi_command.h"

#include <cstdint>
#include <vector>

class Drum;
class Kit;
class MIDIKnob;
class ModControllableAudio;
class Output;
class Song;

namespace deluge::bridge {

/// @brief Put a binding a load read into @p song's table: it replaces its target's entry.
/// @param song    The song being loaded.
/// @param mapping The binding, not a knob.
void recordLoadedMapping(::Song& song, const deluge::project::Mapping& mapping);

/// @brief The runtime's learned knobs of @p knobs as @p owner's table entries, in the runtime's order.
/// @param owner   Whose knobs.
/// @param knobs   The runtime owner holding them.
/// @param isSound Whether @p knobs is a sound (its unpatched params are a sound's).
/// @return The entries.
[[nodiscard]] std::vector<deluge::project::Mapping> knobsOf(deluge::project::ControlsAddress owner,
                                                            const ::ModControllableAudio& knobs, bool isSound);

/// @brief Put into @p song's table each binding @p output brings into it that the table holds nothing for: its learned
/// knobs, its input, and a kit's rows' knobs, mutes and inputs, as the runtime read them.
/// @param output An output joining @p song, with its id.
/// @param song   The song it joins.
void recordArrivingOutput(::Output& output, ::Song& song);

/// @brief Put into @p song's table each binding @p drum brings into it that the table holds nothing for: a sound row's
/// learned knobs, its mute and its input, as the runtime read them.
/// @param drum A kit row joining @p song, with its id.
/// @param song The song it joins.
void recordArrivingDrum(::Drum& drum, ::Song& song);

/// @brief Give output @p to copies of output @p from's learned knobs in @p song's table (in place of its own), and
/// project them onto @p to's knob list, as an audio track cloned from another takes its knobs.
/// @param from The output cloned from.
/// @param to   The clone.
/// @param song The song both belong to.
void cloneOutputMappings(const ::Output& from, ::Output& to, ::Song& song);

/// @brief Hand output @p from's bindings to output @p to in @p song's table, as a preset change hands the replaced
/// instrument's over: its input always, its learned knobs too when @p knobs. What @p to held of either is dropped.
/// @param song  The song.
/// @param from  The replaced instrument's id.
/// @param to    The replacing instrument's id.
/// @param knobs Whether the learned knobs move too.
void handOverInstrument(::Song& song, uint32_t from, uint32_t to, bool knobs);

/// @brief Project the table's learned knobs and input of output @p output onto its engine fields.
/// @param song   The song.
/// @param output The output's id.
void projectOutputMappings(::Song& song, uint32_t output);

/// @brief Make @p kit's rows' mutes and inputs in @p song's table what the runtime holds after it moved them between
/// rows (a kit change gives a replaced kit's row commands to the new kit's rows). A row's command keeps the source an
/// entry of @p before gave it, so a device the runtime could not name stays named.
/// @param kit    The kit whose rows' commands moved.
/// @param before The table as it was before the move.
/// @param song   The song.
void recordMovedDrumCommands(::Kit& kit, const deluge::project::MappingTable& before, ::Song& song);

/// @brief Clear the relative flag of @p knob's entry in @p song's table: a CC value only an absolute controller sends
/// arrived on it. A plain table write, run from the MIDI input path.
/// @param owner The knob's owner.
/// @param knob  The knob, its own flag already cleared.
/// @param song  The song @p owner belongs to.
void knobNoLongerRelative(::ModControllableAudio& owner, const ::MIDIKnob& knob, ::Song& song);

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
