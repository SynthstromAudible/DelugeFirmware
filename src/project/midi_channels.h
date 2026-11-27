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
/// @brief The channel values beyond the sixteen ordinary MIDI channels that a MIDI output can hold.

#include <cstdint>

namespace deluge::project {

/// @brief The MIDI channel value that sends a MIDI output's notes to the song's transpose instead of a channel.
inline constexpr int32_t kMidiChannelTranspose = 55;
/// @brief The MIDI channel value naming the lower MPE zone, numbered straight after the sixteen ordinary channels.
inline constexpr int32_t kMidiChannelLowerZone = 16;
/// @brief The MIDI channel value naming the upper MPE zone.
inline constexpr int32_t kMidiChannelUpperZone = 17;
/// @brief How many channel values an input can follow: the sixteen channels and the two MPE zones.
inline constexpr int32_t kMidiChannelCount = kMidiChannelUpperZone + 1;

} // namespace deluge::project
