/*
 * ContextMenuLaunchStyle.cpp
 *
 *  Created on: 11 Jun 2023
 *      Author: Robin
 */

#include <ContextMenuLaunchStyle.h>
#include <RootUI.h>
#include "matrixdriver.h"
#include "numericdriver.h"
#include "IndicatorLEDs.h"
#include "extern.h"
#include "Clip.h"

#define VALUE_DEFA 0
#define VALUE_FILL 1

#define NUM_VALUES 2

ContextMenuLaunchStyle contextMenuLaunchStyle;

#if HAVE_OLED
char const* optionsls[] = {"Default", "Fill"};
#else
char const* optionsls[] = {"DEFA", "FILL"};
#endif

ContextMenuLaunchStyle::ContextMenuLaunchStyle() {
	basicOptions = optionsls;
	basicNumOptions = NUM_VALUES;
#if HAVE_OLED
	title = "Launch Style";
#endif
}

bool ContextMenuLaunchStyle::setupAndCheckAvailability() {

	switch(clip->launchStyle) {
	case LAUNCH_STYLE_DEFAULT:
		currentOption = VALUE_DEFA;
		break;
	case LAUNCH_STYLE_FILL:
		currentOption = VALUE_FILL;
		break;
	default:
		currentOption = VALUE_DEFA;
	}
    currentUIMode = UI_MODE_NONE;

#if HAVE_OLED
	scrollPos = currentOption;
#endif
	return true;
}


void ContextMenuLaunchStyle::selectEncoderAction(int8_t offset) {
	/* if (currentUIMode) return; */

	ContextMenu::selectEncoderAction(offset);

	switch(currentOption) {
	case VALUE_DEFA:
		clip->launchStyle = LAUNCH_STYLE_DEFAULT;
		break;
	case VALUE_FILL:
		clip->launchStyle = LAUNCH_STYLE_FILL;
		break;

	default:
		clip->launchStyle = LAUNCH_STYLE_DEFAULT;
	}
}
