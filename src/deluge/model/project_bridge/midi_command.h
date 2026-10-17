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
#include <optional>

class MIDICable;
class Serializer;

namespace deluge::bridge {

/// @brief The reference a song saves for @p cable.
/// @param cable The cable.
/// @return The device reference: a built-in port by which one, a hosted USB device by its name and ids, and nothing
///         for any other kind of cable, which a song has no way to name.
[[nodiscard]] std::optional<deluge::project::MidiDeviceRef> reference_of(MIDICable& cable);

/// @brief The cable @p device names, or nullptr if the Deluge knows no such device. Finds only: never creates or
/// updates a device entry (loading a song does that).
/// @param device The device reference.
/// @return The cable, or nullptr.
[[nodiscard]] MIDICable* find_cable(const deluge::project::MidiDeviceRef& device);

/// @brief The cable an engine field plays @p device from: the one find_cable() names, else, for a hosted device the
/// Deluge does not know, a device entry made for it as reading a song's reference to it makes one. No message arrives
/// on that entry's cable until the device is plugged in, which connects that same cable.
/// @param device The device reference.
/// @return The cable, or nullptr when there is no RAM for the entry.
[[nodiscard]] MIDICable* cable_for(const deluge::project::MidiDeviceRef& device);

/// @brief Whether MIDI from @p from on @p channel with @p noteOrCC triggers @p command: the same channel or zone and
/// note or CC and, when inputs are told apart by device, from the device it names (if it names one; a device the
/// Deluge does not know matches nothing).
/// @param command  The command.
/// @param from     The cable the MIDI came from.
/// @param channel  The incoming channel.
/// @param noteOrCC The incoming note or CC number.
/// @return True if the MIDI triggers @p command.
[[nodiscard]] bool matches(const deluge::project::MidiCommand& command, MIDICable& from, int32_t channel,
                           int32_t noteOrCC);

/// @brief The command as MIDI learn edits it. Finds its device only (see find_cable).
/// @param command The command.
/// @return The LearnedMIDI.
[[nodiscard]] LearnedMIDI learned_of(const deluge::project::MidiCommand& command);

/// @brief The engine field that plays @p command: its device's cable from cable_for(), so a binding to a device the
/// Deluge does not know listens to that device's entry and matches nothing while inputs are told apart by device.
/// @param command The command.
/// @return The LearnedMIDI.
[[nodiscard]] LearnedMIDI field_of(const deluge::project::MidiCommand& command);

/// @brief The command a MIDI learn produced.
/// @param learned The learned MIDI.
/// @return The command.
[[nodiscard]] deluge::project::MidiCommand command_of(const LearnedMIDI& learned);

/// @brief Write @p device as the `tagName` element a song holds, byte for byte as the cable itself writes it.
/// @param writer  The serializer to write to.
/// @param tagName The element's tag name.
/// @param device  The device reference.
void write_reference(Serializer& writer, char const* tagName, const deluge::project::MidiDeviceRef& device);

/// @brief Write @p command as the `tagName` element a song holds, byte for byte as LearnedMIDI writes it: its channel
/// or zone, its note or CC under @p midiMessageType's key, and its device as a `device` element. Nothing when it holds
/// nothing.
/// @param writer          The serializer to write to.
/// @param tagName         The element's tag name.
/// @param command         The command.
/// @param midiMessageType MIDI_MESSAGE_NOTE or MIDI_MESSAGE_CC: which key its note or CC is written under.
void write_command(Serializer& writer, char const* tagName, const deluge::project::MidiCommand& command,
                   int32_t midiMessageType);

} // namespace deluge::bridge
