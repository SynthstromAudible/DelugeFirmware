/*
 * Copyright © 2018-2023 Synthstrom Audible Limited
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

#include "hid/display/numeric_layer/numeric_layer_loading_animation.h"
#include "gui/ui_timer_manager.h"
#include "string.h"

NumericLayerLoadingAnimation::NumericLayerLoadingAnimation() {
	loadingAnimationPos = 0;
}

NumericLayerLoadingAnimation::~NumericLayerLoadingAnimation() {
	// TODO Auto-generated destructor stub
}

void NumericLayerLoadingAnimation::isNowOnTop() {
	uiTimerManager.setTimer(TimerName::DISPLAY, kFlashTime);
}

bool NumericLayerLoadingAnimation::callBack() {

	loadingAnimationPos++;
	if (loadingAnimationPos == 10) {
		loadingAnimationPos = 0;
	}

	uiTimerManager.setTimer(TimerName::DISPLAY, kFlashTime);

	return false;
}

void NumericLayerLoadingAnimation::render(uint8_t* returnSegments) {
	if (animationIsTransparent && next) {
		next->render(returnSegments);
	}
	else {
		memset(returnSegments, 0, kNumericDisplayLength);
	}

	if (loadingAnimationPos < 4) {
		returnSegments[loadingAnimationPos] ^= 0x40;
	}
	else if (loadingAnimationPos == 4) {
		returnSegments[3] ^= 0x30;
	}
	else if (loadingAnimationPos < 9) {
		returnSegments[3 - (loadingAnimationPos - 5)] ^= 0x08;
	}
	else {
		returnSegments[0] ^= 0x06;
	}
}
