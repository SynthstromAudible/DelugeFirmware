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

#include "serialization/value.h"

#include <string_view>

class ModControllableAudio;

namespace deluge::edits {

/// @brief A gesture's change of @p owner's effect setting at @p path (a path of the effects' schema) to @p value (its
/// Edit form): for a sound bound in the current song, a SetEngineSetting on its slot, so the slot keeps holding its
/// effects; for any other owner (a kit, an audio track, the song master, a sound outside the song), its setter.
void changeEffect(ModControllableAudio& owner, std::string_view path, deluge::serialization::Value value);

} // namespace deluge::edits
