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

class ParamManagerForTimeline;

/// @file
/// @brief The UI half of a stutter gesture: the UI modes a stutter begins in, and entering and leaving
/// UI_MODE_STUTTERING with the LED redraw. Declared here for model/fx's stutter functions and defined by the app's UI
/// (gui/ui/stutter_ui.cpp), so model/ calls no UI of its own.

namespace deluge::model::stutter_ui {

/// @return Whether the UI is in a mode a stutter may begin in: a clip held in song view, an arranger row held or
/// auditioned, or auditioning.
[[nodiscard]] bool mayBegin();

/// @brief A stutter began: the LEDs are redrawn for @p paramManager, then the UI enters UI_MODE_STUTTERING.
/// @param paramManager The params of the owner stuttering.
void began(ParamManagerForTimeline* paramManager);

/// @brief A stutter ended: the LEDs are redrawn for @p paramManager when given, then the UI leaves UI_MODE_STUTTERING.
/// @param paramManager The params of the owner that stuttered, or null for no redraw.
void ended(ParamManagerForTimeline* paramManager);

} // namespace deluge::model::stutter_ui
