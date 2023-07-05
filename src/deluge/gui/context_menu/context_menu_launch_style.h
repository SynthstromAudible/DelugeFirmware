/*
 * ContextMenuLaunchStyle.h
 *
 *  Created on: 11 Jun 2023
 *      Author: Robin
 */

#ifndef CONTEXTMENULAUNCHSTYLE_H_
#define CONTEXTMENULAUNCHSTYLE_H_

#include "context_menu.h"

class Clip;

class ContextMenuLaunchStyle: public ContextMenu {
public:
	ContextMenuLaunchStyle();
	void selectEncoderAction(int8_t offset);
	bool setupAndCheckAvailability();
	bool canSeeViewUnderneath() { return true; }

	Clip* clip;
};

extern ContextMenuLaunchStyle contextMenuLaunchStyle;

#endif /* CONTEXTMENULAUNCHSTYLE_H_ */
