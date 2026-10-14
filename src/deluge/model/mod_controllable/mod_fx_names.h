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

#include "definitions_cxx.hpp"
#include "util/containers.h"

#include <string_view>

/// @brief The mod FX types' and their knobs' display names, for the menus and the OLED.
namespace modfx {

/// @brief Each mod FX type's name.
/// @return The names, indexed by ModFXType value.
deluge::vector<std::string_view> getModNames();

/// @brief The name of @p type's knob @p param: the grain names its knobs apart from the delay-line types.
/// @param type      The mod FX type.
/// @param param     The knob to name.
/// @param shortName Whether to give the short form, for a column label.
/// @return The knob's name.
const char* getParamName(ModFXType type, ModFXParam param, bool shortName = false);

/// @brief @p type's name.
/// @param type The mod FX type.
/// @return The type's name.
const char* modFXToString(ModFXType type);

} // namespace modfx
