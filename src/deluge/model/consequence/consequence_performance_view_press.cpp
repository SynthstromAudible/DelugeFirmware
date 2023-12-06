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

#include "model/consequence/consequence_performance_view_press.h"
#include "hid/led/indicator_leds.h"
#include "model/model_stack.h"

ConsequencePerformanceViewPress::ConsequencePerformanceViewPress(FXColumnPress fxPressBefore[kDisplayWidth],
                                                                 FXColumnPress fxPressAfter[kDisplayWidth],
                                                                 int32_t xDisplay) {

	memcpy(&fxPress[BEFORE], &fxPressBefore[xDisplay], sizeFXPress);
	memcpy(&fxPress[AFTER], &fxPressAfter[xDisplay], sizeFXPress);

	xDisplayChanged = xDisplay;
}

int32_t ConsequencePerformanceViewPress::revert(TimeType time, ModelStack* modelStack) {
	memcpy(&performanceSessionView.fxPress[xDisplayChanged], &fxPress[time], sizeFXPress);

	performanceSessionView.initPadPress(performanceSessionView.lastPadPress);
	performanceSessionView.updateLayoutChangeStatus();
	performanceSessionView.renderViewDisplay();

	return NO_ERROR;
}
