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

/// @file
/// @brief What a control reads that the model does not hold: the engine's live values and runtime state.

#include "deluge-project/instrument_slot.h"
#include "deluge-project/param_address.h"

#include <cstdint>
#include <optional>

namespace deluge::controls {

/// @brief The facts a gold knob reads that the model does not hold, each answered by the host.
class Facts {
public:
	virtual ~Facts() = default;
	/// @brief The value the param at @p address plays now: the engine's cursor, which automation moves at the
	/// playhead, a latch or override holds, and stutter-type effects and recording change, where the record holds the
	/// saved base value; nullopt where the song holds no such param.
	[[nodiscard]] virtual std::optional<int32_t> param_value(const project::ParamAddress& address) const = 0;
	/// @brief @p address's value at @p pos in its clip, as its automation reads there (the take's while the
	/// arrangement records the clip, in the clip's or the note row's own loop); its current value at 0xFFFFFFFF.
	[[nodiscard]] virtual std::optional<int32_t> param_value_at(const project::ParamAddress& address,
	                                                            uint32_t pos) const = 0;
	/// @brief Whether a stutter is playing: the song's stutter, which a gold knob's push begins, is live state no
	/// record holds.
	[[nodiscard]] virtual bool stutter_active() const = 0;
	/// @brief Whether the sound drum at @p slot can hold note tails, which its kit-arp inclusion needs: its params
	/// (envelope sustain, noise) decide it, which no setting holds.
	[[nodiscard]] virtual bool allows_note_tails(project::SlotAddress slot) const = 0;
};

} // namespace deluge::controls
