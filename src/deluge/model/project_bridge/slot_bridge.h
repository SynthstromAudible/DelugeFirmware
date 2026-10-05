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

class Song;
class Sound;

namespace deluge::bridge {

/// @brief Fill @p slot from the element @p sound was read from (handed over once), or, for a sound not read from a
/// document, start it empty for the Deluge instrument. @p isDrum says whose keys the model owns in the element.
void fillSlot(::Sound& sound, deluge::project::InstrumentSlot& slot, bool isDrum);

/// @brief Make every synth track's and sound kit row's slot what the song writes for it now: each such output is
/// written again into a ValueSerializer and its slot taken from that tree, the model's own keys left out.
void refreshSlots(::Song& song);

/// @brief The slot of @p sound when it is bound: a synth track's (its output's id) or a sound kit row's (its drum's
/// id).
[[nodiscard]] std::optional<deluge::project::SlotAddress> slotAddressOf(::Sound& sound);

/// @brief The sound whose slot @p address names, among the song's outputs and hibernating instruments and their kits'
/// sound drums, or nullptr.
[[nodiscard]] ::Sound* soundAt(::Song& song, deluge::project::SlotAddress address);

/// @brief Make @p sound's slot hold its state: the instrument's entries replaced by the state's encoding, every other
/// entry kept. Nothing for a sound that is not bound.
void syncSlotState(::Sound& sound, ::Song& song);

} // namespace deluge::bridge
