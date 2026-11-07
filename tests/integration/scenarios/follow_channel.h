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

#include "io/midi/midi_engine.h"
#include "util/misc.h"

namespace scenarios {

/// @brief Follows MIDI channel 1 on any device, as settings > MIDI > FOLLOW > CHANNEL A does for a user.
///
/// The firmware follows no channel until one is set, and a scenario's DIN note script reaches the instrument in front
/// of the user through MIDI follow. Call it just before the script starts: the startup settings reset the follow
/// channels when the card has no MIDIFollow.XML.
inline void followChannelOne() {
	midiEngine.midiFollowChannelType[util::to_underlying(MIDIFollowChannelType::A)].channelOrZone = 0;
}

} // namespace scenarios
