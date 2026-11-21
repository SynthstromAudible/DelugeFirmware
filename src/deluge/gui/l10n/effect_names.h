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
/// @brief The effects' display names, in the current language, keyed by their neutral words.

#include "effects/words.h"

#include <cstddef>
#include <string_view>

namespace deluge::l10n {

/// @brief The mod-FX type @p type's display name.
/// @param type The mod-FX type.
/// @return The name; "Disabled" for NONE.
[[nodiscard]] std::string_view mod_fx_name(effects::ModFXType type);

/// @brief The display name of the mod-FX param @p param under type @p type, whose meaning the type changes (a
///        grain's "offset" is its own word).
/// @param type       The mod-FX type.
/// @param param      The param to name.
/// @param short_name Whether to give the short form the knob popups and column labels use.
/// @return The name.
[[nodiscard]] std::string_view mod_fx_param_name(effects::ModFXType type, effects::ModFxParam param, bool short_name);

/// @brief The name of the filter parameter a mode's morph knob sets, which its family decides.
/// @param mode       The filter mode.
/// @param short_name Whether to give the short form the column labels use (only the SVF family has one).
/// @return "Drive" for the low-pass ladders, "FM" for the high-pass ladder, "Morph" for the SVF modes and "None" for
///         OFF.
[[nodiscard]] std::string_view filter_morph_name(effects::FilterMode mode, bool short_name);

/// @brief The name of reverb preset @p preset, as the knobs' popups show it.
/// @param preset The preset's index into effects::kReverbPresets; it wraps modulo the number of presets.
/// @return "Small room", "Medium room" or "Large room" in the current language.
[[nodiscard]] std::string_view reverb_preset_name(size_t preset);

} // namespace deluge::l10n
