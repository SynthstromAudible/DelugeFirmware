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

// A model MIDI command names its device by reference; these resolve it to the live cable, match incoming MIDI with
// LearnedMIDI's rule, and convert to and from the LearnedMIDI that MIDI learn writes into.

#include "deluge-project/midi_command.h"
#include "io/midi/learned_midi.h"

#include <cstdint>

class MIDICable;
class Serializer;

namespace deluge::bridge {

/// @brief The reference a song saves for @p cable.
[[nodiscard]] deluge::project::MidiDeviceRef reference_of(MIDICable& cable);

/// @brief The cable @p device names, or nullptr if the Deluge knows no such device. Finds only: never creates or
/// updates a device entry (loading a song does that).
[[nodiscard]] MIDICable* find_cable(const deluge::project::MidiDeviceRef& device);

/// @brief Whether MIDI from @p from on @p channel with @p noteOrCC triggers @p command: the same channel or zone and
/// note or CC and, when inputs are told apart by device, from the device it names (if it names one; a device the
/// Deluge does not know matches nothing).
[[nodiscard]] bool matches(const deluge::project::MidiCommand& command, MIDICable& from, int32_t channel,
                           int32_t noteOrCC);

/// @brief The command as MIDI learn edits it.
[[nodiscard]] LearnedMIDI learned_of(const deluge::project::MidiCommand& command);

/// @brief The command a MIDI learn produced.
[[nodiscard]] deluge::project::MidiCommand command_of(const LearnedMIDI& learned);

/// @brief Write @p device as the `tagName` element a song holds, byte for byte as the cable itself writes it.
void write_reference(Serializer& writer, char const* tagName, const deluge::project::MidiDeviceRef& device);

} // namespace deluge::bridge
