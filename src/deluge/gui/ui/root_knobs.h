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
/// @brief The root views' gold knobs and mod buttons, over the knobs the menu shell shares.
///
/// The decisions are `ModKnobs`' (the shell's service, `sharedModKnobs()`); what they show goes to the legacy display,
/// `indicator_leds` and `View`. This header names no port type, so `UI`'s base hooks and the views include it.

#include "deluge-project/param_address.h"

#include <cstdint>
#include <optional>

/// @brief The root views' gold knobs and mod buttons: `UI`'s base hooks forward here.
namespace deluge::gui::ui::root_knobs {

/// @brief A gold-knob turn over a root view (accelerated by `interpretEncoders`).
/// @param knob   0 for the lower knob, 1 for the upper.
/// @param offset The accelerated detents.
void turn(int32_t knob, int32_t offset);
/// @brief A gold-knob push over a root view.
/// @param knob 0 for the lower knob, 1 for the upper.
/// @param on   Press or release.
void push(uint8_t knob, bool on);
/// @brief A mod button over a root view.
/// @param button 0..7.
/// @param on     Press or release.
void mod_button(uint8_t button, bool on);
/// @brief Both knob indicators from the knobs' focus (`View::setKnobIndicatorLevels`' non-editor arm).
void refresh_indicators();
/// @brief The address knob @p knob turns, if any: a param its owner holds, or a cable (clip view's automation copy
///        and paste).
/// @param knob 0 for the lower knob, 1 for the upper.
/// @return The address; none with no focus, for a knob on a setting or on nothing, or for a MIDI lane not made yet.
[[nodiscard]] std::optional<deluge::project::ParamAddress> knob_address(int32_t knob);

} // namespace deluge::gui::ui::root_knobs
