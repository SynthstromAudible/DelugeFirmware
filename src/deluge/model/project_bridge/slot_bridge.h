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

#include "definitions_cxx.hpp" // Error
#include "deluge-project/instrument_slot.h"
#include "engines/deluge/codec.h"
#include "engines/deluge/sound_state.h"

#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>

class Kit;
class Output;
class Song;
class Sound;

namespace deluge::bridge {

/// @brief Fill @p slot from the element @p sound was read from (handed over once), or, for a sound not read from a
/// document, start it empty for the Deluge instrument. @p isDrum says whose keys the model owns in the element. The
/// slot is at @p song's input tick magnitude, which the element was read at.
///
/// @param sound  The sound.
/// @param slot   The slot to fill.
/// @param isDrum Whether the sound is a drum's.
/// @param song   The song the sound is read into.
void fillSlot(::Sound& sound, deluge::project::InstrumentSlot& slot, bool isDrum, const ::Song& song);

/// @brief Make every synth track's and sound kit row's slot what the song writes for it now: each such output is
/// written again into a ValueSerializer and its slot taken from that tree, the model's own keys left out.
///
/// @param song The song whose outputs are refreshed.
void refreshSlots(::Song& song);

/// @brief The slot of @p sound when it is bound: a synth track's (its output's id) or a sound kit row's (its drum's
/// id).
///
/// @param sound The sound.
/// @return The slot's address, or nullopt when the sound is not bound.
[[nodiscard]] std::optional<deluge::project::SlotAddress> slotAddressOf(::Sound& sound);

/// @brief The sound whose slot @p address names, among the song's outputs and hibernating instruments and their kits'
/// sound drums, or nullptr.
///
/// @param song    The song to search.
/// @param address The slot's address.
/// @return The sound, or nullptr.
[[nodiscard]] ::Sound* soundAt(::Song& song, deluge::project::SlotAddress address);

/// @brief The kit among the song's outputs whose output id is @p id, or nullptr.
///
/// @param song The song to search.
/// @param id   The kit's output id.
/// @return The kit, or nullptr.
[[nodiscard]] ::Kit* kitAt(::Song& song, uint32_t id);

/// @brief Make @p slot (@p sound's, as the model resolves it) hold the sound's state: the instrument's entries replaced
/// by the state's encoding at the slot's own tick magnitude, every other entry kept. Nothing for a sound that is not
/// bound.
///
/// @param sound The sound.
/// @param slot  The sound's slot.
/// @param song  The song the sound belongs to.
void syncSlotState(const ::Sound& sound, deluge::project::InstrumentSlot& slot, const ::Song& song);

/// @brief @p tree with the leaf at @p path (slash-separated) set to @p leaf, maps along the way made as needed, every
/// other entry kept in order.
///
/// @param tree The tree to start from.
/// @param path The leaf's slash-separated path.
/// @param leaf The leaf value.
/// @return The new tree.
[[nodiscard]] deluge::serialization::Value withLeaf(const deluge::serialization::Value& tree, std::string_view path,
                                                    deluge::serialization::Value leaf);

/// @brief Take @p sound's runtime from @p old to @p next: each settings row of the @p changed entries whose value
/// differs, in the table's order, through applySetting; then each source's DX patch and ranges where they differ. A row
/// of an entry the change did not touch is not compared: @p next holds what @p old does there.
///
/// @param sound   The sound to change.
/// @param old     The state the runtime holds.
/// @param next    The state to reach.
/// @param changed The entries the change touched.
/// @param song    The song the sound belongs to.
/// @return Error::NONE, or the first error an apply returned (the rows and sources after it are not applied).
Error applyDifference(::Sound& sound, const deluge::instrument::SoundState& old,
                      const deluge::instrument::SoundState& next, std::span<const std::string_view> changed,
                      ::Song& song);

/// @brief Take @p sound from its state to @p change's by difference: the setters of the entries it touched. The slot is
/// not touched: its tree is a cache of the state, which refreshSlot brings up to date when something reads it.
///
/// @param sound  The sound.
/// @param change The state to reach and the entries the change touched.
/// @param song   The song the sound belongs to.
/// @return Error::UNSPECIFIED when an apply is refused or the runtime did not reach the state.
Error commitState(::Sound& sound, const deluge::instrument::SlotChange& change, ::Song& song);

/// @brief @p slot's tree as @p sound's state encodes it now (syncSlotState): what a reader of a slot's tree calls
/// first, since writes change the state and leave the tree behind.
///
/// @param sound The sound playing the slot.
/// @param slot  Its slot.
/// @return The refreshed tree.
const deluge::serialization::Value& refreshSlot(const ::Sound& sound, deluge::project::InstrumentSlot& slot);

/// @brief The tree of the slot @p address names, refreshed from the sound playing it, or nullptr where @p song has no
/// such slot or sound.
///
/// @param song    The song.
/// @param address The slot's address.
/// @return The refreshed tree, or nullptr.
const deluge::serialization::Value* refreshedSlotState(::Song& song, deluge::project::SlotAddress address);

/// @brief A check of the typed writes, for the gates: when enabled, each setting, DX patch or ranges write is also made
/// the old way (its slot refreshed, the change written into the tree and the tree decoded), and each write whose state
/// or touched entries differ is counted.
struct TypedWriteAudit {
	bool enabled{false};
	uint32_t writes{0};     ///< writes checked
	uint32_t mismatches{0}; ///< of them, those the old way reads differently
	std::string first;      ///< the first mismatch, for the failure message
};

/// @brief The process's audit of typed writes.
/// @return The audit.
TypedWriteAudit& typedWriteAudit();

/// @brief When the audit is enabled, compare @p got with the old way's change: @p element_of (the refreshed tree in,
/// the changed tree out) decoded onto the sound's state.
///
/// @param sound      The sound, before the change is committed.
/// @param slot       Its slot.
/// @param element_of How the old way changed the tree.
/// @param got        The typed write's change.
/// @param what       What was written, for the failure message.
void auditTypedWrite(const ::Sound& sound, const deluge::project::InstrumentSlot& slot,
                     const std::function<deluge::serialization::Value(const deluge::serialization::Value&)>& element_of,
                     const std::expected<deluge::instrument::SlotChange, deluge::engine::StateError>& got,
                     std::string_view what);

/// @brief @p sound re-read into its record from the runtime, its slot refreshed and its controls record set from its
/// knobs, when it is bound in @p song.
///
/// @param sound The sound.
/// @param song  The song.
void recordLoadedSound(::Sound& sound, ::Song& song);

/// @brief Every bound sound of @p output (a synth's, or a kit's sound drums) re-read into its record from the runtime
/// and its slot refreshed: what loading made of it (an end clamped to a sample's length, millisecond zones in samples,
/// a path found in another folder) becomes what the record and slot say.
///
/// @param output The output.
/// @param song   The song.
void recordLoadedOutput(::Output& output, ::Song& song);

/// @brief recordLoadedOutput for every output and hibernating instrument of @p song, once a load's samples are in.
///
/// @param song The song.
void recordLoadedSounds(::Song& song);

} // namespace deluge::bridge
