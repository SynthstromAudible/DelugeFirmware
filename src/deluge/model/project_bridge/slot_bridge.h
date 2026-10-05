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

#include "deluge-project/instrument_slot.h"

#include <optional>

class Output;
class Song;
class Sound;

namespace deluge::bridge {

/// @brief Fill @p slot from the element @p sound was read from (handed over once), or, for a sound not read from a
/// document, start it empty for the Deluge instrument. @p isDrum says whose keys the model owns in the element. The
/// slot is at @p song's input tick magnitude, which the element was read at.
void fillSlot(::Sound& sound, deluge::project::InstrumentSlot& slot, bool isDrum, const ::Song& song);

/// @brief Make every synth track's and sound kit row's slot what the song writes for it now: each such output is
/// written again into a ValueSerializer and its slot taken from that tree, the model's own keys left out.
void refreshSlots(::Song& song);

/// @brief The slot of @p sound when it is bound: a synth track's (its output's id) or a sound kit row's (its drum's
/// id).
[[nodiscard]] std::optional<deluge::project::SlotAddress> slotAddressOf(::Sound& sound);

/// @brief The sound whose slot @p address names, among the song's outputs and hibernating instruments and their kits'
/// sound drums, or nullptr.
[[nodiscard]] ::Sound* soundAt(::Song& song, deluge::project::SlotAddress address);

/// @brief Make @p slot (@p sound's, as the model resolves it) hold the sound's state: the instrument's entries replaced
/// by the state's encoding, every other entry kept. Nothing for a sound that is not bound.
void syncSlotState(const ::Sound& sound, deluge::project::InstrumentSlot& slot, const ::Song& song);

/// @brief @p sound re-read into its record from the runtime, its slot refreshed and its controls record set from its
/// knobs, when it is bound in @p song.
void recordLoadedSound(::Sound& sound, ::Song& song);

/// @brief Every bound sound of @p output (a synth's, or a kit's sound drums) re-read into its record from the runtime
/// and its slot refreshed: what loading made of it (an end clamped to a sample's length, millisecond zones in samples,
/// a path found in another folder) becomes what the record and slot say.
void recordLoadedOutput(::Output& output, ::Song& song);

/// @brief recordLoadedOutput for every output and hibernating instrument of @p song, once a load's samples are in.
void recordLoadedSounds(::Song& song);

} // namespace deluge::bridge
