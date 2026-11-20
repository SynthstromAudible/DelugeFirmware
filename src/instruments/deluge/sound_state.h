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

#include "instruments/deluge/composed_state.h"
#include "instruments/deluge/range_state.h"

namespace deluge::instrument {

struct FlatSoundState; // the file's flat layout: flat_layout.h, inside the library

/// @brief The Deluge instrument's settings for one sound, everything about it that is not a param: what it plays, and
/// what it keeps of what it does not play for a switch back. A default state is a new synth's, but for the two effect
/// settings that follow the song (with_song_defaults).
struct SoundState {
	ComposedState plays;
	Recall recall;

	SoundState() = default;
	/// @brief The record a flat state is (`flatten(SoundState{f}) == f` for every state a file decodes to or a rule
	/// makes).
	explicit SoundState(const FlatSoundState& flat);
	/// @brief The record in the file's flat layout.
	explicit operator FlatSoundState() const;
	bool operator==(const SoundState&) const = default;
};

} // namespace deluge::instrument
