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

#include "deluge-project/midi_command.h"
#include "serialization/value.h"

#include <optional>

namespace deluge::project {

/// @brief A device reference as a song holds it: a port, or a hosted device's name and USB ids.
/// @param device The device reference to encode.
/// @return The encoded reference.
[[nodiscard]] serialization::Value encode_device(const MidiDeviceRef& device);

/// @brief A device reference as the device manager reads one: a known port, else a hosted device if the reference
/// names one or gives a vendor id, else none.
/// @param node The device reference as stored in the song.
/// @return The device, or nullopt when the reference names none.
[[nodiscard]] std::optional<MidiDeviceRef> decode_device(const serialization::Value& node);

} // namespace deluge::project
