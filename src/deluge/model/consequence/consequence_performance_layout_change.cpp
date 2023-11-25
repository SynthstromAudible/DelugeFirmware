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

#include "model/consequence/consequence_performance_layout_change.h"
#include "hid/led/indicator_leds.h"
#include "model/model_stack.h"

ConsequencePerformanceLayoutChange::ConsequencePerformanceLayoutChange(
    PadPress(*padPressBefore), PadPress(*padPressAfter), FXColumnPress(*FXPressBefore), FXColumnPress(*FXPressAfter),
    ParamsForPerformance(*layoutBefore), ParamsForPerformance(*layoutAfter),
    int32_t valuesBefore[kDisplayWidth][kDisplayHeight], int32_t valuesAfter[kDisplayWidth][kDisplayHeight]) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		memcpy(&FXPress[xDisplay][BEFORE], &FXPressBefore[xDisplay], sizeFXPress);
		memcpy(&layoutForPerformance[xDisplay][BEFORE], &layoutBefore[xDisplay], sizeParamsForPerformance);

		memcpy(&FXPress[xDisplay][AFTER], &FXPressAfter[xDisplay], sizeFXPress);
		memcpy(&layoutForPerformance[xDisplay][AFTER], &layoutAfter[xDisplay], sizeParamsForPerformance);

		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			defaultFXValues[xDisplay][yDisplay][BEFORE] = valuesBefore[xDisplay][yDisplay];
			defaultFXValues[xDisplay][yDisplay][AFTER] = valuesAfter[xDisplay][yDisplay];
		}
	}
	memcpy(&lastPadPress[BEFORE], &padPressBefore, sizePadPress);
	memcpy(&lastPadPress[AFTER], &padPressAfter, sizePadPress);
}

int32_t ConsequencePerformanceLayoutChange::revert(TimeType time, ModelStack* modelStack) {
	for (int32_t xDisplay = 0; xDisplay < kDisplayWidth; xDisplay++) {
		memcpy(&performanceSessionView.FXPress[xDisplay], &FXPress[xDisplay][time], sizeFXPress);
		memcpy(&performanceSessionView.layoutForPerformance[xDisplay], &layoutForPerformance[xDisplay][time],
		       sizeParamsForPerformance);

		for (int32_t yDisplay = 0; yDisplay < kDisplayHeight; yDisplay++) {
			performanceSessionView.defaultFXValues[xDisplay][yDisplay] = defaultFXValues[xDisplay][yDisplay][time];
		}
	}
	memcpy(&performanceSessionView.lastPadPress, &lastPadPress[time], sizePadPress);

	performanceSessionView.updateLayoutChangeStatus();

	return NO_ERROR;
}
