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
/// @brief MIDI takeover: where a controller's value moves a knob, as a pure function of the knob, the value, the global
///        mode and what the controller last sent.

#include "project/mapping_table.h"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace deluge::ui::controls {

/// @brief How a controller that does not match the knob's position takes it over.
enum class TakeoverMode : uint8_t {
	JUMP,     ///< the knob goes to the controller's value
	PICKUP,   ///< the knob stays until the controller crosses it
	SCALE,    ///< the knob moves toward its end as the controller moves toward its own
	RELATIVE, ///< the controller's value is an offset
};

/// @brief What a takeover reads besides the knob: the global mode, and what forces a jump or a relative reading.
struct TakeoverInputs {
	TakeoverMode mode{TakeoverMode::JUMP};
	bool cable_relative{false}; ///< the cable sends relative values
	bool knob_relative{false};  ///< the knob's own mapping is relative
	bool recording{false};      ///< a clock runs and the song records: jump
	bool step_editing{false};   ///< a step region is held: jump
};

/// @brief One controller's remembered position.
struct TakeoverState {
	std::optional<int32_t> previous; ///< the last position the controller sent, once it has sent one
	[[nodiscard]] bool operator==(const TakeoverState&) const = default;
};

/// @brief The knob position a controller value moves a knob at @p knob_pos to, updating @p state.
///
/// A relative reading (the cable's, the knob's or the global mode's) adds the value as a signed offset and keeps the
/// knob from falling below -64 or its current position, whichever is lower. A jump (the JUMP mode, a recording song, a
/// held step region) takes the controller's position. PICKUP and SCALE take it once the controller has met the knob's
/// position from either side; until then PICKUP holds still and SCALE moves the knob so both reach an end together.
/// The first value of a PICKUP or SCALE reading seeds @p state and takes the controller's position.
/// @param knob_pos The knob's position, -64..64.
/// @param value    The controller's value, 0..127; 127 reads as +64 where a position is wanted.
/// @param inputs   The mode and what forces a jump or a relative reading.
/// @param state    The controller's remembered position, updated.
/// @return The knob's new position, -64..64.
[[nodiscard]] int32_t knob_pos_after(int32_t knob_pos, uint8_t value, TakeoverInputs inputs, TakeoverState& state);

/// @brief The takeover state of every learned knob, by (owner, param, source): the same key as the mapping table's
/// entry, so a table reorder, a flipped relative flag and an unchanged re-index all leave it where it is.
///
/// App loop only: it allocates, and the render never reaches it (the mapping table's writes the render makes are held
/// for the app loop, which applies them here with the table). It holds the current song's knobs.
class TakeoverStore {
public:
	/// @brief The state of the knob @p source plays on @p target, made empty when the knob has none yet.
	/// @return The state, valid until the next call that adds or drops one.
	[[nodiscard]] TakeoverState& at(const project::OwnerParamTarget& target, const project::MidiCommand& source);

	/// @brief The state of the knob @p source plays on @p target, or null when it has none.
	[[nodiscard]] const TakeoverState* find(const project::OwnerParamTarget& target,
	                                        const project::MidiCommand& source) const;

	/// @brief Move the state of the knob @p from plays on @p target to the knob @p to plays, as a relearn in place
	/// does. The state the knob @p to had is replaced: by @p from's, or by none when @p from has none (a knob that has
	/// not seen a position takes the surviving knob's place whole).
	void relearn(const project::OwnerParamTarget& target, const project::MidiCommand& from,
	             const project::MidiCommand& to);

	/// @brief Make @p to's states copies of @p from's, as a cloned owner's knobs are: @p to's own are dropped first.
	void clone_owner(project::ControlsAddress from, project::ControlsAddress to);

	/// @brief Drop every state of @p owner's knobs.
	void forget_owner(project::ControlsAddress owner);

	/// @brief Drop the state of the knob @p source plays on @p target.
	void forget(const project::OwnerParamTarget& target, const project::MidiCommand& source);

	/// @brief Drop every state, as a song load does.
	void clear();

	/// @brief How many knobs hold a state.
	[[nodiscard]] size_t size() const { return states_.size(); }

private:
	struct Held {
		project::OwnerParamTarget target;
		project::MidiCommand source;
		TakeoverState state;
	};
	[[nodiscard]] std::vector<Held>::iterator locate(const project::OwnerParamTarget& target,
	                                                 const project::MidiCommand& source);

	std::vector<Held> states_; ///< a handful of learned knobs: a scan, with no allocation for one already held
};

} // namespace deluge::ui::controls
