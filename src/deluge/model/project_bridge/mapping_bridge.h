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
#include <optional>
#include <span>
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

/// @brief [task] [audio] Give output @p to copies of output @p from's learned knobs in @p song's table (in place of its
/// own), and project them onto @p to's knob list, as an audio track cloned from another takes its knobs: at once on the
/// app loop, held for it on the render (an auto-overdub's clone on the song's tick), whose engine knobs, copied with
/// the rest of the output, play meanwhile.
/// @param from The output cloned from.
/// @param to   The clone.
/// @param song The song both belong to.
void cloneOutputMappings(const ::Output& from, ::Output& to, ::Song& song);

/// @brief Whether the caller is the render (the song's tick included), which never writes the mapping table: a table
/// write it makes to the current song's is held for the app loop (applyPendingMappingOps). The render is the queue's
/// only producer; the app loop writes the table itself.
/// @return True on the audio tier, or inside the host's render block.
[[nodiscard]] bool onRenderTier();

/// @brief How many table writes the render can hold before the app loop applies them. Past that, the app loop brings
/// the table into line with what the song holds (drops entries of owners gone, records what owners play that it holds
/// nothing for): coarser, but the same table.
///
/// The render holds a write only where the song's tick changes what the song holds. An audio clip that overdubs holds
/// at most three per loop it crosses: the clone of its track's knobs for the overdub's new track, then, when a pending
/// overdub is dropped, its clip's mute and its track's bindings. (A new overdub track brings no bindings of its own, so
/// its arrival holds nothing.) The writes wait while a kit change moves its rows' commands (RowCommandMoves), which
/// can last as long as the change loads its samples, a few seconds. 32 holds ten overdubbing clips each crossing one
/// loop in that time, or three crossing three; more than that falls back to the reconcile.
inline constexpr size_t kPendingMappingOps = 32;

/// @brief [audio] [task] Drop clip @p clip's mute command from @p song's table: at once on the app loop, held for it on
/// the render (a pending overdub deleted on the song's tick).
/// @param song The current song.
/// @param clip The clip's id.
void forgetClipMappings(::Song& song, uint32_t clip);

/// @brief [audio] [task] Drop output @p output's knobs and input from @p song's table, as forgetClipMappings() does.
/// @param song   The current song.
/// @param output The output's id.
void forgetOutputMappings(::Song& song, uint32_t output);

/// @brief [audio] [task] Drop kit row @p drum's knobs, mute and input from @p song's table, as forgetClipMappings()
/// does.
/// @param song The current song.
/// @param drum The row's id.
void forgetDrumMappings(::Song& song, uint32_t drum);

/// @brief What a drain of the held writes does when the render lost some (applyPendingMappingOps).
enum class OverflowReconcile : uint8_t {
	NOW,   ///< reconcile the table with the song at once
	DEFER, ///< leave the loss noted and the index stale, for the next drain: the caller holds the render off
};

/// @brief [task] Apply, in order and with the render excluded, the table writes the render held for @p song; those it
/// held for another song (one a swap replaced) are dropped. Run by every rebuild of the mapping index and before an
/// app-loop write to the table, so a write the render made first lands first. When the render lost some, the table is
/// reconciled with what the song holds, which excludes the render only where it must: a caller that holds the render
/// off itself defers that.
/// @param song      The current song.
/// @param reconcile Whether a reconcile the loss calls for runs now or waits for the next drain.
/// @return Whether a reconcile was deferred: the index is to stay stale for it.
bool applyPendingMappingOps(::Song& song, OverflowReconcile reconcile = OverflowReconcile::NOW);

/// @brief [task] [audio] Drop the table writes the render held: the song they were for is no longer current.
void discardPendingMappingOps();

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

/// @brief Project again every binding of @p song whose source names a hosted device, as the device list now resolves
/// it: a device plugged in since its binding was projected is played from its own cable. Makes the device entries
/// first, then excludes the render only while it projects each target.
/// @param song The song.
void projectDeviceMappings(::Song& song);

/// @brief [task] The rows' commands (every kit row's mute and input) as a kit change found them in the table, before it
/// moves them (recordMovedDrumCommands reads them): only those, as a change runs once per clip of a kit and the rest of
/// the table plays no part in a move. The writes the render held land first; those it holds from then on wait until
/// the last one open closes, so none lands between the commands as they were and the moves recorded from them, where a
/// moved command could undo it.
class RowCommandMoves {
public:
	/// @param song The current song.
	explicit RowCommandMoves(::Song& song);
	/// Applies the writes the render held meanwhile, when it is the last one open.
	~RowCommandMoves();
	RowCommandMoves(const RowCommandMoves&) = delete;
	RowCommandMoves& operator=(const RowCommandMoves&) = delete;
	RowCommandMoves(RowCommandMoves&&) = delete;
	RowCommandMoves& operator=(RowCommandMoves&&) = delete;

	/// @brief The rows' commands as they were, in table order.
	[[nodiscard]] std::span<const deluge::project::Mapping> before() const { return before_; }

private:
	::Song& song_;
	std::vector<deluge::project::Mapping> before_;
};

/// @brief Make @p kit's rows' mutes and inputs in @p song's table what the runtime holds after it moved them between
/// rows: a kit change gives a replaced kit's row commands to the clips' rows, and they give them to the new kit's rows.
/// A row's command keeps the source @p before held for it, or for the same kind of command of one of @p fromRows, so a
/// device the runtime could not name stays named. The writes the render holds land after the moves, not before.
/// @param kit      The kit whose rows' commands moved.
/// @param before   The rows' commands as the table held them before the move (RowCommandMoves::before); other entries
///                 are passed over.
/// @param fromRows The ids of the rows the commands came from (rowIdsOf the kit they left), empty when not known.
/// @param song     The song.
void recordMovedDrumCommands(::Kit& kit, std::span<const deluge::project::Mapping> before,
                             std::span<const uint32_t> fromRows, ::Song& song);

/// @brief The ids of @p output's kit rows: none when it is not a kit.
/// @param output The output, or nullptr.
/// @return The ids, in row order.
[[nodiscard]] std::vector<uint32_t> rowIdsOf(const ::Output* output);

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

/// @brief projectMapping() of @p target after a relearn put @p source in place of the knob that played @p replaced:
/// the knob that now plays @p source keeps that knob's takeover state, as a knob learned again in place always has.
/// @param song     The song.
/// @param target   The param the knob turns.
/// @param replaced The source of the knob the relearn replaced.
/// @param source   The source it learned.
/// @return As projectMapping().
Error projectRelearnedKnob(::Song& song, const deluge::project::ParamTarget& target,
                           const deluge::project::MidiCommand& replaced, const deluge::project::MidiCommand& source);

/// @brief Whether @p knob plays what an index hit says the knob at its place does: @p noteOrCC, on @p target's param.
/// @param knob     The knob at the hit's place in its owner's list.
/// @param target   The hit's target.
/// @param noteOrCC The hit's CC number (128 for pitch bend).
/// @return True if it does.
[[nodiscard]] bool knobPlays(const ::MIDIKnob& knob, const deluge::project::MappingTarget& target, uint8_t noteOrCC);

/// @brief Whether @p song holds @p output, playing or hibernating: a save then writes its bindings from the table.
/// @param song   The song.
/// @param output The output.
/// @return True if @p output is one of @p song's.
[[nodiscard]] bool holdsOutput(::Song& song, const ::Output& output);

/// @brief Whether @p song holds @p drum, in any kit, playing or hibernating: a save then writes its bindings from the
/// table.
/// @param song The song.
/// @param drum The kit row.
/// @return True if @p drum is one of @p song's.
[[nodiscard]] bool holdsDrum(::Song& song, const ::Drum& drum);

/// @brief The learned knobs a save writes for @p owner: those @p song's table holds for @p address, in table order,
/// when @p address names @p owner in @p song.
/// @param song    The song.
/// @param owner   The runtime owner being saved.
/// @param address Whose controls @p owner's are, if known.
/// @return The knobs, or nullopt when @p song does not hold @p owner at @p address (a save then has no table to read).
[[nodiscard]] std::optional<std::vector<deluge::project::Mapping>>
savedKnobsOf(::Song& song, const ::ModControllableAudio& owner,
             std::optional<deluge::project::ControlsAddress> address);

} // namespace deluge::bridge
