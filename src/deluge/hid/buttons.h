/*
 * Copyright © 2014-2023 Synthstrom Audible Limited
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

#include "button.h"
#include <stddef.h>
#include <string>

namespace Buttons {

ActionResult buttonAction(deluge::hid::Button b, bool on, bool inCardRoutine);
bool isButtonPressed(deluge::hid::Button b);
bool isAnyOfButtonsPressed(std::initializer_list<deluge::hid::Button> buttons);
bool isShiftButtonPressed();
bool isShiftStuck();
void noPressesHappening(bool inCardRoutine);
void ignoreCurrentShiftForSticky();
const char* getButtonName(deluge::hid::Button b);

/**
 * Notify the button management code that the shift button should no longer be considered sticky. We need an explicit
 * notification so we can clear the buttons.cpp:shiftIsHeld to avoid shift getting permanantly stuck.
 */
void clearShiftSticky();
/**
 * Determine if the shift button has changed since the last time someone asked.
 * This implicitly clears the "shiftHasChangedSinceLastCheck" flag, so only the main loop should call this function.
 */
bool shiftHasChanged();

extern bool recordButtonPressUsedUp;
extern bool considerCrossScreenReleaseForCrossScreenMode;
extern bool selectButtonPressUsedUp;
} // namespace Buttons
