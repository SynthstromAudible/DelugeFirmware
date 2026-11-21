/*
 * Copyright © 2016-2023 Synthstrom Audible Limited
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

// Callers which may create an undo action from a UI which otherwise can't.
enum class ActionOrigin { DEFAULT, RESET_PARAM_VALUES };

constexpr bool actionAllowedFromUI(bool root, bool soundEditor, bool gridEditor, bool patternLoader,
                                   ActionOrigin origin) {
	// Reset Param Values creates its action from its confirmation menu.
	return root || patternLoader || (soundEditor && gridEditor) || origin == ActionOrigin::RESET_PARAM_VALUES;
}
