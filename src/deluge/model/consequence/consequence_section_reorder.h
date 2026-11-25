/*
 * Copyright © 2017-2023 Synthstrom Audible Limited
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

#include "model/consequence/consequence.h"
#include <cstdint>

// A section swap is its own inverse, so undo and redo need only the two section IDs.
class ConsequenceSectionReorder final : public Consequence {
public:
	ConsequenceSectionReorder(uint8_t first_section, uint8_t second_section)
	    : first_section_(first_section), second_section_(second_section) {}
	Error revert(TimeType time, ModelStack* model_stack) override;
	static void swapSections(Song& song, uint8_t first_section, uint8_t second_section);

private:
	uint8_t first_section_;
	uint8_t second_section_;
};
