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

#include "definitions_cxx.hpp" // Error
#include "serialization/value.h"

#include <string_view>

class Song;
class Sound;

namespace deluge::bridge {

/// @brief Change @p sound's setting at @p path (a path of the Deluge instrument's settings schema) to @p value, as the
/// sound editor's menu for it does, side effects included: the runtime's own setter for the path.
/// @return Error::UNSPECIFIED when the schema does not allow @p value at @p path, or the path has no setter here.
Error applySetting(::Sound& sound, std::string_view path, const deluge::serialization::Value& value, ::Song& song);

} // namespace deluge::bridge
