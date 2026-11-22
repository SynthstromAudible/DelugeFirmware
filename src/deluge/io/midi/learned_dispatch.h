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
/// @brief Where an incoming CC or pitch bend reaches the current song's learned knobs (ui/controls/learned.h), through
///        the app's facts, sink and takeover states. Defined with the scenes' host, which holds them
///        (gui/ui/shell_host/learned_dispatch.cpp).

#include <cstdint>

class MIDICable;
class Output;

namespace deluge::midi {

/// @brief Move the params of the song's learned knobs a CC moves (controls::dispatch_song_cc). The song hears a CC on
/// any channel, an MPE member channel included.
/// @param cable   The cable it came on.
/// @param channel Its channel, raw.
/// @param cc      The CC number.
/// @param value   The CC's value.
void learnedCcToSong(MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value);

/// @brief Move the params of @p output's learned knobs a CC moves (controls::dispatch_output_cc): its own, then a kit's
/// rows'. Called for each output in play order, before the output hears the CC itself: a hit makes the take the
/// arrangement records the output's clip into, which the output's own offer then sees as its active clip.
/// @param output  The output.
/// @param cable   The cable it came on.
/// @param channel Its channel, raw.
/// @param cc      The CC number.
/// @param value   The CC's value.
void learnedCcToOutput(const Output& output, MIDICable& cable, uint8_t channel, uint8_t cc, uint8_t value);

/// @brief Move the params of @p output's learned knobs a pitch bend moves (controls::dispatch_output_bend), as
/// learnedCcToOutput() does a CC's.
/// @param output  The output.
/// @param cable   The cable it came on.
/// @param channel Its channel, raw.
/// @param value14 The bend's 14-bit value.
/// @return Whether a learned knob of @p output hears the bend: then its instrument does not bend.
[[nodiscard]] bool learnedPitchBendToOutput(const Output& output, MIDICable& cable, uint8_t channel, uint16_t value14);

} // namespace deluge::midi
