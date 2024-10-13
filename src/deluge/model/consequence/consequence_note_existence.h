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

#include "definitions_cxx.hpp"
#include "model/consequence/consequence.h"
#include "model/iterance/iterance.h"
#include <cstdint>

class Note;

class ConsequenceNoteExistence final : public Consequence {
public:
	ConsequenceNoteExistence(InstrumentClip* newClip, int32_t newNoteRowId, Note* note, ExistenceChangeType newType);
	Error revert(TimeType time, ModelStack* modelStack) override;

	InstrumentClip* clip;
	int32_t noteRowId;
	int32_t pos;
	int32_t length;
	int8_t velocity;
	int8_t probability;
	uint8_t lift;
	Iterance iterance;
	int8_t fill;

	ExistenceChangeType type;
};
