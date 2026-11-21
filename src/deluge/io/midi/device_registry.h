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
/// @brief The host's map between the scenes' MIDI device handles and the cables they name. Cable objects are never
///        destroyed while the firmware runs (the fixed ports are statics a factory reset re-constructs in place;
///        hosted devices are allocated once and kept), so a cable's handle is fixed for the session.
///
/// Both functions run on the app loop's task only (a MIDI learn offer reaches the shell from
/// `PlaybackHandler::midiRoutine`, an app-loop job, and the menu runs on the same loop), so the table takes no lock.

#include "io/midi/midi_device.h" // MIDICable: `cable_of`'s callers read and write the cable it returns
#include "midi/device_id.h"

namespace deluge::midi {

/// @brief @p cable's handle: a fixed port's constant, or a hosted device's, given the first time it is asked for.
///
/// Every cable gets one. The hosted table grows with the cables named (one pointer each, on the app loop's task), and
/// a handle is 32 bits: a hosted cable is a distinct heap object of more than one byte, so the device's 32-bit address
/// space holds fewer of them than there are handles.
/// @param cable The cable to name.
/// @return The handle; none only where the heap cannot grow the table by one pointer, the same exhaustion in which
///         `MIDIDeviceManager` refuses to make a hosted cable at all. A learn offer whose event names none is not
///         taken by any scene: `ShellHostUI` passes it on to the base `UI` learn answer, and when that does not use it
///         either, `PlaybackHandler` handles the message as usual.
[[nodiscard]] DeviceId id_of(const ::MIDICable& cable);

/// @brief The cable @p id names.
/// @param id A handle @ref id_of gave, or none.
/// @return The cable, or null for none or a handle never given.
[[nodiscard]] ::MIDICable* cable_of(DeviceId id);

} // namespace deluge::midi
