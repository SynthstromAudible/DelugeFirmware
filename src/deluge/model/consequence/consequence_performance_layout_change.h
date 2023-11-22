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
#include "gui/views/performance_session_view.h"
#include "model/consequence/consequence.h"
#include <cstdint>

class ConsequencePerformanceLayoutChange final : public Consequence {
public:
	ConsequencePerformanceLayoutChange(PadPress(*padPressBefore), PadPress(*padPressAfter),
	                                   FXColumnPress(*FXPressBefore), FXColumnPress(*FXPressAfter),
	                                   ParamsForPerformance(*layoutBefore), ParamsForPerformance(*layoutAfter),
	                                   int32_t valuesBefore[kDisplayWidth][kDisplayHeight], int32_t valuesAfter[kDisplayWidth][kDisplayHeight], bool changesBefore,
	                                   bool changesAfter);
	int32_t revert(TimeType time, ModelStack* modelStack);

	PadPress lastPadPress[2];
	FXColumnPress FXPress[kDisplayWidth][2];
	ParamsForPerformance layoutForPerformance[kDisplayWidth][2];
	int32_t defaultFXValues[kDisplayWidth][kDisplayHeight][2];
	bool anyChangesToSave[2];
};
