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

#include "deluge-project/arp_record.h"

#include <cstdint>
#include <optional>

class ArpeggiatorSettings;
class Serializer;

namespace deluge::bridge {

/// @brief @p settings as an arp record.
/// @param settings   The engine's arp.
/// @param hostValues Whether its host values are settings (a MIDI or CV clip's arp, a MIDI or gate kit row's); where
///                   they are params the record leaves them absent.
/// @return The record.
[[nodiscard]] project::ArpRecord arpRecordOf(const ArpeggiatorSettings& settings, bool hostValues);

/// @brief Write an arp element's attributes: its settings from @p record, what the arp generated (the randomizer's
///        locked values, the note pattern) from @p settings, then, where they are settings, its host values.
/// @param writer        The serializer, inside the element's opening tag.
/// @param settings      The engine's arp.
/// @param record        The arp's record; null for an arp that has none (one being built), which writes @p settings'.
/// @param hostValues    Whether the host values are settings (see arpRecordOf). A host value the record does not hold
///                      is written from @p settings.
/// @param tickMagnitude The song's input tick magnitude for a sound's element, which holds the sync level relative to
///                      it; nullopt for a clip's or a non-audio kit row's.
void writeArpAttributes(Serializer& writer, const ArpeggiatorSettings& settings, const project::ArpRecord* record,
                        bool hostValues, std::optional<int32_t> tickMagnitude);

} // namespace deluge::bridge
